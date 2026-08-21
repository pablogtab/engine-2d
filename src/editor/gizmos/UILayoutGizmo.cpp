#include "UILayoutGizmo.hpp"

#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EntityCommands.hpp"
#include "../tools/EditorEntityVisibilityFilter.hpp"
#include "../tools/UILayoutGizmoMath.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ui/CanvasLayout.hpp"

#include <algorithm>
#include <array>
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

bool point_near(const ImVec2& point, const ImVec2& center, float radius) {
    const float x = point.x - center.x;
    const float y = point.y - center.y;
    return x * x + y * y <= radius * radius;
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

void draw_anchor_marker(ImDrawList* draw_list, const ImVec2& center, ImU32 color, float radius) {
    draw_list->AddQuadFilled(
        {center.x, center.y - radius},
        {center.x + radius, center.y},
        {center.x, center.y + radius},
        {center.x - radius, center.y},
        color
    );
    draw_list->AddQuad(
        {center.x, center.y - radius},
        {center.x + radius, center.y},
        {center.x, center.y + radius},
        {center.x - radius, center.y},
        IM_COL32(24, 28, 38, 230),
        1.0f
    );
}

ImGuiMouseCursor resize_cursor(UIResizeHandle handle) {
    switch (handle) {
        case UIResizeHandle::Top:
        case UIResizeHandle::Bottom:
            return ImGuiMouseCursor_ResizeNS;
        case UIResizeHandle::Left:
        case UIResizeHandle::Right:
            return ImGuiMouseCursor_ResizeEW;
        case UIResizeHandle::TopLeft:
        case UIResizeHandle::BottomRight:
            return ImGuiMouseCursor_ResizeNWSE;
        case UIResizeHandle::TopRight:
        case UIResizeHandle::BottomLeft:
            return ImGuiMouseCursor_ResizeNESW;
    }
    return ImGuiMouseCursor_Arrow;
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

    if (m_drag_mode != DragMode::None && (!selected_is_editable || selected != m_drag_entity)) {
        cancel_drag(entities);
        cancelled_this_frame = true;
    }
    if (!selected_is_editable) return cancelled_this_frame;

    std::optional<GizmoLayoutContext> context = resolve_context(entities, selected, camera);
    if (!context) {
        if (m_drag_mode != DragMode::None) cancel_drag(entities);
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
    const auto mouse_to_canvas = [&](const ImVec2& mouse) {
        const glm::vec2 camera_screen{
            (mouse.x - viewport_position.x) / std::max(display_scale.x, 0.0001f),
            (mouse.y - viewport_position.y) / std::max(display_scale.y, 0.0001f)
        };
        return screen_to_canvas(camera_screen, context->viewport);
    };

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    ImVec2 rect_min = to_editor_screen(context->resolved.screen_rect.min());
    ImVec2 rect_max = to_editor_screen(context->resolved.screen_rect.max());
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
    const ImVec2 anchor_min = to_editor_screen(canvas_to_screen(anchor_min_canvas, context->viewport));
    const ImVec2 anchor_max = to_editor_screen(canvas_to_screen(anchor_max_canvas, context->viewport));
    const ImVec2 anchor_pivot = to_editor_screen(canvas_to_screen(anchor_pivot_canvas, context->viewport));
    const ImVec2 pivot = to_editor_screen(canvas_to_screen(pivot_canvas, context->viewport));

    struct ResizePoint {
        UIResizeHandle handle;
        ImVec2 position;
    };
    const float center_x = (rect_min.x + rect_max.x) * 0.5f;
    const float center_y = (rect_min.y + rect_max.y) * 0.5f;
    const std::array<ResizePoint, 8> resize_points{{
        {UIResizeHandle::TopLeft, rect_min},
        {UIResizeHandle::Top, {center_x, rect_min.y}},
        {UIResizeHandle::TopRight, {rect_max.x, rect_min.y}},
        {UIResizeHandle::Right, {rect_max.x, center_y}},
        {UIResizeHandle::BottomRight, rect_max},
        {UIResizeHandle::Bottom, {center_x, rect_max.y}},
        {UIResizeHandle::BottomLeft, {rect_min.x, rect_max.y}},
        {UIResizeHandle::Left, {rect_min.x, center_y}}
    }};
    const bool fit_width = entities.has_component<UITextComponent>(selected) &&
        entities.get_component<UITextComponent>(selected).fit_content_width;
    const bool fit_height = entities.has_component<UITextComponent>(selected) &&
        entities.get_component<UITextComponent>(selected).fit_content_height;
    const auto handle_available = [&](UIResizeHandle handle) {
        const bool horizontal = handle == UIResizeHandle::TopLeft ||
            handle == UIResizeHandle::TopRight || handle == UIResizeHandle::Right ||
            handle == UIResizeHandle::BottomRight || handle == UIResizeHandle::BottomLeft ||
            handle == UIResizeHandle::Left;
        const bool vertical = handle == UIResizeHandle::TopLeft ||
            handle == UIResizeHandle::Top || handle == UIResizeHandle::TopRight ||
            handle == UIResizeHandle::BottomRight || handle == UIResizeHandle::Bottom ||
            handle == UIResizeHandle::BottomLeft;
        return !(horizontal && fit_width) && !(vertical && fit_height);
    };

    DragMode hovered_mode = DragMode::None;
    UIResizeHandle hovered_resize = UIResizeHandle::BottomRight;
    if (is_viewport_hovered && point_near(mouse, pivot, 8.0f)) {
        hovered_mode = DragMode::Pivot;
    }
    if (is_viewport_hovered && hovered_mode == DragMode::None) {
        for (const ResizePoint& point : resize_points) {
            if (handle_available(point.handle) && point_near(mouse, point.position, 7.0f)) {
                hovered_mode = DragMode::Resize;
                hovered_resize = point.handle;
                break;
            }
        }
    }
    const bool anchors_collapsed = glm::all(glm::lessThanEqual(
        glm::abs(context->layout.anchor_max - context->layout.anchor_min),
        glm::vec2(0.0001f)
    ));
    if (is_viewport_hovered &&
        (hovered_mode == DragMode::None || ImGui::GetIO().KeyAlt)) {
        if (anchors_collapsed && point_near(mouse, anchor_pivot, 8.0f)) {
            hovered_mode = DragMode::AnchorBoth;
        } else if (!anchors_collapsed && point_near(mouse, anchor_min, 8.0f)) {
            hovered_mode = DragMode::AnchorMinimum;
        } else if (!anchors_collapsed && point_near(mouse, anchor_max, 8.0f)) {
            hovered_mode = DragMode::AnchorMaximum;
        }
    }
    const ImVec2 hit_min{rect_min.x - 4.0f, rect_min.y - 4.0f};
    const ImVec2 hit_max{rect_max.x + 4.0f, rect_max.y + 4.0f};
    if (is_viewport_hovered && hovered_mode == DragMode::None &&
        point_inside(mouse, hit_min, hit_max)) {
        hovered_mode = DragMode::Move;
    }
    m_hovered = hovered_mode != DragMode::None;

    if (m_drag_mode == DragMode::None && m_hovered && command_history &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_drag_entity = selected;
        m_drag_mode = hovered_mode;
        m_resize_handle = hovered_resize;
        m_initial_layout = context->layout;
        m_initial_rect = context->resolved.logical_rect;
        m_initial_parent_rect = context->resolved.parent_logical_rect;
        m_drag_start_canvas = mouse_to_canvas(mouse);
    }

    if (m_drag_mode != DragMode::None) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancel_drag(entities);
            cancelled_this_frame = true;
        } else if (io.MouseDown[ImGuiMouseButton_Left]) {
            const glm::vec2 current_canvas = mouse_to_canvas(mouse);
            auto& layout = entities.get_component<UILayoutComponent>(m_drag_entity);
            const glm::vec2 delta = current_canvas - m_drag_start_canvas;
            if (m_drag_mode == DragMode::Move) {
                layout = m_initial_layout;
                layout.anchored_position = {
                    snap_ui_value(
                        m_initial_layout.anchored_position.x + delta.x,
                        snap_enabled,
                        snap_step
                    ),
                    snap_ui_value(
                        m_initial_layout.anchored_position.y + delta.y,
                        snap_enabled,
                        snap_step
                    )
                };
            } else if (m_drag_mode == DragMode::Resize) {
                const UIRect resized = resize_ui_rect(
                    m_initial_rect,
                    delta,
                    m_resize_handle,
                    snap_enabled,
                    snap_step
                );
                layout = layout_for_ui_rect(m_initial_layout, m_initial_parent_rect, resized);
                if (fit_width) {
                    layout.size_delta.x = m_initial_layout.size_delta.x;
                    layout.anchored_position.x = m_initial_layout.anchored_position.x;
                }
                if (fit_height) {
                    layout.size_delta.y = m_initial_layout.size_delta.y;
                    layout.anchored_position.y = m_initial_layout.anchored_position.y;
                }
            } else if (m_drag_mode == DragMode::Pivot) {
                const glm::vec2 safe_size = glm::max(m_initial_rect.size, glm::vec2(0.0001f));
                layout = move_ui_pivot_preserving_rect(
                    m_initial_layout,
                    m_initial_parent_rect,
                    m_initial_rect,
                    (current_canvas - m_initial_rect.position) / safe_size
                );
            } else {
                glm::vec2 anchor = (current_canvas - m_initial_parent_rect.position) /
                    glm::max(m_initial_parent_rect.size, glm::vec2(0.0001f));
                if (snap_enabled && snap_step > 0.0f) {
                    const glm::vec2 snapped_canvas{
                        snap_ui_value(current_canvas.x, true, snap_step),
                        snap_ui_value(current_canvas.y, true, snap_step)
                    };
                    anchor = (snapped_canvas - m_initial_parent_rect.position) /
                        glm::max(m_initial_parent_rect.size, glm::vec2(0.0001f));
                }
                if (m_drag_mode == DragMode::AnchorBoth) {
                    layout = move_ui_anchors_preserving_rect(
                        m_initial_layout,
                        m_initial_parent_rect,
                        m_initial_rect,
                        anchor,
                        anchor
                    );
                } else if (m_drag_mode == DragMode::AnchorMinimum) {
                    layout = move_ui_anchors_preserving_rect(
                        m_initial_layout,
                        m_initial_parent_rect,
                        m_initial_rect,
                        glm::min(anchor, m_initial_layout.anchor_max),
                        m_initial_layout.anchor_max
                    );
                } else if (m_drag_mode == DragMode::AnchorMaximum) {
                    layout = move_ui_anchors_preserving_rect(
                        m_initial_layout,
                        m_initial_parent_rect,
                        m_initial_rect,
                        m_initial_layout.anchor_min,
                        glm::max(anchor, m_initial_layout.anchor_min)
                    );
                }
            }
        } else {
            const EntityID dragged_entity = m_drag_entity;
            const DragMode completed_mode = m_drag_mode;
            const UILayoutComponent before = m_initial_layout;
            const UILayoutComponent after = entities.get_component<UILayoutComponent>(dragged_entity);
            clear_drag();
            if (command_history && before != after) {
                const char* description = "Edit UI Layout";
                if (completed_mode == DragMode::Move) description = "Move UI Element";
                else if (completed_mode == DragMode::Resize) description = "Resize UI Element";
                else if (completed_mode == DragMode::Pivot) description = "Move UI Pivot";
                else description = "Move UI Anchors";
                command_history->push_executed_command(
                    std::make_unique<SetComponentCommand<UILayoutComponent>>(
                        &entities,
                        dragged_entity,
                        before,
                        after,
                        description
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

    const UIRect& updated_parent = context->resolved.parent_logical_rect;
    const glm::vec2 updated_anchor_min_canvas = updated_parent.position +
        updated_parent.size * context->layout.anchor_min;
    const glm::vec2 updated_anchor_max_canvas = updated_parent.position +
        updated_parent.size * context->layout.anchor_max;
    const glm::vec2 updated_anchor_pivot_canvas = glm::mix(
        updated_anchor_min_canvas, updated_anchor_max_canvas, context->layout.pivot
    );
    const glm::vec2 updated_pivot_canvas = context->resolved.logical_rect.position +
        context->resolved.logical_rect.size * context->layout.pivot;
    const ImVec2 updated_anchor_min = to_editor_screen(canvas_to_screen(
        updated_anchor_min_canvas, context->viewport
    ));
    const ImVec2 updated_anchor_max = to_editor_screen(canvas_to_screen(
        updated_anchor_max_canvas, context->viewport
    ));
    const ImVec2 updated_anchor_pivot = to_editor_screen(canvas_to_screen(
        updated_anchor_pivot_canvas, context->viewport
    ));
    const ImVec2 updated_pivot = to_editor_screen(canvas_to_screen(
        updated_pivot_canvas, context->viewport
    ));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    constexpr ImU32 anchor_color = IM_COL32(255, 188, 74, 240);
    constexpr ImU32 rect_color = IM_COL32(89, 202, 255, 255);
    if (std::abs(updated_anchor_min.x - updated_anchor_max.x) > 0.5f ||
        std::abs(updated_anchor_min.y - updated_anchor_max.y) > 0.5f) {
        draw_list->AddRect(
            updated_anchor_min,
            updated_anchor_max,
            IM_COL32(255, 188, 74, 155),
            0.0f,
            0,
            1.0f
        );
        draw_anchor_marker(draw_list, updated_anchor_min, anchor_color, 5.0f);
        draw_anchor_marker(draw_list, updated_anchor_max, anchor_color, 5.0f);
    } else {
        draw_anchor_marker(draw_list, updated_anchor_pivot, anchor_color, 5.5f);
    }
    if (m_hovered || m_drag_mode != DragMode::None) {
        draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(89, 202, 255, 24));
    }
    draw_list->AddRect(rect_min, rect_max, rect_color, 0.0f, 0, 2.0f);
    const float updated_center_x = (rect_min.x + rect_max.x) * 0.5f;
    const float updated_center_y = (rect_min.y + rect_max.y) * 0.5f;
    const std::array<ResizePoint, 8> updated_resize_points{{
        {UIResizeHandle::TopLeft, rect_min},
        {UIResizeHandle::Top, {updated_center_x, rect_min.y}},
        {UIResizeHandle::TopRight, {rect_max.x, rect_min.y}},
        {UIResizeHandle::Right, {rect_max.x, updated_center_y}},
        {UIResizeHandle::BottomRight, rect_max},
        {UIResizeHandle::Bottom, {updated_center_x, rect_max.y}},
        {UIResizeHandle::BottomLeft, {rect_min.x, rect_max.y}},
        {UIResizeHandle::Left, {rect_min.x, updated_center_y}}
    }};
    for (const ResizePoint& point : updated_resize_points) {
        if (handle_available(point.handle)) draw_marker(draw_list, point.position, rect_color, 4.0f);
    }
    draw_list->AddCircleFilled(updated_pivot, 4.0f, rect_color, 16);
    draw_list->AddCircle(updated_pivot, 7.0f, IM_COL32(89, 202, 255, 190), 20, 1.0f);

    const DragMode cursor_mode = m_drag_mode != DragMode::None ? m_drag_mode : hovered_mode;
    if (cursor_mode == DragMode::Resize) {
        const UIResizeHandle cursor_handle = m_drag_mode == DragMode::Resize
            ? m_resize_handle : hovered_resize;
        ImGui::SetMouseCursor(resize_cursor(cursor_handle));
    } else if (cursor_mode == DragMode::Move || cursor_mode == DragMode::Pivot ||
               cursor_mode == DragMode::AnchorMinimum ||
               cursor_mode == DragMode::AnchorMaximum ||
               cursor_mode == DragMode::AnchorBoth) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    return cancelled_this_frame || m_drag_mode != DragMode::None || m_hovered;
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
    m_drag_mode = DragMode::None;
    m_drag_start_canvas = {0.0f, 0.0f};
}

} // namespace Engine
