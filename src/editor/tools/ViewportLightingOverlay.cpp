#include "ViewportLightingOverlay.hpp"

#include "EditorEntityVisibilityFilter.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../physics/ColliderGeometry.hpp"
#include <cmath>
#include <vector>

namespace Engine {

void ViewportLightingOverlay::render(
    EntityManager& entities,
    const EditorSelection* selection,
    const Camera2D& camera,
    const ImVec2& image_min,
    const ImVec2& image_size,
    const ImVec2& uv_min,
    const ImVec2& uv_max,
    const EditorEntityVisibilityFilter* visibility_filter
) const {
    if (!selection || image_size.x <= 0.0f || image_size.y <= 0.0f) return;
    const float uv_width = uv_max.x - uv_min.x;
    const float uv_height = uv_max.y - uv_min.y;
    if (std::abs(uv_width) < 0.0001f || std::abs(uv_height) < 0.0001f) return;

    const glm::vec2 viewport_size = camera.get_viewport_size();
    const auto to_screen = [&](const glm::vec2& world) {
        const glm::vec2 viewport = camera.world_to_screen(glm::vec3(world, 0.0f));
        return ImVec2(
            image_min.x + (viewport.x / viewport_size.x - uv_min.x) * image_size.x / uv_width,
            image_min.y + (viewport.y / viewport_size.y - uv_min.y) * image_size.y / uv_height
        );
    };

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(image_min, {image_min.x + image_size.x, image_min.y + image_size.y}, true);

    entities.for_each<TransformComponent, Light2DComponent>(
        [&](EntityID entity, const TransformComponent& transform, const Light2DComponent& light) {
            if (!selection->is_selected(entity) || !transform.world_visible ||
                (visibility_filter && !visibility_filter->should_render(entity))) return;
            const ImVec2 center = to_screen(glm::vec2(transform.world_position));
            const ImU32 color = IM_COL32(light.color.r, light.color.g, light.color.b, 230);
            if (light.type == Light2DType::Point) {
                const ImVec2 edge = to_screen(glm::vec2(transform.world_position) + glm::vec2(light.radius, 0.0f));
                const float radius = std::hypot(edge.x - center.x, edge.y - center.y);
                draw_list->AddCircle(center, radius, color, 64, 2.0f);
                draw_list->AddLine({center.x - 7.0f, center.y}, {center.x + 7.0f, center.y}, color, 2.0f);
                draw_list->AddLine({center.x, center.y - 7.0f}, {center.x, center.y + 7.0f}, color, 2.0f);
            } else {
                draw_list->AddCircleFilled(center, 6.0f, color, 16);
                for (int ray = 0; ray < 8; ++ray) {
                    const float angle = static_cast<float>(ray) * 0.785398163f;
                    draw_list->AddLine(
                        {center.x + std::cos(angle) * 9.0f, center.y + std::sin(angle) * 9.0f},
                        {center.x + std::cos(angle) * 15.0f, center.y + std::sin(angle) * 15.0f},
                        color, 1.5f
                    );
                }
            }
        }
    );

    entities.for_each<TransformComponent, ShadowCaster2DComponent>(
        [&](EntityID entity, const TransformComponent& transform, const ShadowCaster2DComponent& caster) {
            if (!selection->is_selected(entity) || !transform.world_visible ||
                (visibility_filter && !visibility_filter->should_render(entity))) return;
            if (caster.source != ShadowGeometrySource::StaticPolygon || caster.vertices.size() < 2) return;
            std::vector<ImVec2> points;
            points.reserve(caster.vertices.size());
            for (const glm::vec2& vertex : caster.vertices) {
                const glm::vec2 world = collider_local_to_world(vertex * transform.world_scale, transform);
                points.push_back(to_screen(world));
            }
            draw_list->AddPolyline(points.data(), static_cast<int>(points.size()),
                                   IM_COL32(121, 104, 220, 240), ImDrawFlags_Closed, 2.0f);
        }
    );

    draw_list->PopClipRect();
}

} // namespace Engine
