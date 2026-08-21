#include "ViewportSelectionTool.hpp"
#include "ViewportSelectionGeometry.hpp"
#include "EditorEntityVisibilityFilter.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../ecs/components/UIComponents.hpp"
#include "../../ui/CanvasLayout.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Engine {

namespace {

ImVec2 world_to_viewport_screen(const Camera2D& camera, const glm::vec2& world, const ImVec2& image_min, float scale_x, float scale_y) {
    const glm::vec2 screen = camera.world_to_screen(glm::vec3(world, 0.0f));
    return ImVec2(image_min.x + screen.x * scale_x, image_min.y + screen.y * scale_y);
}

} // namespace

void ViewportSelectionTool::render_selection_outlines(
    EntityManager& em,
    const EditorSelection& selection,
    const Camera2D& camera,
    const ImVec2& img_min,
    const ImVec2& img_size,
    const EditorEntityVisibilityFilter* visibility_filter
) {
    if (selection.empty()) return;

    const float scale_x = img_size.x / camera.get_viewport_size().x;
    const float scale_y = img_size.y / camera.get_viewport_size().y;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (EntityID id : selection.get_selected()) {
        if (visibility_filter && !visibility_filter->should_render(id)) continue;
        if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) continue;
        const auto& transform = em.get_component<TransformComponent>(id);
        if (!transform.world_visible) continue;

        ViewportSelectionQuad quad;
        if (!get_selection_quad(em, id, quad)) continue;
        ImVec2 screen_corners[4];
        for (size_t index = 0; index < quad.corners.size(); ++index) {
            screen_corners[index] = world_to_viewport_screen(camera, quad.corners[index], img_min, scale_x, scale_y);
        }
        draw_list->AddQuadFilled(screen_corners[0], screen_corners[1], screen_corners[2], screen_corners[3], IM_COL32(99, 102, 241, 30));
        draw_list->AddPolyline(screen_corners, 4, IM_COL32(129, 140, 248, 220), ImDrawFlags_Closed, 1.2f);
    }
}

