#include "TransformGizmo2D.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../tools/EditorTransformUtils.hpp"
#include "../tools/EditorEntityVisibilityFilter.hpp"
#include "../tools/ViewportTransformMath.hpp"
#include "../tools/ViewportSelectionGeometry.hpp"
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

static bool is_entity_locked_from_gizmo(EntityManager& em, EntityID id, const EditorEntityVisibilityFilter* visibility_filter) {
    if (!em.is_alive(id)) return true;
    if (visibility_filter && !visibility_filter->should_interact(id)) return true;
    return em.has_component<TilemapGeneratedColliderComponent>(id);
}

bool TransformGizmo2D::draw_gizmo(
    EntityManager& em,
    const std::unordered_set<EntityID>& selected_entities,
    const Camera2D& camera,
    const ImVec2& viewport_pos,
    const ImVec2& viewport_size,
    bool is_viewport_hovered,
    EditorCommandHistory* command_history,
    const EditorEntityVisibilityFilter* visibility_filter
) {
    if (selected_entities.empty()) {
        m_active_axis = GizmoAxis::None;
        m_hovered_axis = GizmoAxis::None;
        m_start_root_snapshots.clear();
        return false;
    }

    // Update transform system immediately so world coordinates are completely fresh
    TransformSystem::update(em);

    // Step 1: Collect all transformable roots in the active selection
    std::vector<EntityID> transformable_roots;
    transformable_roots.reserve(selected_entities.size());
    std::vector<glm::vec3> root_positions;
    root_positions.reserve(selected_entities.size());

    for (EntityID id : selected_entities) {
        if (em.is_alive(id) && em.has_component<TransformComponent>(id) &&
            !is_entity_locked_from_gizmo(em, id, visibility_filter) &&
            !has_selected_ancestor(em, id, selected_entities)) {
            transformable_roots.push_back(id);
            root_positions.push_back(em.get_component<TransformComponent>(id).world_position);
        }
    }

    if (transformable_roots.empty()) {
        m_active_axis = GizmoAxis::None;
        m_hovered_axis = GizmoAxis::None;
        m_start_root_snapshots.clear();
        return false;
    }

    // Force World space when multiple transformable roots exist
    if (transformable_roots.size() > 1 && !is_using()) {
        m_space = GizmoSpace::World;
    }

    // Calculate group centroid over transformable roots using pure math helper
    const glm::vec3 centroid = ViewportTransformMath::compute_centroid(root_positions);

    // Convert centroid to screen coordinates
    glm::vec2 cam_screen = camera.world_to_screen(centroid);

    float scale_x = viewport_size.x / camera.get_viewport_size().x;
    float scale_y = viewport_size.y / camera.get_viewport_size().y;

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

    // Draw multi-selection bounding box visual indicator when multiple roots are selected
    if (transformable_roots.size() > 1) {
        glm::vec2 group_min(std::numeric_limits<float>::max());
        glm::vec2 group_max(std::numeric_limits<float>::lowest());
        bool has_valid_bounds = false;

        for (EntityID id : transformable_roots) {
            ViewportSelectionQuad quad;
            if (get_selection_quad(em, id, quad)) {
                for (const auto& pt : quad.corners) {
                    group_min = glm::min(group_min, pt);
                    group_max = glm::max(group_max, pt);
                }
                has_valid_bounds = true;
            } else if (em.has_component<TransformComponent>(id)) {
                const auto& tx = em.get_component<TransformComponent>(id);
                glm::vec2 pt(tx.world_position.x, tx.world_position.y);
                group_min = glm::min(group_min, pt - glm::vec2(8.0f));
                group_max = glm::max(group_max, pt + glm::vec2(8.0f));
                has_valid_bounds = true;
            }
        }

        if (has_valid_bounds && group_min.x < group_max.x && group_min.y < group_max.y) {
            const glm::vec2 s_tl = camera.world_to_screen(glm::vec3(group_min.x, group_max.y, 0.0f));
            const glm::vec2 s_br = camera.world_to_screen(glm::vec3(group_max.x, group_min.y, 0.0f));
            const ImVec2 box_min(viewport_pos.x + s_tl.x * scale_x - 3.0f, viewport_pos.y + s_tl.y * scale_y - 3.0f);
            const ImVec2 box_max(viewport_pos.x + s_br.x * scale_x + 3.0f, viewport_pos.y + s_br.y * scale_y + 3.0f);

            // Subtle bounding box
            draw_list->AddRect(box_min, box_max, IM_COL32(99, 102, 241, 120), 2.0f, 0, 1.0f);

            // Small corner ticks
            const float tick = 6.0f;
            const ImU32 tick_col = IM_COL32(129, 140, 248, 200);
            draw_list->AddLine(box_min, ImVec2(box_min.x + tick, box_min.y), tick_col, 1.5f);
            draw_list->AddLine(box_min, ImVec2(box_min.x, box_min.y + tick), tick_col, 1.5f);
            draw_list->AddLine(ImVec2(box_max.x, box_min.y), ImVec2(box_max.x - tick, box_min.y), tick_col, 1.5f);
            draw_list->AddLine(ImVec2(box_max.x, box_min.y), ImVec2(box_max.x, box_min.y + tick), tick_col, 1.5f);
            draw_list->AddLine(ImVec2(box_min.x, box_max.y), ImVec2(box_min.x + tick, box_max.y), tick_col, 1.5f);
            draw_list->AddLine(ImVec2(box_min.x, box_max.y), ImVec2(box_min.x, box_max.y - tick), tick_col, 1.5f);
            draw_list->AddLine(box_max, ImVec2(box_max.x - tick, box_max.y), tick_col, 1.5f);
            draw_list->AddLine(box_max, ImVec2(box_max.x, box_max.y - tick), tick_col, 1.5f);

            // Centroid anchor marker
            draw_list->AddCircle(origin, 3.5f, IM_COL32(105, 198, 255, 200), 12, 1.2f);
        }
    }

    // Local space is meaningful only for one transformable root.
    glm::vec2 axis_x(1.0f, 0.0f);
    glm::vec2 axis_y(0.0f, 1.0f);
    if (m_space == GizmoSpace::Local && transformable_roots.size() == 1) {
        EntityID id = transformable_roots.front();
        if (em.is_alive(id) && em.has_component<TransformComponent>(id)) {
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
    const ImVec2 x_axis_start(origin.x + x_screen.x / gizmo_len * 2.0f, origin.y + x_screen.y / gizmo_len * 2.0f);
    const ImVec2 y_axis_start(origin.x - y_screen.x / gizmo_len * 2.0f, origin.y - y_screen.y / gizmo_len * 2.0f);

    const bool is_move_tool = m_tool == TransformTool::Move;
    const float center_half = center_size * 0.5f;
    const ImVec2 center_handle_min = is_move_tool
        ? ImVec2(origin.x, origin.y - center_size)
        : ImVec2(origin.x - center_half, origin.y - center_half);
    const ImVec2 center_handle_max = is_move_tool
        ? ImVec2(origin.x + center_size, origin.y)
        : ImVec2(origin.x + center_half, origin.y + center_half);

    glm::vec2 mouse_in_viewport(
        (mouse_pos.x - viewport_pos.x) / scale_x,
        (mouse_pos.y - viewport_pos.y) / scale_y
    );
    glm::vec2 mouse_world = camera.screen_to_world(mouse_in_viewport);

    const float rotate_radius = 52.0f;
    const float mouse_radius = std::hypot(mouse_pos.x - origin.x, mouse_pos.y - origin.y);
    bool cancelled_this_frame = false;

    // Escape cancellation: restore captured local transforms and sync physics
    if (m_active_axis != GizmoAxis::None && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        for (const auto& [id, snapshot] : m_start_root_snapshots) {
            if (em.is_alive(id) && em.has_component<TransformComponent>(id)) {
                em.get_component<TransformComponent>(id) = snapshot.local;
            }
        }
        TransformSystem::update(em);
        for (const auto& [id, snapshot] : m_start_root_snapshots) {
            (void)snapshot;
            if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;
            PhysicsSystem::sync_body_hierarchy(em, id);
        }
        m_active_axis = GizmoAxis::None;
        m_hovered_axis = GizmoAxis::None;
        m_start_root_snapshots.clear();
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
        m_drag_centroid = centroid;
        m_start_root_snapshots.clear();

        TransformSystem::update(em);

        for (EntityID id : transformable_roots) {
            const auto& tx = em.get_component<TransformComponent>(id);
            EntityID parent_id = 0;
            if (em.has_component<HierarchyComponent>(id)) {
                parent_id = em.get_component<HierarchyComponent>(id).parent;
            }
            TransformComponent world_snapshot = tx;
            world_snapshot.position = tx.world_position;
            world_snapshot.rotation = tx.world_rotation;
            world_snapshot.scale = tx.world_scale;
            world_snapshot.pivot = tx.world_pivot;
            world_snapshot.skew = tx.world_skew;
            world_snapshot.visible = tx.world_visible;

            m_start_root_snapshots[id] = RootTransformSnapshot{
                tx,
                world_snapshot,
                parent_id
            };
        }
    }

    // --- Drag Interaction End (Record Command for Undo/Redo) ---
    if (!io.MouseDown[ImGuiMouseButton_Left] && m_active_axis != GizmoAxis::None) {
        if (command_history && !m_start_root_snapshots.empty()) {
            std::unordered_map<EntityID, TransformComponent> before_map;
            std::unordered_map<EntityID, TransformComponent> after_map;
            bool any_changed = false;

            for (const auto& [id, snapshot] : m_start_root_snapshots) {
                if (em.is_alive(id) && em.has_component<TransformComponent>(id)) {
                    const auto current = em.get_component<TransformComponent>(id);
                    before_map[id] = snapshot.local;
                    after_map[id] = current;
                    if (glm::distance(snapshot.local.position, current.position) > 0.001f ||
                        std::abs(snapshot.local.rotation - current.rotation) > 0.001f ||
                        glm::distance(snapshot.local.scale, current.scale) > 0.001f) {
                        any_changed = true;
                    }
                }
            }

            if (any_changed) {
                const char* operation = m_tool == TransformTool::Move ? "Move" : (m_tool == TransformTool::Rotate ? "Rotate" : "Scale");
                if (m_tool == TransformTool::Scale) {
                    for (const auto& [id, _] : m_start_root_snapshots) {
                        PhysicsSystem::rebuild_body_hierarchy(em, id);
                    }
                }
                command_history->push_executed_command(
                    std::make_unique<TransformEntitiesCommand>(&em, std::move(before_map), std::move(after_map), std::string(operation) + " Entities")
                );
            }
        }

        m_active_axis = GizmoAxis::None;
        m_start_root_snapshots.clear();
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
        if (snap && m_tool == TransformTool::Rotate && m_snap.rotation_step > 0.0f) {
            angle_delta = std::round(angle_delta / m_snap.rotation_step) * m_snap.rotation_step;
        }
        float scale_factor = std::max(0.01f, mouse_radius / m_drag_start_distance);
        if (snap && m_tool == TransformTool::Scale && m_snap.scale_step > 0.0f) {
            scale_factor = std::max(0.01f, 1.0f + std::round((scale_factor - 1.0f) / m_snap.scale_step) * m_snap.scale_step);
        }

        glm::vec2 scale_vec(1.0f, 1.0f);
        if (m_tool == TransformTool::Scale) {
            if (m_active_axis == GizmoAxis::AxisX) scale_vec = glm::vec2(scale_factor, 1.0f);
            else if (m_active_axis == GizmoAxis::AxisY) scale_vec = glm::vec2(1.0f, scale_factor);
            else scale_vec = glm::vec2(scale_factor, scale_factor);
        }

        std::unordered_map<EntityID, TransformComponent> new_locals;
        bool conversion_failed = false;

        for (const auto& [id, snapshot] : m_start_root_snapshots) {
            if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;

            TransformComponent target_world;

            if (m_tool == TransformTool::Move) {
                glm::vec2 constrained_delta = world_delta;
                if (m_active_axis == GizmoAxis::AxisX) constrained_delta = m_drag_axis_x * glm::dot(world_delta, m_drag_axis_x);
                else if (m_active_axis == GizmoAxis::AxisY) constrained_delta = m_drag_axis_y * glm::dot(world_delta, m_drag_axis_y);
                target_world = ViewportTransformMath::calculate_move_target(snapshot.world, constrained_delta);
            } else if (m_tool == TransformTool::Rotate) {
                target_world = ViewportTransformMath::calculate_rotate_target(snapshot.world, m_drag_centroid, angle_delta);
            } else { // Scale
                target_world = ViewportTransformMath::calculate_scale_target(snapshot.world, m_drag_centroid, scale_vec);
            }

            const TransformComponent* parent_world = (snapshot.parent_id != 0 && em.has_component<TransformComponent>(snapshot.parent_id))
                ? &em.get_component<TransformComponent>(snapshot.parent_id)
                : nullptr;

            auto local_opt = TransformSystem::world_to_local_transform(target_world, parent_world);
            if (!local_opt) {
                conversion_failed = true;
                break;
            }
            new_locals[id] = *local_opt;
        }

        if (conversion_failed) {
            for (const auto& [id, snapshot] : m_start_root_snapshots) {
                if (em.is_alive(id) && em.has_component<TransformComponent>(id)) {
                    em.get_component<TransformComponent>(id) = snapshot.local;
                }
            }
            TransformSystem::update(em);
            for (const auto& [id, snapshot] : m_start_root_snapshots) {
                (void)snapshot;
                if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;
                PhysicsSystem::sync_body_hierarchy(em, id);
            }
            m_active_axis = GizmoAxis::None;
            m_hovered_axis = GizmoAxis::None;
            m_start_root_snapshots.clear();
        } else {
            for (const auto& [id, new_local] : new_locals) {
                em.get_component<TransformComponent>(id) = new_local;
            }
            TransformSystem::update(em);
            for (const auto& [id, _] : new_locals) {
                PhysicsSystem::sync_body_hierarchy(em, id);
            }
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
