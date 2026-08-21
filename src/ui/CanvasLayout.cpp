#include "CanvasLayout.hpp"

#include "UITextLayout.hpp"

#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../scene/Camera2D.hpp"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace Engine {
namespace {

glm::vec2 positive_size(const glm::vec2& value) {
    return glm::max(value, glm::vec2(1.0f));
}

SafeAreaInsets clamp_safe_area(const SafeAreaInsets& value, const glm::vec2& output_size) {
    SafeAreaInsets result;
    result.left = std::clamp(value.left, 0.0f, output_size.x);
    result.right = std::clamp(value.right, 0.0f, output_size.x - result.left);
    result.top = std::clamp(value.top, 0.0f, output_size.y);
    result.bottom = std::clamp(value.bottom, 0.0f, output_size.y - result.top);
    return result;
}

} // namespace

CanvasViewport calculate_canvas_viewport(
    const CanvasComponent& canvas,
    const glm::vec2& output_size,
    const SafeAreaInsets& safe_area
) {
    const glm::vec2 output = positive_size(output_size);
    const glm::vec2 reference = positive_size(canvas.reference_size);
    const SafeAreaInsets insets = canvas.use_safe_area
        ? clamp_safe_area(safe_area, output)
        : SafeAreaInsets{};
    const UIRect safe_rect{
        {insets.left, insets.top},
        positive_size({
            output.x - insets.left - insets.right,
            output.y - insets.top - insets.bottom
        })
    };

    CanvasViewport result;
    result.safe_rect = safe_rect;
    result.logical_rect = {{0.0f, 0.0f}, reference};
    const glm::vec2 ratios = safe_rect.size / reference;

    switch (canvas.scaling_policy) {
        case PresentationMode::Fit: {
            const float scale = std::min(ratios.x, ratios.y);
            result.scale = glm::vec2(scale);
            result.physical_rect.size = reference * scale;
            result.physical_rect.position = safe_rect.position +
                (safe_rect.size - result.physical_rect.size) * 0.5f;
            break;
        }
        case PresentationMode::Fill: {
            const float scale = std::max(ratios.x, ratios.y);
            result.scale = glm::vec2(scale);
            result.physical_rect.size = reference * scale;
            result.physical_rect.position = safe_rect.position +
                (safe_rect.size - result.physical_rect.size) * 0.5f;
            break;
        }
        case PresentationMode::Stretch:
            result.scale = ratios;
            result.physical_rect = safe_rect;
            break;
        case PresentationMode::Responsive: {
            const float scale = std::min(ratios.x, ratios.y);
            result.scale = glm::vec2(scale);
            result.logical_rect.size = safe_rect.size / scale;
            result.physical_rect = safe_rect;
            break;
        }
    }
    return result;
}

CanvasViewport calculate_canvas_editor_viewport(
    const CanvasComponent& canvas,
    const glm::vec2& canvas_world_position,
    const Camera2D& editor_camera
) {
    const glm::vec2 reference = positive_size(canvas.reference_size);
    const glm::vec2 world_top_left = canvas_world_position +
        glm::vec2(-reference.x * 0.5f, reference.y * 0.5f);
    const float scale = editor_camera.get_effective_zoom();

    CanvasViewport result;
    result.logical_rect = {{0.0f, 0.0f}, reference};
    result.physical_rect = {
        editor_camera.world_to_screen(glm::vec3(world_top_left, 0.0f)),
        reference * scale
    };
    result.safe_rect = result.physical_rect;
    result.scale = glm::vec2(scale);
    return result;
}

UIRect resolve_ui_rect(const UILayoutComponent& layout, const UIRect& parent_rect) {
    const glm::vec2 anchor_min = parent_rect.position + parent_rect.size * layout.anchor_min;
    const glm::vec2 anchor_max = parent_rect.position + parent_rect.size * layout.anchor_max;
    const glm::vec2 size = glm::max(anchor_max - anchor_min + layout.size_delta, glm::vec2(0.0f));
    const glm::vec2 anchor_pivot = glm::mix(anchor_min, anchor_max, layout.pivot);
    const glm::vec2 pivot_position = anchor_pivot + layout.anchored_position;
    return {pivot_position - size * layout.pivot, size};
}

glm::vec2 canvas_to_screen(
    const glm::vec2& canvas_position,
    const CanvasViewport& viewport
) {
    return viewport.physical_rect.position +
        (canvas_position - viewport.logical_rect.position) * viewport.scale;
}

glm::vec2 screen_to_canvas(
    const glm::vec2& screen_position,
    const CanvasViewport& viewport
) {
    const glm::vec2 safe_scale = glm::max(glm::abs(viewport.scale), glm::vec2(0.0001f));
    return viewport.logical_rect.position +
        (screen_position - viewport.physical_rect.position) / safe_scale;
}

UIRect canvas_to_screen(const UIRect& canvas_rect, const CanvasViewport& viewport) {
    return {
        canvas_to_screen(canvas_rect.position, viewport),
        canvas_rect.size * viewport.scale
    };
}

std::vector<ResolvedUILayout> resolve_canvas_hierarchy(
    const EntityManager& entities,
    EntityID canvas_entity,
    const CanvasViewport& viewport,
    const AssetDatabase* asset_database
) {
    std::vector<ResolvedUILayout> result;
    if (!entities.is_alive(canvas_entity) ||
        !entities.has_component<CanvasComponent>(canvas_entity)) return result;

    std::unordered_set<EntityID> visited;
    visited.insert(canvas_entity);
    const std::function<void(EntityID, const UIRect&)> visit =
        [&](EntityID parent, const UIRect& parent_rect) {
            if (!entities.has_component<HierarchyComponent>(parent)) return;
            const auto& hierarchy = entities.get_component<HierarchyComponent>(parent);
            for (const EntityID child : hierarchy.children) {
                if (!entities.is_alive(child) || !visited.insert(child).second) continue;
                if (entities.has_component<CanvasComponent>(child)) continue;

                UIRect child_rect = parent_rect;
                if (entities.has_component<UILayoutComponent>(child)) {
                    const auto& layout = entities.get_component<UILayoutComponent>(child);
                    child_rect = resolve_ui_rect(layout, parent_rect);
                    if (entities.has_component<UITextComponent>(child)) {
                        const auto& text = entities.get_component<UITextComponent>(child);
                        const glm::vec2 fitted_size = calculate_ui_text_fitted_size(
                            text, child_rect.size, asset_database
                        );
                        const glm::vec2 pivot_position = child_rect.position +
                            child_rect.size * layout.pivot;
                        child_rect.size = fitted_size;
                        child_rect.position = pivot_position - fitted_size * layout.pivot;
                    }
                    result.push_back({
                        child,
                        parent_rect,
                        child_rect,
                        canvas_to_screen(child_rect, viewport)
                    });
                }
                visit(child, child_rect);
            }
        };
    visit(canvas_entity, viewport.logical_rect);
    return result;
}

} // namespace Engine