EntityID ViewportSelectionTool::pick_entity(
    EntityManager& em,
    const glm::vec2& world_position,
    const Camera2D* view_camera,
    const EditorEntityVisibilityFilter* visibility_filter
) const {
    EntityID hit_entity = 0;
    float closest_distance = std::numeric_limits<float>::max();

    em.for_each<TransformComponent>([&](EntityID id, const TransformComponent& transform) {
        if (visibility_filter && !visibility_filter->should_interact(id)) return;
        if (!transform.world_visible) return;
        ViewportSelectionQuad quad;
        if (!get_selection_quad(em, id, quad) || !point_in_selection_quad(world_position, quad)) return;
        const float distance = glm::distance(world_position, glm::vec2(transform.world_position));
        if (distance < closest_distance) {
            closest_distance = distance;
            hit_entity = id;
        }
    });

    // Camera gizmos are constant-size editor icons rather than scene geometry.
    // Test them in world units derived from the active editor zoom.
    const float camera_pick_radius = view_camera
        ? 14.0f / std::max(view_camera->get_effective_zoom(), 0.001f)
        : 14.0f;
    em.for_each<CameraComponent, TransformComponent>([&](EntityID id, const CameraComponent&, const TransformComponent& transform) {
        if (visibility_filter && !visibility_filter->should_interact(id)) return;
        if (!transform.world_visible) return;
        const float distance = glm::distance(world_position, glm::vec2(transform.world_position));
        if (distance <= camera_pick_radius && distance < closest_distance) {
            closest_distance = distance;
            hit_entity = id;
        }
    });

    if (hit_entity == 0) {
        const float edge_tolerance = view_camera
            ? 5.0f / std::max(view_camera->get_effective_zoom(), 0.001f)
            : 5.0f;
        em.for_each<CameraComponent, TransformComponent>([&](EntityID id, const CameraComponent& camera, const TransformComponent& transform) {
            if (visibility_filter && !visibility_filter->should_interact(id)) return;
            const glm::vec2 relative = world_position - glm::vec2(transform.world_position);
            const float radians = glm::radians(-transform.world_rotation);
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            const glm::vec2 local(
                relative.x * cosine - relative.y * sine,
                relative.x * sine + relative.y * cosine
            );
            const glm::vec2 half = m_camera_output_size * 0.5f / std::max(camera.zoom, 0.001f);
            const bool near_vertical = std::abs(std::abs(local.x) - half.x) <= edge_tolerance && std::abs(local.y) <= half.y + edge_tolerance;
            const bool near_horizontal = std::abs(std::abs(local.y) - half.y) <= edge_tolerance && std::abs(local.x) <= half.x + edge_tolerance;
            if (near_vertical || near_horizontal) hit_entity = id;
        });
    }

    // Canvas UI is composited above world geometry, so visible UI rectangles
    // take selection priority at the same pointer location.
    if (view_camera) {
        const glm::vec2 screen_position = view_camera->world_to_screen(
            glm::vec3(world_position, 0.0f)
        );
        std::vector<EntityID> canvases;
        em.for_each<CanvasComponent>([&](EntityID id, const CanvasComponent& canvas) {
            if (!canvas.enabled) return;
            if (visibility_filter && !visibility_filter->should_interact(id)) return;
            if (em.has_component<TransformComponent>(id) &&
                !em.get_component<TransformComponent>(id).world_visible) return;
            canvases.push_back(id);
        });
        std::sort(canvases.begin(), canvases.end(), [&](EntityID lhs, EntityID rhs) {
            const auto& left = em.get_component<CanvasComponent>(lhs);
            const auto& right = em.get_component<CanvasComponent>(rhs);
            if (left.sorting_order != right.sorting_order) {
                return left.sorting_order < right.sorting_order;
            }
            return lhs < rhs;
        });

        for (const EntityID canvas_entity : canvases) {
            glm::vec2 canvas_position{0.0f};
            if (em.has_component<TransformComponent>(canvas_entity)) {
                canvas_position = glm::vec2(
                    em.get_component<TransformComponent>(canvas_entity).world_position
                );
            }
            const CanvasViewport viewport = calculate_canvas_editor_viewport(
                em.get_component<CanvasComponent>(canvas_entity),
                canvas_position,
                *view_camera
            );
            for (const ResolvedUILayout& resolved : resolve_canvas_hierarchy(
                    em, canvas_entity, viewport, &AssetDatabase::get()
                )) {
                const EntityID candidate = resolved.entity;
                if (visibility_filter && !visibility_filter->should_interact(candidate)) continue;
                if (em.has_component<TransformComponent>(candidate) &&
                    !em.get_component<TransformComponent>(candidate).world_visible) continue;
                const bool has_visual =
                    (em.has_component<UITextComponent>(candidate) &&
                     em.get_component<UITextComponent>(candidate).enabled) ||
                    (em.has_component<UIImageComponent>(candidate) &&
                     em.get_component<UIImageComponent>(candidate).enabled) ||
                    (em.has_component<UIButtonComponent>(candidate) &&
                     em.get_component<UIButtonComponent>(candidate).enabled);
                if (!has_visual) continue;
                const glm::vec2 minimum = resolved.screen_rect.min();
                const glm::vec2 maximum = resolved.screen_rect.max();
                if (screen_position.x >= minimum.x && screen_position.x <= maximum.x &&
                    screen_position.y >= minimum.y && screen_position.y <= maximum.y) {
                    hit_entity = candidate;
                }
            }
        }
    }

    if (hit_entity != 0 && em.has_component<HierarchyComponent>(hit_entity)) {
        const EntityID parent = em.get_component<HierarchyComponent>(hit_entity).parent;
        if (parent != 0 && em.is_alive(parent) && em.has_component<TilemapComponent>(parent)) hit_entity = parent;
    }

    if (hit_entity != 0 && visibility_filter && !visibility_filter->should_interact(hit_entity)) {
        hit_entity = 0;
    }
    return hit_entity;
}

