#include "TransformGizmo2D.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../tools/EditorTransformUtils.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../tilemap/Tilemap.hpp"
#include <cmath>

namespace Engine {

static float distance_to_segment(const ImVec2& point, const ImVec2& start, const ImVec2& end) {
    const ImVec2 segment(end.x - start.x, end.y - start.y);
    const float length_sq = segment.x * segment.x + segment.y * segment.y;
    if (length_sq <= 0.0001f) return std::hypot(point.x - start.x, point.y - start.y);
    const float projection = std::clamp(((point.x - start.x) * segment.x + (point.y - start.y) * segment.y) / length_sq, 0.0f, 1.0f);
    const ImVec2 closest(start.x + segment.x * projection, start.y + segment.y * projection);
    return std::hypot(point.x - closest.x, point.y - closest.y);
}

static void draw_arrow(ImDrawList* draw_list, const ImVec2& start, const ImVec2& end, ImU32 color, float size) {
    draw_list->AddLine(start, end, color, 4.0f);
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::hypot(dx, dy);
    if (length <= 0.001f) return;
    const ImVec2 direction(dx / length, dy / length);
    const ImVec2 perpendicular(-direction.y, direction.x);
    const ImVec2 tip(end.x + direction.x * size, end.y + direction.y * size);
    const ImVec2 left(end.x - direction.x * size * 0.25f + perpendicular.x * size * 0.5f, end.y - direction.y * size * 0.25f + perpendicular.y * size * 0.5f);
    const ImVec2 right(end.x - direction.x * size * 0.25f - perpendicular.x * size * 0.5f, end.y - direction.y * size * 0.25f - perpendicular.y * size * 0.5f);
    draw_list->AddTriangleFilled(tip, left, right, color);
}

static bool is_entity_locked_from_gizmo(EntityManager& em, EntityID id) {
    if (!em.is_alive(id)) return true;
    return em.has_component<TilemapGeneratedColliderComponent>(id);
}

bool TransformGizmo2D::draw_gizmo(
    EntityManager& em,
    const std::unordered_set<EntityID>& selected_entities,
    const Camera2D& camera,
    const ImVec2& viewport_pos,
    const ImVec2& viewport_size,
    bool is_viewport_hovered,
    EditorCommandHistory* command_history
) {
    if (selected_entities.empty()) { m_active_axis = GizmoAxis::None; m_hovered_axis = GizmoAxis::None; m_start_transforms.clear(); return false; }
    if (selected_entities.size() > 1 && !is_using()) m_space = GizmoSpace::World;

    // Calculate group centroid excluding locked entities
    glm::vec3 centroid(0.0f, 0.0f, 0.0f);
    size_t valid_count = 0;

    for (EntityID id : selected_entities) {
        if (em.is_alive(id) && em.has_component<TransformComponent>(id) && !is_entity_locked_from_gizmo(em, id)) {
            const auto& tx = em.get_component<TransformComponent>(id);
            glm::vec3 entity_pos = tx.world_position;

            if (em.has_component<TilemapComponent>(id)) {
                const auto& tm = em.get_component<TilemapComponent>(id);
                if (!tm.empty()) {
                    int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
                    tm.get_bounds(min_x, min_y, max_x, max_y);
                    float tile_sz_x = tm.tile_size * tx.world_scale.x;
                    float tile_sz_y = tm.tile_size * tx.world_scale.y;

                    float center_x = tx.world_position.x + ((static_cast<float>(min_x + max_x + 1) * 0.5f) * tile_sz_x);
                    float center_y = tx.world_position.y - ((static_cast<float>(min_y + max_y + 1) * 0.5f) * tile_sz_y);
                    entity_pos = glm::vec3(center_x, center_y, tx.world_position.z);
                }
            }

            centroid += entity_pos;
            valid_count++;
        }
    }

    if (valid_count == 0) {
        m_active_axis = GizmoAxis::None;
        m_hovered_axis = GizmoAxis::None;
        m_start_transforms.clear();
        return false;
    }

    centroid /= static_cast<float>(valid_count);

    // Convert centroid to screen coordinates
    glm::vec2 cam_screen = camera.world_to_screen(centroid);

    float scale_x = viewport_size.x / camera.get_viewport_size().x;
    float scale_y = viewport_size.y / camera.get_viewport_size().y;

    // Pixel-snap the common origin so both axis strokes and the free-move
    // handle meet cleanly without sub-pixel anti-alias seams.
    ImVec2 origin(
        std::round(viewport_pos.x + cam_screen.x * scale_x),
        std::round(viewport_pos.y + cam_screen.y * scale_y)
    );

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos = io.MousePos;

    float gizmo_len = 75.0f;
    float arrow_size = 14.0f;
    float center_size = 20.0f;

    // Local space is meaningful only for one selected entity.
    glm::vec2 axis_x(1.0f, 0.0f);
    glm::vec2 axis_y(0.0f, 1.0f);
    if (m_space == GizmoSpace::Local && selected_entities.size() == 1) {
        EntityID id = *selected_entities.begin();
        if (em.is_alive(id) && em.has_component<TransformComponent>(id)) {
            // SDL renderer rotation is clockwise in screen space; convert it
            // before deriving the editor's world-space local axes.
            const float radians = glm::radians(-em.get_component<TransformComponent>(id).world_rotation);
            axis_x = glm::vec2(std::cos(radians), std::sin(radians));
            axis_y = glm::vec2(-std::sin(radians), std::cos(radians));
        }
    }

    glm::vec2 x_screen(axis_x.x * scale_x, -axis_x.y * scale_y);
    glm::vec2 y_screen(axis_y.x * scale_x, -axis_y.y * scale_y);
    x_screen = glm::normalize(x_screen) * gizmo_len;
    y_screen = glm::normalize(y_screen) * gizmo_len;
    ImVec2 x_axis_end(origin.x + x_screen.x, origin.y + x_screen.y);
    ImVec2 y_axis_end(origin.x + y_screen.x, origin.y + y_screen.y);
    // Let Y reach the lower edge of the X stroke while X is trimmed by half
    // its stroke. This produces a deliberate, flush L junction.
    const ImVec2 x_axis_start(origin.x + x_screen.x / gizmo_len * 2.0f, origin.y + x_screen.y / gizmo_len * 2.0f);
    const ImVec2 y_axis_start(origin.x - y_screen.x / gizmo_len * 2.0f, origin.y - y_screen.y / gizmo_len * 2.0f);

    // Unity-style free-move handle: its bottom-left corner touches the axis
    // intersection, placing it in the positive X/Y quadrant.
    const bool is_move_tool = m_tool == TransformTool::Move;
    const float center_half = center_size * 0.5f;
    const ImVec2 center_handle_min = is_move_tool
        ? ImVec2(origin.x, origin.y - center_size)
        : ImVec2(origin.x - center_half, origin.y - center_half);
    const ImVec2 center_handle_max = is_move_tool
        ? ImVec2(origin.x + center_size, origin.y)
        : ImVec2(origin.x + center_half, origin.y + center_half);

    // Convert mouse to world space
    glm::vec2 mouse_in_viewport(
        (mouse_pos.x - viewport_pos.x) / scale_x,
        (mouse_pos.y - viewport_pos.y) / scale_y
    );
    glm::vec2 mouse_world = camera.screen_to_world(mouse_in_viewport);

    const float rotate_radius = 52.0f;
    const float mouse_radius = std::hypot(mouse_pos.x - origin.x, mouse_pos.y - origin.y);
    bool cancelled_this_frame = false;

    // Escape cancels the entire gesture, restoring the snapshot captured on
    // mouse-down. It must happen before release/commit and before applying the
    // current mouse delta again.
    if (m_active_axis != GizmoAxis::None && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        for (const auto& [id, start] : m_start_transforms) {
            if (em.is_alive(id) && em.has_component<TransformComponent>(id)) {
                em.get_component<TransformComponent>(id) = start;
            }
        }
        TransformSystem::update(em);
        for (const auto& [id, start] : m_start_transforms) {
            (void)start;
            if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;
            const auto& transform = em.get_component<TransformComponent>(id);
            PhysicsSystem::set_position(em, id, glm::vec2(transform.world_position));
        }
        m_active_axis = GizmoAxis::None;
        m_hovered_axis = GizmoAxis::None;
        m_start_transforms.clear();
        cancelled_this_frame = true;
    }

    // --- Hover Detection ---
    if (m_active_axis == GizmoAxis::None) {
        m_hovered_axis = GizmoAxis::None;

        if (is_viewport_hovered) {
            if (m_tool == TransformTool::Rotate && std::abs(mouse_radius - rotate_radius) <= 8.0f) {
                m_hovered_axis = GizmoAxis::CenterXY;
            } else if (mouse_pos.x >= center_handle_min.x && mouse_pos.x <= center_handle_max.x &&
                mouse_pos.y >= center_handle_min.y && mouse_pos.y <= center_handle_max.y) {
                m_hovered_axis = GizmoAxis::CenterXY;
            }
            else if (m_tool == TransformTool::Scale &&
                mouse_pos.x >= x_axis_end.x - 10.0f && mouse_pos.x <= x_axis_end.x + 10.0f &&
                mouse_pos.y >= x_axis_end.y - 10.0f && mouse_pos.y <= x_axis_end.y + 10.0f) {
                m_hovered_axis = GizmoAxis::AxisX;
            }
            else if (m_tool == TransformTool::Scale &&
                mouse_pos.x >= y_axis_end.x - 10.0f && mouse_pos.x <= y_axis_end.x + 10.0f &&
                mouse_pos.y >= y_axis_end.y - 10.0f && mouse_pos.y <= y_axis_end.y + 10.0f) {
                m_hovered_axis = GizmoAxis::AxisY;
            }
            // Axis lines remain interactive for quick constrained transforms.
            else if (distance_to_segment(mouse_pos, origin, x_axis_end) <= 14.0f) m_hovered_axis = GizmoAxis::AxisX;
            else if (distance_to_segment(mouse_pos, origin, y_axis_end) <= 14.0f) m_hovered_axis = GizmoAxis::AxisY;
        }
    }

    // --- Drag Interaction Start ---
    if (io.MouseClicked[ImGuiMouseButton_Left] && is_viewport_hovered && m_hovered_axis != GizmoAxis::None) {
        m_active_axis = m_hovered_axis;
        m_drag_start_world = mouse_world;
        m_drag_origin = glm::vec2(origin.x, origin.y);
        m_drag_start_angle = std::atan2(mouse_pos.y - origin.y, mouse_pos.x - origin.x);
        m_drag_start_distance = std::max(1.0f, mouse_radius);
        m_drag_axis_x = axis_x;
        m_drag_axis_y = axis_y;
        m_start_transforms.clear();

        for (EntityID id : selected_entities) {
            if (em.is_alive(id) && em.has_component<TransformComponent>(id) && !is_entity_locked_from_gizmo(em, id) && !has_selected_ancestor(em, id, selected_entities)) {
                m_start_transforms[id] = em.get_component<TransformComponent>(id);
            }
        }
    }

    // --- Drag Interaction End (Record Command for Undo/Redo) ---
    if (!io.MouseDown[ImGuiMouseButton_Left] && m_active_axis != GizmoAxis::None) {
        if (command_history && !m_start_transforms.empty()) {
            std::unordered_map<EntityID, TransformComponent> after;
            bool any_changed = false;
            for (const auto& [id, start] : m_start_transforms) {
                if (em.is_alive(id) && em.has_component<TransformComponent>(id)) {
                    const auto current = em.get_component<TransformComponent>(id);
                    after[id] = current;
                    if (glm::distance(start.position, current.position) > 0.001f || std::abs(start.rotation - current.rotation) > 0.001f || glm::distance(start.scale, current.scale) > 0.001f) {
                        any_changed = true;
                    }
                }
            }

            if (any_changed) {
                const char* operation = m_tool == TransformTool::Move ? "Move" : (m_tool == TransformTool::Rotate ? "Rotate" : "Scale");
                command_history->push_executed_command(std::make_unique<TransformEntitiesCommand>(&em, m_start_transforms, after, std::string(operation) + " Entities"));
            }
        }

        m_active_axis = GizmoAxis::None;
    }

    // --- Apply preview transform ---
    if (m_active_axis != GizmoAxis::None) {
        glm::vec2 world_delta = mouse_world - m_drag_start_world;
        const bool snap = m_snap.enabled != io.KeyShift;
        if (snap && m_tool == TransformTool::Move && m_snap.position_step > 0.0f) {
            world_delta.x = std::round(world_delta.x / m_snap.position_step) * m_snap.position_step;
            world_delta.y = std::round(world_delta.y / m_snap.position_step) * m_snap.position_step;
        }
        float angle_delta = glm::degrees(std::atan2(mouse_pos.y - m_drag_origin.y, mouse_pos.x - m_drag_origin.x) - m_drag_start_angle);
        if (snap && m_tool == TransformTool::Rotate && m_snap.rotation_step > 0.0f) angle_delta = std::round(angle_delta / m_snap.rotation_step) * m_snap.rotation_step;
        float scale_factor = std::max(0.01f, mouse_radius / m_drag_start_distance);
        if (snap && m_tool == TransformTool::Scale && m_snap.scale_step > 0.0f) scale_factor = std::max(0.01f, 1.0f + std::round((scale_factor - 1.0f) / m_snap.scale_step) * m_snap.scale_step);

        for (const auto& [id, start] : m_start_transforms) {
            if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;

            auto& transform = em.get_component<TransformComponent>(id);
            if (m_tool == TransformTool::Move) {
                glm::vec2 constrained_delta = world_delta;
                if (m_active_axis == GizmoAxis::AxisX) constrained_delta = m_drag_axis_x * glm::dot(world_delta, m_drag_axis_x);
                else if (m_active_axis == GizmoAxis::AxisY) constrained_delta = m_drag_axis_y * glm::dot(world_delta, m_drag_axis_y);
                transform.position = start.position + glm::vec3(world_delta_to_local(em, id, constrained_delta), 0.0f);
            } else if (m_tool == TransformTool::Rotate) {
                transform.rotation = start.rotation + angle_delta;
            } else {
                if (m_active_axis == GizmoAxis::AxisX) transform.scale.x = std::max(0.01f, start.scale.x * scale_factor);
                else if (m_active_axis == GizmoAxis::AxisY) transform.scale.y = std::max(0.01f, start.scale.y * scale_factor);
                else transform.scale = glm::max(glm::vec2(0.01f), start.scale * scale_factor);
            }
        }

        TransformSystem::update(em);
        for (const auto& [id, start] : m_start_transforms) {
            if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;
            const auto& transform = em.get_component<TransformComponent>(id);
            PhysicsSystem::set_position(em, id, glm::vec2(transform.world_position.x, transform.world_position.y));
        }
    }

    // --- Render Gizmo ---
    ImU32 col_x = (m_active_axis == GizmoAxis::AxisX || m_hovered_axis == GizmoAxis::AxisX) ? IM_COL32(255, 105, 105, 255) : IM_COL32(245, 60, 60, 240);
    ImU32 col_y = (m_active_axis == GizmoAxis::AxisY || m_hovered_axis == GizmoAxis::AxisY) ? IM_COL32(110, 250, 125, 255) : IM_COL32(60, 235, 80, 240);
    ImU32 col_c = (m_active_axis == GizmoAxis::CenterXY || m_hovered_axis == GizmoAxis::CenterXY) ? IM_COL32(105, 198, 255, 255) : IM_COL32(70, 180, 255, 210);

    if (m_tool == TransformTool::Rotate) {
        ImU32 col = (m_hovered_axis == GizmoAxis::CenterXY || m_active_axis == GizmoAxis::CenterXY) ? IM_COL32(110, 198, 255, 255) : IM_COL32(80, 170, 255, 230);
        draw_list->AddCircle(origin, rotate_radius, col, 48, 3.0f);
        return cancelled_this_frame || m_active_axis != GizmoAxis::None || m_hovered_axis != GizmoAxis::None;
    }

    // Center handle (move XY / uniform scale)
    draw_list->AddRectFilled(center_handle_min, center_handle_max, col_c, 0.0f);
    draw_list->AddRect(center_handle_min, center_handle_max, IM_COL32(112, 124, 158, 210), 0.0f, 0, 1.5f);

    // Local X / Y axes, aligned to the selected entity's rotation.
    if (m_tool == TransformTool::Scale) {
        draw_list->AddLine(x_axis_start, x_axis_end, col_x, 3.0f);
        draw_list->AddLine(y_axis_start, y_axis_end, col_y, 3.0f);
        draw_list->AddRectFilled(
            ImVec2(x_axis_end.x - 8.0f, x_axis_end.y - 8.0f),
            ImVec2(x_axis_end.x + 8.0f, x_axis_end.y + 8.0f),
            col_x,
            2.0f
        );
        draw_list->AddRectFilled(
            ImVec2(y_axis_end.x - 8.0f, y_axis_end.y - 8.0f),
            ImVec2(y_axis_end.x + 8.0f, y_axis_end.y + 8.0f),
            col_y,
            2.0f
        );
    } else {
        draw_arrow(draw_list, x_axis_start, x_axis_end, col_x, arrow_size);
        draw_arrow(draw_list, y_axis_start, y_axis_end, col_y, arrow_size);
    }

    return cancelled_this_frame || m_active_axis != GizmoAxis::None || m_hovered_axis != GizmoAxis::None;
}

} // namespace Engine
