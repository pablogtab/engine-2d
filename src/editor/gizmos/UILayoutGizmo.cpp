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
    float rotation_snap_step,
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
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const glm::vec2 mouse_camera_screen{
        (mouse.x - viewport_position.x) / std::max(display_scale.x, 0.0001f),
        (mouse.y - viewport_position.y) / std::max(display_scale.y, 0.0001f)
    };
    const auto resolved_corners = resolved_ui_corners(context->resolved);
    std::array<ImVec2, 4> rect_corners{};
    for (std::size_t index = 0; index < rect_corners.size(); ++index) {
        rect_corners[index] = to_editor_screen(resolved_corners[index]);
    }
    const UIRect& parent = context->resolved.parent_logical_rect;
    const glm::vec2 anchor_min_canvas = parent.position + parent.size * context->layout.anchor_min;
    const glm::vec2 anchor_max_canvas = parent.position + parent.size * context->layout.anchor_max;
    const glm::vec2 anchor_pivot_canvas = glm::mix(
        anchor_min_canvas, anchor_max_canvas, context->layout.pivot
    );
    const auto parent_point_to_screen = [&](const glm::vec2& point) {
        const glm::vec2 local_screen =
            (point - context->resolved.parent_logical_pivot) * context->viewport.scale;
        const glm::vec2 rotated = rotate_ui_point(
            local_screen, glm::vec2(0.0f), context->resolved.parent_rotation
        );
        return context->resolved.parent_screen_pivot + rotated;
    };
    const ImVec2 anchor_min = to_editor_screen(parent_point_to_screen(anchor_min_canvas));
    const ImVec2 anchor_max = to_editor_screen(parent_point_to_screen(anchor_max_canvas));
    const ImVec2 anchor_pivot = to_editor_screen(parent_point_to_screen(anchor_pivot_canvas));
    const ImVec2 pivot = to_editor_screen(context->resolved.screen_pivot);

    struct ResizePoint {
        UIResizeHandle handle;
        ImVec2 position;
    };
    const auto midpoint = [](const ImVec2& lhs, const ImVec2& rhs) {
        return ImVec2((lhs.x + rhs.x) * 0.5f, (lhs.y + rhs.y) * 0.5f);
    };
    const std::array<ResizePoint, 8> resize_points{{
        {UIResizeHandle::TopLeft, rect_corners[0]},
        {UIResizeHandle::Top, midpoint(rect_corners[0], rect_corners[1])},
        {UIResizeHandle::TopRight, rect_corners[1]},
        {UIResizeHandle::Right, midpoint(rect_corners[1], rect_corners[2])},
        {UIResizeHandle::BottomRight, rect_corners[2]},
        {UIResizeHandle::Bottom, midpoint(rect_corners[2], rect_corners[3])},
        {UIResizeHandle::BottomLeft, rect_corners[3]},
        {UIResizeHandle::Left, midpoint(rect_corners[3], rect_corners[0])}
    }};
    const ImVec2 top_center = resize_points[1].position;
    const ImVec2 bottom_center = resize_points[5].position;
    const float outward_x = top_center.x - bottom_center.x;
    const float outward_y = top_center.y - bottom_center.y;
    const float outward_length = std::max(std::hypot(outward_x, outward_y), 0.0001f);
    const ImVec2 rotation_handle{
        top_center.x + outward_x / outward_length * 22.0f,
        top_center.y + outward_y / outward_length * 22.0f
    };
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
    if (is_viewport_hovered && point_near(mouse, rotation_handle, 8.0f)) {
        hovered_mode = DragMode::Rotate;
    } else if (is_viewport_hovered && point_near(mouse, pivot, 8.0f)) {
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
    if (is_viewport_hovered && hovered_mode == DragMode::None &&
        point_in_resolved_ui_rect(context->resolved, mouse_camera_screen)) {
        hovered_mode = DragMode::Move;
    }
    m_hovered = hovered_mode != DragMode::None;

    if (m_drag_mode == DragMode::None && m_hovered && command_history &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_drag_entity = selected;
        m_drag_mode = hovered_mode;
        m_resize_handle = hovered_resize;
        m_initial_layout = context->layout;
        if (entities.has_component<TransformComponent>(selected)) {
            m_initial_transform = entities.get_component<TransformComponent>(selected);
        }
        m_initial_rect = context->resolved.logical_rect;
        m_initial_parent_rect = context->resolved.parent_logical_rect;
        m_drag_start_screen = mouse_camera_screen;
        m_initial_logical_pivot = context->resolved.logical_rect.position +
            context->resolved.logical_rect.size * context->layout.pivot;
        m_initial_screen_pivot = context->resolved.screen_pivot;
        m_initial_parent_logical_pivot = context->resolved.parent_logical_pivot;
        m_initial_parent_screen_pivot = context->resolved.parent_screen_pivot;
        m_initial_parent_rotation = context->resolved.parent_rotation;
        m_initial_rotation = context->resolved.rotation;
        m_drag_start_angle = std::atan2(mouse.y - pivot.y, mouse.x - pivot.x);
    }

    if (m_drag_mode != DragMode::None) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancel_drag(entities);
            cancelled_this_frame = true;
        } else if (io.MouseDown[ImGuiMouseButton_Left]) {
            auto& layout = entities.get_component<UILayoutComponent>(m_drag_entity);
            const glm::vec2 safe_scale = glm::max(
                glm::abs(context->viewport.scale), glm::vec2(0.0001f)
            );
            const glm::vec2 screen_delta = mouse_camera_screen - m_drag_start_screen;
            const glm::vec2 parent_delta = rotate_ui_point(
                screen_delta, glm::vec2(0.0f), -m_initial_parent_rotation
            ) / safe_scale;
            const glm::vec2 local_delta = rotate_ui_point(
                screen_delta, glm::vec2(0.0f), -m_initial_rotation
            ) / safe_scale;
            if (m_drag_mode == DragMode::Move) {
                layout = m_initial_layout;
                layout.anchored_position = {
                    snap_ui_value(
                        m_initial_layout.anchored_position.x + parent_delta.x,
                        snap_enabled,
                        snap_step
                    ),
                    snap_ui_value(
                        m_initial_layout.anchored_position.y + parent_delta.y,
                        snap_enabled,
                        snap_step
                    )
                };
            } else if (m_drag_mode == DragMode::Resize) {
                const UIRect resized = resize_ui_rect(
                    m_initial_rect,
                    local_delta,
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
                const glm::vec2 logical_point = m_initial_logical_pivot + rotate_ui_point(
                    mouse_camera_screen - m_initial_screen_pivot,
                    glm::vec2(0.0f),
                    -m_initial_rotation
                ) / safe_scale;
                layout = move_ui_pivot_preserving_rect(
                    m_initial_layout,
                    m_initial_parent_rect,
                    m_initial_rect,
                    (logical_point - m_initial_rect.position) / safe_size
                );
            } else if (m_drag_mode == DragMode::Rotate) {
                if (entities.has_component<TransformComponent>(m_drag_entity)) {
                    const float current_angle = std::atan2(
                        mouse.y - pivot.y, mouse.x - pivot.x
                    );
                    float angle_delta = glm::degrees(current_angle - m_drag_start_angle);
                    while (angle_delta > 180.0f) angle_delta -= 360.0f;
                    while (angle_delta < -180.0f) angle_delta += 360.0f;
                    float rotation = m_initial_transform.rotation + angle_delta;
                    if (snap_enabled && rotation_snap_step > 0.0f) {
                        rotation = std::round(rotation / rotation_snap_step) * rotation_snap_step;
                    }
                    entities.get_component<TransformComponent>(m_drag_entity).rotation = rotation;
                }
            } else {
                glm::vec2 parent_point = m_initial_parent_logical_pivot + rotate_ui_point(
                    mouse_camera_screen - m_initial_parent_screen_pivot,
                    glm::vec2(0.0f),
                    -m_initial_parent_rotation
                ) / safe_scale;
                glm::vec2 anchor = (parent_point - m_initial_parent_rect.position) /
                    glm::max(m_initial_parent_rect.size, glm::vec2(0.0001f));
                if (snap_enabled && snap_step > 0.0f) {
                    const glm::vec2 snapped_canvas{
                        snap_ui_value(parent_point.x, true, snap_step),
                        snap_ui_value(parent_point.y, true, snap_step)
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
            const TransformComponent transform_before = m_initial_transform;
            const TransformComponent transform_after =
                entities.has_component<TransformComponent>(dragged_entity)
                    ? entities.get_component<TransformComponent>(dragged_entity)
                    : m_initial_transform;
            clear_drag();
            if (command_history && completed_mode == DragMode::Rotate &&
                std::abs(transform_before.rotation - transform_after.rotation) > 0.0001f) {
                command_history->push_executed_command(
                    std::make_unique<SetComponentCommand<TransformComponent>>(
                        &entities,
                        dragged_entity,
                        transform_before,
                        transform_after,
                        "Rotate UI Element"
                    )
                );
            } else if (command_history && before != after) {
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

    const auto updated_resolved_corners = resolved_ui_corners(context->resolved);
    std::array<ImVec2, 4> updated_rect_corners{};
    for (std::size_t index = 0; index < updated_rect_corners.size(); ++index) {
        updated_rect_corners[index] = to_editor_screen(updated_resolved_corners[index]);
    }

    const UIRect& updated_parent = context->resolved.parent_logical_rect;
    const glm::vec2 updated_anchor_min_canvas = updated_parent.position +
        updated_parent.size * context->layout.anchor_min;
    const glm::vec2 updated_anchor_max_canvas = updated_parent.position +
        updated_parent.size * context->layout.anchor_max;
    const glm::vec2 updated_anchor_pivot_canvas = glm::mix(
        updated_anchor_min_canvas, updated_anchor_max_canvas, context->layout.pivot
    );
    const auto updated_parent_point_to_screen = [&](const glm::vec2& point) {
        const glm::vec2 local_screen =
            (point - context->resolved.parent_logical_pivot) * context->viewport.scale;
        const glm::vec2 rotated = rotate_ui_point(
            local_screen, glm::vec2(0.0f), context->resolved.parent_rotation
        );
        return context->resolved.parent_screen_pivot + rotated;
    };
    const ImVec2 updated_anchor_min = to_editor_screen(
        updated_parent_point_to_screen(updated_anchor_min_canvas)
    );
    const ImVec2 updated_anchor_max = to_editor_screen(
        updated_parent_point_to_screen(updated_anchor_max_canvas)
    );
    const ImVec2 updated_anchor_pivot = to_editor_screen(
        updated_parent_point_to_screen(updated_anchor_pivot_canvas)
    );
    const ImVec2 updated_pivot = to_editor_screen(context->resolved.screen_pivot);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    constexpr ImU32 anchor_color = IM_COL32(255, 188, 74, 240);
    constexpr ImU32 rect_color = IM_COL32(89, 202, 255, 255);
    if (std::abs(updated_anchor_min.x - updated_anchor_max.x) > 0.5f ||
        std::abs(updated_anchor_min.y - updated_anchor_max.y) > 0.5f) {
        const glm::vec2 anchor_top_right_canvas{
            updated_anchor_max_canvas.x, updated_anchor_min_canvas.y
        };
        const glm::vec2 anchor_bottom_left_canvas{
            updated_anchor_min_canvas.x, updated_anchor_max_canvas.y
        };
        const ImVec2 anchor_corners[4]{
            updated_anchor_min,
            to_editor_screen(updated_parent_point_to_screen(anchor_top_right_canvas)),
            updated_anchor_max,
            to_editor_screen(updated_parent_point_to_screen(anchor_bottom_left_canvas))
        };
        draw_list->AddPolyline(
            anchor_corners, 4, IM_COL32(255, 188, 74, 155),
            ImDrawFlags_Closed, 1.0f
        );
        draw_anchor_marker(draw_list, updated_anchor_min, anchor_color, 5.0f);
        draw_anchor_marker(draw_list, updated_anchor_max, anchor_color, 5.0f);
    } else {
        draw_anchor_marker(draw_list, updated_anchor_pivot, anchor_color, 5.5f);
    }
    if (m_hovered || m_drag_mode != DragMode::None) {
        draw_list->AddQuadFilled(
            updated_rect_corners[0], updated_rect_corners[1],
            updated_rect_corners[2], updated_rect_corners[3],
            IM_COL32(89, 202, 255, 24)
        );
    }
    draw_list->AddPolyline(
        updated_rect_corners.data(), 4, rect_color, ImDrawFlags_Closed, 2.0f
    );
    const std::array<ResizePoint, 8> updated_resize_points{{
        {UIResizeHandle::TopLeft, updated_rect_corners[0]},
        {UIResizeHandle::Top, midpoint(updated_rect_corners[0], updated_rect_corners[1])},
        {UIResizeHandle::TopRight, updated_rect_corners[1]},
        {UIResizeHandle::Right, midpoint(updated_rect_corners[1], updated_rect_corners[2])},
        {UIResizeHandle::BottomRight, updated_rect_corners[2]},
        {UIResizeHandle::Bottom, midpoint(updated_rect_corners[2], updated_rect_corners[3])},
        {UIResizeHandle::BottomLeft, updated_rect_corners[3]},
        {UIResizeHandle::Left, midpoint(updated_rect_corners[3], updated_rect_corners[0])}
    }};
    for (const ResizePoint& point : updated_resize_points) {
        if (handle_available(point.handle)) draw_marker(draw_list, point.position, rect_color, 4.0f);
    }
    const ImVec2 updated_top_center = updated_resize_points[1].position;
    const ImVec2 updated_bottom_center = updated_resize_points[5].position;
    const float updated_outward_x = updated_top_center.x - updated_bottom_center.x;
    const float updated_outward_y = updated_top_center.y - updated_bottom_center.y;
    const float updated_outward_length = std::max(
        std::hypot(updated_outward_x, updated_outward_y), 0.0001f
    );
    const ImVec2 updated_rotation_handle{
        updated_top_center.x + updated_outward_x / updated_outward_length * 22.0f,
        updated_top_center.y + updated_outward_y / updated_outward_length * 22.0f
    };
    draw_list->AddCircle(updated_rotation_handle, 5.0f, rect_color, 20, 2.0f);
    draw_list->AddCircleFilled(updated_pivot, 4.0f, rect_color, 16);
    draw_list->AddCircle(updated_pivot, 7.0f, IM_COL32(89, 202, 255, 190), 20, 1.0f);

    const DragMode cursor_mode = m_drag_mode != DragMode::None ? m_drag_mode : hovered_mode;
    if (cursor_mode == DragMode::Resize) {
        const UIResizeHandle cursor_handle = m_drag_mode == DragMode::Resize
            ? m_resize_handle : hovered_resize;
        ImGui::SetMouseCursor(resize_cursor(cursor_handle));
    } else if (cursor_mode == DragMode::Rotate) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
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
    if (m_drag_mode == DragMode::Rotate && m_drag_entity != 0 &&
        entities.is_alive(m_drag_entity) &&
        entities.has_component<TransformComponent>(m_drag_entity)) {
        entities.get_component<TransformComponent>(m_drag_entity) = m_initial_transform;
    }
    clear_drag();
}

void UILayoutGizmo::clear_drag() {
    m_drag_entity = 0;
    m_drag_mode = DragMode::None;
    m_drag_start_screen = {0.0f, 0.0f};
    m_drag_start_angle = 0.0f;
}

} // namespace Engine