void ViewportSelectionTool::handle_selection_input(
    Scene* active_scene,
    const Camera2D& camera,
    EditorSelection& selection,
    const ImVec2& img_min,
    const ImVec2& img_size,
    bool is_viewport_hovered,
    bool is_gizmo_active,
    const EditorEntityVisibilityFilter* visibility_filter
) {
    if (!active_scene || !is_viewport_hovered) return;

    EntityManager& em = active_scene->get_entity_manager();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse_pos = io.MousePos;
    const float scale_x = img_size.x / camera.get_viewport_size().x;
    const float scale_y = img_size.y / camera.get_viewport_size().y;
    const bool is_additive = io.KeyCtrl || io.KeyShift;
    const bool is_alt_strict = io.KeyAlt;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !is_gizmo_active) {
        m_is_box_selecting = true;
        m_box_select_start = mouse_pos;
    }

    if (m_is_box_selecting && io.MouseDown[ImGuiMouseButton_Left]) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 min_point(std::min(m_box_select_start.x, mouse_pos.x), std::min(m_box_select_start.y, mouse_pos.y));
        const ImVec2 max_point(std::max(m_box_select_start.x, mouse_pos.x), std::max(m_box_select_start.y, mouse_pos.y));
        draw_list->AddRectFilled(min_point, max_point, IM_COL32(99, 102, 241, 40), 2.0f);
        draw_list->AddRect(min_point, max_point, IM_COL32(129, 140, 248, 220), 2.0f, 0, 1.5f);
    }

    if (m_is_box_selecting && !io.MouseDown[ImGuiMouseButton_Left]) {
        m_is_box_selecting = false;
        const float drag_distance = std::hypot(mouse_pos.x - m_box_select_start.x, mouse_pos.y - m_box_select_start.y);
        if (drag_distance > 6.0f) {
            const glm::vec2 start_viewport((m_box_select_start.x - img_min.x) / scale_x, (m_box_select_start.y - img_min.y) / scale_y);
            const glm::vec2 end_viewport((mouse_pos.x - img_min.x) / scale_x, (mouse_pos.y - img_min.y) / scale_y);
            const glm::vec2 start_world = camera.screen_to_world(start_viewport);
            const glm::vec2 end_world = camera.screen_to_world(end_viewport);
            const glm::vec2 rect_min(glm::min(start_world, end_world));
            const glm::vec2 rect_max(glm::max(start_world, end_world));
            std::unordered_set<EntityID> found_entities;

            em.for_each<TransformComponent>([&](EntityID id, const TransformComponent& transform) {
                if (visibility_filter && !visibility_filter->should_interact(id)) return;
                if (!transform.world_visible) return;
                ViewportSelectionQuad quad;
                if (!get_selection_quad(em, id, quad)) {
                    if (em.has_component<CameraComponent>(id) &&
                        point_in_world_rect(glm::vec2(transform.world_position), rect_min, rect_max)) {
                        found_entities.insert(id);
                    }
                    return;
                }
                const bool matched = is_alt_strict
                    ? std::all_of(quad.corners.begin(), quad.corners.end(), [&](const glm::vec2& corner) { return point_in_world_rect(corner, rect_min, rect_max); })
                    : selection_quad_intersects_world_rect(quad, rect_min, rect_max);
                if (!matched) return;

                EntityID target = id;
                if (em.has_component<HierarchyComponent>(id)) {
                    const EntityID parent = em.get_component<HierarchyComponent>(id).parent;
                    if (parent != 0 && em.is_alive(parent) && em.has_component<TilemapComponent>(parent)) target = parent;
                }
                if (visibility_filter && !visibility_filter->should_interact(target)) return;
                found_entities.insert(target);
            });
            if (is_additive) selection.add_selection(found_entities);
            else selection.set_selection(found_entities);
        } else {
            const glm::vec2 mouse_viewport((mouse_pos.x - img_min.x) / scale_x, (mouse_pos.y - img_min.y) / scale_y);
            const glm::vec2 mouse_world = camera.screen_to_world(mouse_viewport);
            const EntityID hit_entity = pick_entity(em, mouse_world, &camera, visibility_filter);
            selection.select(hit_entity, is_additive);
        }
    }

    // A transform gesture owns Escape so it can restore its initial snapshot.
    // Only an idle Viewport uses Escape to clear the current selection.
    if (!is_gizmo_active && ImGui::IsKeyPressed(ImGuiKey_Escape)) selection.clear();
}

} // namespace Engine
