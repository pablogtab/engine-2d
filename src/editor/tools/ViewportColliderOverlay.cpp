#include "ViewportColliderOverlay.hpp"
#include "EditorEntityVisibilityFilter.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../physics/ColliderGeometry.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include <array>
#include <cmath>
#include <vector>

namespace Engine {

namespace {

ImU32 collider_color(const ColliderShape& shape, bool selected) {
    if (shape.is_trigger) {
        return selected ? IM_COL32(69, 220, 235, 255) : IM_COL32(69, 220, 235, 145);
    }
    if (shape.is_one_way) {
        return selected ? IM_COL32(244, 183, 64, 255) : IM_COL32(244, 183, 64, 145);
    }
    return selected ? IM_COL32(91, 219, 132, 255) : IM_COL32(91, 219, 132, 135);
}

} // namespace

void ViewportColliderOverlay::cycle_mode() {
    switch (m_mode) {
        case PhysicsOverlayMode::Selected: m_mode = PhysicsOverlayMode::All; break;
        case PhysicsOverlayMode::All: m_mode = PhysicsOverlayMode::None; break;
        case PhysicsOverlayMode::None: m_mode = PhysicsOverlayMode::Selected; break;
    }
}

PhysicsDebugOptions ViewportColliderOverlay::effective_debug_options() const {
    if (m_mode == PhysicsOverlayMode::None) return {};
    PhysicsDebugOptions options = m_debug_options;
    options.selection_only = m_mode == PhysicsOverlayMode::Selected;
    return options;
}

void ViewportColliderOverlay::render(
    EntityManager& entity_manager,
    const EditorSelection* selection,
    const Camera2D& camera,
    const ImVec2& image_min,
    const ImVec2& image_size,
    const ImVec2& uv_min,
    const ImVec2& uv_max,
    const EditorEntityVisibilityFilter* visibility_filter
) const {
    if (m_mode == PhysicsOverlayMode::None) return;
    if (image_size.x <= 0.0f || image_size.y <= 0.0f) return;
    const float uv_width = uv_max.x - uv_min.x;
    const float uv_height = uv_max.y - uv_min.y;
    if (std::abs(uv_width) < 0.0001f || std::abs(uv_height) < 0.0001f) return;

    const glm::vec2 viewport_size = camera.get_viewport_size();
    const auto to_screen = [&](const glm::vec2& world) {
        const glm::vec2 viewport = camera.world_to_screen(glm::vec3(world, 0.0f));
        const float u = viewport.x / viewport_size.x;
        const float v = viewport.y / viewport_size.y;
        return ImVec2(
            image_min.x + (u - uv_min.x) * image_size.x / uv_width,
            image_min.y + (v - uv_min.y) * image_size.y / uv_height
        );
    };

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(
        image_min,
        ImVec2(image_min.x + image_size.x, image_min.y + image_size.y),
        true
    );

    // Runtime data is deliberately copied by PhysicsSystem.  This path never
    // reaches into Box2D and retains the last completed frame while paused.
    const PhysicsDebugOptions options = effective_debug_options();
    const PhysicsDebugFrame& debug = PhysicsSystem::debug_frame();
    if (options.enabled() && debug.revision != 0) {
        const auto selected = [selection](EntityID entity) { return selection && selection->is_selected(entity); };
        for (const PhysicsDebugCollider& collider : debug.colliders) {
            if (options.selection_only && !selected(collider.entity)) continue;
            const ImU32 color = collider.trigger ? IM_COL32(69, 220, 235, 220)
                : collider.one_way ? IM_COL32(244, 183, 64, 220) : IM_COL32(91, 219, 132, 220);
            if (options.colliders) {
                if (collider.type == PhysicsDebugShapeType::Circle && !collider.points.empty()) {
                    const ImVec2 center = to_screen(collider.points.front());
                    const ImVec2 edge = to_screen(collider.points.front() + glm::vec2(collider.radius, 0.0f));
                    draw_list->AddCircle(center, std::abs(edge.x - center.x), color, 48, selected(collider.entity) ? 2.0f : 1.25f);
                } else if (collider.points.size() >= 2) {
                    std::vector<ImVec2> points; points.reserve(collider.points.size());
                    for (const glm::vec2& point : collider.points) points.push_back(to_screen(point));
                    draw_list->AddPolyline(points.data(), static_cast<int>(points.size()), color, ImDrawFlags_Closed, 1.25f);
                }
            }
            if (options.layers && !collider.points.empty()) {
                const ImVec2 label = to_screen(collider.points.front());
                draw_list->AddText(label, color, ("C:" + std::to_string(collider.category) + " M:" + std::to_string(collider.mask)).c_str());
            }
        }
        for (const PhysicsDebugBody& body : debug.bodies) {
            if (options.selection_only && !selected(body.entity)) continue;
            const ImVec2 origin = to_screen(body.position);
            if (options.velocities) draw_list->AddLine(origin, to_screen(body.position + body.velocity * 0.15f), IM_COL32(110, 180, 255, 230), 2.0f);
            if (options.centers_of_mass) draw_list->AddCircleFilled(to_screen(body.center_of_mass), 3.0f, IM_COL32(255, 220, 86, 240));
        }
        for (const PhysicsDebugContact& contact : debug.contacts) {
            if (options.selection_only && !selected(contact.entity_a) && !selected(contact.entity_b)) continue;
            const ImVec2 point = to_screen(contact.point);
            if (options.contacts) draw_list->AddCircleFilled(point, 4.0f, contact.trigger ? IM_COL32(69,220,235,240) : IM_COL32(255,100,100,240));
            if (options.normals) draw_list->AddLine(point, to_screen(contact.point + contact.normal * 28.0f), IM_COL32(255,255,255,240), 2.0f);
        }
    } else entity_manager.for_each<TransformComponent, ColliderComponent>(
        [&](EntityID id, const TransformComponent& transform, const ColliderComponent& collider) {
            if (visibility_filter && !visibility_filter->should_render(id)) return;
            if (!transform.world_visible) return;
            const bool selected = selection && selection->is_selected(id);
            if (m_mode == PhysicsOverlayMode::Selected && !selected) return;

            for (const ColliderShape& shape : collider.shapes) {
                const ImU32 color = collider_color(shape, selected);
                const float thickness = selected ? 2.0f : 1.25f;

                if (shape.type == ColliderShapeType::Circle) {
                    const glm::vec2 center = collider_local_to_world(
                        collider_scaled_offset(shape, transform), transform
                    );
                    const float radius = collider_scaled_circle_radius(shape, transform);
                    const ImVec2 screen_center = to_screen(center);
                    const ImVec2 screen_edge = to_screen(center + glm::vec2(radius, 0.0f));
                    draw_list->AddCircle(
                        screen_center,
                        std::abs(screen_edge.x - screen_center.x),
                        color,
                        48,
                        thickness
                    );
                    continue;
                }

                std::vector<glm::vec2> local_points;
                if (shape.type == ColliderShapeType::Box) {
                    const glm::vec2 center = collider_scaled_offset(shape, transform);
                    const glm::vec2 half = collider_scaled_box_half_extent(shape, transform);
                    local_points = {
                        center + glm::vec2(-half.x, half.y),
                        center + glm::vec2(half.x, half.y),
                        center + glm::vec2(half.x, -half.y),
                        center + glm::vec2(-half.x, -half.y)
                    };
                } else {
                    local_points = collider_scaled_polygon_vertices(shape, transform);
                }
                if (local_points.size() < 2) continue;

                std::vector<ImVec2> screen_points;
                screen_points.reserve(local_points.size());
                for (const glm::vec2& point : local_points) {
                    screen_points.push_back(to_screen(collider_local_to_world(point, transform)));
                }
                draw_list->AddPolyline(
                    screen_points.data(),
                    static_cast<int>(screen_points.size()),
                    color,
                    ImDrawFlags_Closed,
                    thickness
                );
            }
        }
    );

    draw_list->PopClipRect();
}

} // namespace Engine
