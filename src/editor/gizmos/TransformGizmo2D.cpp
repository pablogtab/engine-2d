#include "TransformGizmo2D.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
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
    if (em.has_component<TagComponent>(id)) {
        const auto& tag = em.get_component<TagComponent>(id).tag;
        if (tag.find("Tilemap_Composite_Collider") != std::string::npos ||
            tag.find("Tilemap_Collider") != std::string::npos) {
            return true;
        }
    }
    if (em.has_component<HierarchyComponent>(id)) {
        EntityID parent_id = em.get_component<HierarchyComponent>(id).parent;
        if (parent_id != 0 && em.is_alive(parent_id) && em.has_component<TilemapComponent>(parent_id)) {
            return true;
        }
    }
    return false;
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
    if (selected_entities.empty()) {
        m_active_axis = GizmoAxis::None;
        m_hovered_axis = GizmoAxis::None;
        m_entity_start_positions.clear();
        return false;
    }

    // Calculate group centroid excluding locked entities
    glm::vec3 centroid(0.0f, 0.0f, 0.0f);
    size_t valid_count = 0;

    for (EntityID id : selected_entities) {
        if (em.is_alive(id) && em.has_component<TransformComponent>(id) && !is_entity_locked_from_gizmo(em, id)) {
            const auto& tx = em.get_component<TransformComponent>(id);
            glm::vec3 entity_pos = tx.position;

            if (em.has_component<TilemapComponent>(id)) {
                const auto& tm = em.get_component<TilemapComponent>(id);
                if (!tm.empty()) {
                    int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
                    tm.get_bounds(min_x, min_y, max_x, max_y);
                    float tile_sz_x = tm.tile_size * tx.world_scale.x;
                    float tile_sz_y = tm.tile_size * tx.world_scale.y;

                    float center_x = tx.position.x + ((static_cast<float>(min_x + max_x + 1) * 0.5f) * tile_sz_x);
                    float center_y = tx.position.y - ((static_cast<float>(min_y + max_y + 1) * 0.5f) * tile_sz_y);
                    entity_pos = glm::vec3(center_x, center_y, tx.position.z);
                }
            }

            centroid += entity_pos;
            valid_count++;
        }
    }

    if (valid_count == 0) {
        m_active_axis = GizmoAxis::None;
        m_hovered_axis = GizmoAxis::None;
        m_entity_start_positions.clear();
        return false;
    }

    centroid /= static_cast<float>(valid_count);

    // Convert centroid to screen coordinates
    glm::vec2 cam_screen = camera.world_to_screen(centroid);

    float scale_x = viewport_size.x / camera.get_viewport_size().x;
    float scale_y = viewport_size.y / camera.get_viewport_size().y;

    ImVec2 origin(
        viewport_pos.x + cam_screen.x * scale_x,
        viewport_pos.y + cam_screen.y * scale_y
    );

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos = io.MousePos;

    float gizmo_len = 75.0f;
    float arrow_size = 14.0f;
    float center_size = 20.0f;

    // A single selection uses its local axes. Group transforms intentionally use
    // world axes because a group has no unambiguous local orientation.
    glm::vec2 axis_x(1.0f, 0.0f);
    glm::vec2 axis_y(0.0f, 1.0f);
    if (selected_entities.size() == 1) {
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

    // Convert mouse to world space
    glm::vec2 mouse_in_viewport(
        (mouse_pos.x - viewport_pos.x) / scale_x,
        (mouse_pos.y - viewport_pos.y) / scale_y
    );
    glm::vec2 mouse_world = camera.screen_to_world(mouse_in_viewport);

    // --- Hover Detection (When not actively dragging) ---
    if (m_active_axis == GizmoAxis::None) {
        m_hovered_axis = GizmoAxis::None;

        if (is_viewport_hovered) {
            // Center square
            if (mouse_pos.x >= origin.x - center_size * 0.5f && mouse_pos.x <= origin.x + center_size * 0.5f &&
                mouse_pos.y >= origin.y - center_size * 0.5f && mouse_pos.y <= origin.y + center_size * 0.5f) {
                m_hovered_axis = GizmoAxis::CenterXY;
            }
            // X axis arrow
            else if (distance_to_segment(mouse_pos, origin, x_axis_end) <= 14.0f) {
                m_hovered_axis = GizmoAxis::AxisX;
            }
            // Y axis arrow
            else if (distance_to_segment(mouse_pos, origin, y_axis_end) <= 14.0f) {
                m_hovered_axis = GizmoAxis::AxisY;
            }
        }
    }

    // --- Drag Interaction Start ---
    if (io.MouseClicked[ImGuiMouseButton_Left] && is_viewport_hovered && m_hovered_axis != GizmoAxis::None) {
        m_active_axis = m_hovered_axis;
        m_drag_start_world = mouse_world;
        m_drag_axis_x = axis_x;
        m_drag_axis_y = axis_y;
        m_entity_start_positions.clear();

        for (EntityID id : selected_entities) {
            if (em.is_alive(id) && em.has_component<TransformComponent>(id) && !is_entity_locked_from_gizmo(em, id)) {
                m_entity_start_positions[id] = em.get_component<TransformComponent>(id).position;
            }
        }
    }

    // --- Drag Interaction End (Record Command for Undo/Redo) ---
    if (!io.MouseDown[ImGuiMouseButton_Left] && m_active_axis != GizmoAxis::None) {
        if (command_history && !m_entity_start_positions.empty()) {
            std::unordered_map<EntityID, glm::vec3> new_positions;
            bool any_changed = false;

            for (const auto& [id, start_pos] : m_entity_start_positions) {
                if (em.is_alive(id) && em.has_component<TransformComponent>(id)) {
                    glm::vec3 current_p = em.get_component<TransformComponent>(id).position;
                    new_positions[id] = current_p;
                    if (glm::distance(start_pos, current_p) > 0.001f) {
                        any_changed = true;
                    }
                }
            }

            if (any_changed) {
                std::string desc = (m_entity_start_positions.size() == 1) 
                    ? "Move " + (em.has_component<TagComponent>(m_entity_start_positions.begin()->first) 
                        ? em.get_component<TagComponent>(m_entity_start_positions.begin()->first).tag : "Entity")
                    : "Move (" + std::to_string(m_entity_start_positions.size()) + ") Entities";

                command_history->push_executed_command(
                    std::make_unique<MoveEntitiesCommand>(&em, m_entity_start_positions, new_positions, desc)
                );
            }
        }

        m_active_axis = GizmoAxis::None;
    }

    // --- Apply Translation to ALL Entities in the Selection ---
    if (m_active_axis != GizmoAxis::None) {
        glm::vec2 world_delta = mouse_world - m_drag_start_world;

        for (const auto& [id, start_pos] : m_entity_start_positions) {
            if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;

            auto& transform = em.get_component<TransformComponent>(id);
            if (m_active_axis == GizmoAxis::AxisX) {
                transform.position = start_pos + glm::vec3(m_drag_axis_x * glm::dot(world_delta, m_drag_axis_x), 0.0f);
            } else if (m_active_axis == GizmoAxis::AxisY) {
                transform.position = start_pos + glm::vec3(m_drag_axis_y * glm::dot(world_delta, m_drag_axis_y), 0.0f);
            } else if (m_active_axis == GizmoAxis::CenterXY) {
                transform.position.x = start_pos.x + world_delta.x;
                transform.position.y = start_pos.y + world_delta.y;
            }

            transform.world_position = transform.position;
            PhysicsSystem::set_position(em, id, glm::vec2(transform.position.x, transform.position.y));
        }
    }

    // --- Render Gizmo ---
    ImU32 col_x = (m_active_axis == GizmoAxis::AxisX || m_hovered_axis == GizmoAxis::AxisX) ? IM_COL32(255, 235, 50, 255) : IM_COL32(245, 60, 60, 240);
    ImU32 col_y = (m_active_axis == GizmoAxis::AxisY || m_hovered_axis == GizmoAxis::AxisY) ? IM_COL32(255, 235, 50, 255) : IM_COL32(60, 235, 80, 240);
    ImU32 col_c = (m_active_axis == GizmoAxis::CenterXY || m_hovered_axis == GizmoAxis::CenterXY) ? IM_COL32(255, 255, 80, 255) : IM_COL32(70, 180, 255, 210);

    // 1. Center Box Handle
    draw_list->AddRectFilled(
        ImVec2(origin.x - 8.0f, origin.y - 8.0f),
        ImVec2(origin.x + 8.0f, origin.y + 8.0f),
        col_c, 3.0f
    );
    draw_list->AddRect(
        ImVec2(origin.x - 8.0f, origin.y - 8.0f),
        ImVec2(origin.x + 8.0f, origin.y + 8.0f),
        IM_COL32(255, 255, 255, 240), 3.0f, 0, 1.5f
    );

    // 2. Local X / Y axes, aligned to the selected entity's rotation.
    draw_arrow(draw_list, origin, x_axis_end, col_x, arrow_size);
    draw_arrow(draw_list, origin, y_axis_end, col_y, arrow_size);

    return (m_active_axis != GizmoAxis::None || m_hovered_axis != GizmoAxis::None);
}

} // namespace Engine
