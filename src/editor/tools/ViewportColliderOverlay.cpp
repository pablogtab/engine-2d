#include "ViewportColliderOverlay.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../physics/ColliderGeometry.hpp"
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

void ViewportColliderOverlay::render(
    EntityManager& entity_manager,
    const EditorSelection* selection,
    const Camera2D& camera,
    const ImVec2& image_min,
    const ImVec2& image_size,
    const ImVec2& uv_min,
    const ImVec2& uv_max
) const {
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

    entity_manager.for_each<TransformComponent, ColliderComponent>(
        [&](EntityID id, const TransformComponent& transform, const ColliderComponent& collider) {
            if (!transform.world_visible) return;
            const bool selected = selection && selection->is_selected(id);
            if (!m_show_all && !selected) return;

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
