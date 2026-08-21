#include "UILayoutGizmo.hpp"

#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EntityCommands.hpp"
#include "../tools/EditorEntityVisibilityFilter.hpp"
#include "../tools/UILayoutGizmoMath.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ui/CanvasLayout.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <unordered_set>

namespace Engine {
namespace {

struct GizmoLayoutContext {
    CanvasViewport viewport;
    ResolvedUILayout resolved;
    UILayoutComponent layout;
};

std::optional<EntityID> find_canvas_ancestor(EntityManager& entities, EntityID entity) {
    std::unordered_set<EntityID> visited;
    EntityID current = entity;
    while (current != 0 && entities.is_alive(current) && visited.insert(current).second) {
        if (entities.has_component<CanvasComponent>(current)) return current;
        if (!entities.has_component<HierarchyComponent>(current)) break;
        current = entities.get_component<HierarchyComponent>(current).parent;
    }
    return std::nullopt;
}

std::optional<GizmoLayoutContext> resolve_context(
    EntityManager& entities,
    EntityID entity,
    const Camera2D& camera
) {
    if (!entities.is_alive(entity) || !entities.has_component<UILayoutComponent>(entity)) {
        return std::nullopt;
    }
    const std::optional<EntityID> canvas_entity = find_canvas_ancestor(entities, entity);
    if (!canvas_entity || *canvas_entity == entity) return std::nullopt;

    glm::vec2 canvas_position{0.0f};
    if (entities.has_component<TransformComponent>(*canvas_entity)) {
        canvas_position = glm::vec2(
            entities.get_component<TransformComponent>(*canvas_entity).world_position
        );
    }
    const auto& canvas = entities.get_component<CanvasComponent>(*canvas_entity);
    const CanvasViewport viewport = calculate_canvas_editor_viewport(
        canvas, canvas_position, camera
    );
    for (const ResolvedUILayout& resolved : resolve_canvas_hierarchy(
            entities, *canvas_entity, viewport, &AssetDatabase::get()
        )) {
        if (resolved.entity == entity) {
            return GizmoLayoutContext{
                viewport,
                resolved,
                entities.get_component<UILayoutComponent>(entity)
            };
        }
    }
    return std::nullopt;
}

bool point_inside(const ImVec2& point, const ImVec2& minimum, const ImVec2& maximum) {
    return point.x >= minimum.x && point.x <= maximum.x &&
        point.y >= minimum.y && point.y <= maximum.y;
}

void draw_marker(ImDrawList* draw_list, const ImVec2& center, ImU32 color, float radius) {
    draw_list->AddRectFilled(
        {center.x - radius, center.y - radius},
        {center.x + radius, center.y + radius},
        color,
        1.0f
    );
    draw_list->AddRect(
        {center.x - radius, center.y - radius},
        {center.x + radius, center.y + radius},
        IM_COL32(24, 28, 38, 230),
        1.0f
    );
}

} // namespace

bool UILayoutGizmo::draw_gizmo(
    EntityManager& entities,
    const std::unordered_set<EntityID>& selected_entities,
    const Camera2D& camera,
    const ImVec2& viewport_position,
    const ImVec2& viewport_size,
    bool is_viewport_hovered,
    bool snap_enabled,
    float snap_step,
    EditorCommandHistory* command_history,
    const EditorEntityVisibilityFilter* visibility_filter
) {
    m_hovered = false;
    bool cancelled_this_frame = false;

    const EntityID selected = selected_entities.size() == 1
        ? *selected_entities.begin() : 0;
    const bool selected_is_editable = selected != 0 && entities.is_alive(selected) &&
        entities.has_component<UILayoutComponent>(selected) &&
        (!visibility_filter || visibility_filter->should_interact(selected));

    if (m_drag_entity != 0 && (!selected_is_editable || selected != m_drag_entity)) {
        cancel_drag(entities);
        cancelled_this_frame = true;
    }
    if (!selected_is_editable) return cancelled_this_frame;

    std::optional<GizmoLayoutContext> context = resolve_context(entities, selected, camera);
    if (!context) {
        if (m_drag_entity != 0) cancel_drag(entities);
        return cancelled_this_frame;
    }

    const glm::vec2 camera_size = glm::max(camera.get_viewport_size(), glm::vec2(1.0f));
    const glm::vec2 display_scale{
        viewport_size.x / camera_size.x,
        viewport_size.y / camera_size.y
    };
    const auto to_editor_screen = [&](const glm::vec2& screen_position) {
        return ImVec2(
            viewport_position.x + screen_position.x * display_scale.x,
            viewport_position.y + screen_position.y * display_scale.y
        );
    };
    const auto mouse_to_canvas_screen = [&](const ImVec2& mouse) {
        return glm::vec2(
            (mouse.x - viewport_position.x) / std::max(display_scale.x, 0.0001f),
            (mouse.y - viewport_position.y) / std::max(display_scale.y, 0.0001f)
        );
    };

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    ImVec2 rect_min = to_editor_screen(context->resolved.screen_rect.min());
    ImVec2 rect_max = to_editor_screen(context->resolved.screen_rect.max());
    if (rect_min.x > rect_max.x) std::swap(rect_min.x, rect_max.x);
    if (rect_min.y > rect_max.y) std::swap(rect_min.y, rect_max.y);
    const ImVec2 hit_min{rect_min.x - 4.0f, rect_min.y - 4.0f};
    const ImVec2 hit_max{rect_max.x + 4.0f, rect_max.y + 4.0f};
    m_hovered = is_viewport_hovered && point_inside(mouse, hit_min, hit_max);

    if (m_drag_entity == 0 && m_hovered && command_history &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_drag_entity = selected;
        m_initial_layout = context->layout;
        m_drag_start_screen = mouse_to_canvas_screen(mouse);
    }

    if (m_drag_entity != 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancel_drag(entities);
            cancelled_this_frame = true;
        } else if (io.MouseDown[ImGuiMouseButton_Left]) {
            const glm::vec2 current_screen = mouse_to_canvas_screen(mouse);
            auto& layout = entities.get_component<UILayoutComponent>(m_drag_entity);
            layout.anchored_position = calculate_ui_drag_position(
                m_initial_layout.anchored_position,
                current_screen - m_drag_start_screen,
                context->viewport,
                snap_enabled,
                snap_step
            );
        } else {
            const EntityID dragged_entity = m_drag_entity;
            const UILayoutComponent before = m_initial_layout;
            const UILayoutComponent after = entities.get_component<UILayoutComponent>(dragged_entity);
            clear_drag();
            if (command_history && before != after) {
                command_history->push_executed_command(
                    std::make_unique<SetComponentCommand<UILayoutComponent>>(
                        &entities,
                        dragged_entity,
                        before,
                        after,
                        "Move UI Element"
                    )
                );
            }
        }
    }

    context = resolve_context(entities, selected, camera);
    if (!context) return true;

    rect_min = to_editor_screen(context->resolved.screen_rect.min());
    rect_max = to_editor_screen(context->resolved.screen_rect.max());
    if (rect_min.x > rect_max.x) std::swap(rect_min.x, rect_max.x);
    if (rect_min.y > rect_max.y) std::swap(rect_min.y, rect_max.y);

    const UIRect& parent = context->resolved.parent_logical_rect;
    const glm::vec2 anchor_min_canvas = parent.position + parent.size * context->layout.anchor_min;
    const glm::vec2 anchor_max_canvas = parent.position + parent.size * context->layout.anchor_max;
    const glm::vec2 anchor_pivot_canvas = glm::mix(
        anchor_min_canvas, anchor_max_canvas, context->layout.pivot
    );
    const glm::vec2 pivot_canvas = context->resolved.logical_rect.position +
        context->resolved.logical_rect.size * context->layout.pivot;
    const ImVec2 anchor_min = to_editor_screen(canvas_to_screen(
        anchor_min_canvas, context->viewport
    ));
    const ImVec2 anchor_max = to_editor_screen(canvas_to_screen(
        anchor_max_canvas, context->viewport
    ));
    const ImVec2 anchor_pivot = to_editor_screen(canvas_to_screen(
        anchor_pivot_canvas, context->viewport
    ));
    const ImVec2 pivot = to_editor_screen(canvas_to_screen(pivot_canvas, context->viewport));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    constexpr ImU32 anchor_color = IM_COL32(255, 188, 74, 240);
    constexpr ImU32 rect_color = IM_COL32(89, 202, 255, 255);
    if (std::abs(anchor_min.x - anchor_max.x) > 0.5f ||
        std::abs(anchor_min.y - anchor_max.y) > 0.5f) {
        draw_list->AddRect(anchor_min, anchor_max, IM_COL32(255, 188, 74, 155), 0.0f, 0, 1.0f);
        draw_marker(draw_list, anchor_min, anchor_color, 3.5f);
        draw_marker(draw_list, anchor_max, anchor_color, 3.5f);
    } else {
        draw_marker(draw_list, anchor_pivot, anchor_color, 4.0f);
    }
    if (m_hovered || m_drag_entity != 0) {
        draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(89, 202, 255, 24));
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    draw_list->AddRect(rect_min, rect_max, rect_color, 0.0f, 0, 2.0f);
    draw_list->AddCircleFilled(pivot, 4.0f, rect_color, 16);
    draw_list->AddCircle(pivot, 7.0f, IM_COL32(89, 202, 255, 190), 20, 1.0f);
    return cancelled_this_frame || m_drag_entity != 0 || m_hovered;
}

void UILayoutGizmo::cancel_drag(EntityManager& entities) {
    if (m_drag_entity != 0 && entities.is_alive(m_drag_entity) &&
        entities.has_component<UILayoutComponent>(m_drag_entity)) {
        entities.get_component<UILayoutComponent>(m_drag_entity) = m_initial_layout;
    }
    clear_drag();
}

void UILayoutGizmo::clear_drag() {
    m_drag_entity = 0;
    m_drag_start_screen = {0.0f, 0.0f};
}

} // namespace Engine
