#include "ecs/components/UIComponents.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/EntityManager.hpp"
#include "ui/CanvasLayout.hpp"
#include "scene/Camera2D.hpp"
#include "editor/tools/UILayoutGizmoMath.hpp"
#include "editor/inspector/UIAnchorPresets.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

using namespace Engine;

bool approximate(float lhs, float rhs, float epsilon = 0.001f) {
    return std::abs(lhs - rhs) <= epsilon;
}

void assert_vec2(const glm::vec2& actual, const glm::vec2& expected) {
    assert(approximate(actual.x, expected.x));
    assert(approximate(actual.y, expected.y));
}

void test_canvas_scaling_policies() {
    CanvasComponent canvas;
    canvas.reference_size = {1280.0f, 720.0f};
    canvas.use_safe_area = false;

    canvas.scaling_policy = PresentationMode::Fit;
    auto viewport = calculate_canvas_viewport(canvas, {1000.0f, 1000.0f});
    assert_vec2(viewport.scale, {0.78125f, 0.78125f});
    assert_vec2(viewport.physical_rect.position, {0.0f, 218.75f});
    assert_vec2(viewport.physical_rect.size, {1000.0f, 562.5f});

    canvas.scaling_policy = PresentationMode::Fill;
    viewport = calculate_canvas_viewport(canvas, {1000.0f, 1000.0f});
    assert_vec2(viewport.scale, {1.3888889f, 1.3888889f});
    assert_vec2(viewport.physical_rect.position, {-388.8889f, 0.0f});
    assert_vec2(viewport.physical_rect.size, {1777.7778f, 1000.0f});

    canvas.scaling_policy = PresentationMode::Stretch;
    viewport = calculate_canvas_viewport(canvas, {1000.0f, 1000.0f});
    assert_vec2(viewport.scale, {0.78125f, 1.3888889f});
    assert_vec2(viewport.logical_rect.size, canvas.reference_size);
    assert_vec2(viewport.physical_rect.size, {1000.0f, 1000.0f});

    canvas.scaling_policy = PresentationMode::Responsive;
    viewport = calculate_canvas_viewport(canvas, {1000.0f, 1000.0f});
    assert_vec2(viewport.scale, {0.78125f, 0.78125f});
    assert_vec2(viewport.logical_rect.size, {1280.0f, 1280.0f});
    assert_vec2(viewport.physical_rect.size, {1000.0f, 1000.0f});
}

void test_safe_area_changes_the_available_canvas() {
    CanvasComponent canvas;
    canvas.reference_size = {390.0f, 844.0f};
    canvas.scaling_policy = PresentationMode::Responsive;
    canvas.use_safe_area = true;
    const auto viewport = calculate_canvas_viewport(
        canvas,
        {1170.0f, 2532.0f},
        {0.0f, 141.0f, 0.0f, 102.0f}
    );
    assert_vec2(viewport.safe_rect.position, {0.0f, 141.0f});
    assert_vec2(viewport.safe_rect.size, {1170.0f, 2289.0f});
    assert(approximate(viewport.scale.x, viewport.scale.y));
    assert_vec2(viewport.physical_rect.position, viewport.safe_rect.position);
    assert_vec2(viewport.physical_rect.size, viewport.safe_rect.size);
    assert(viewport.logical_rect.size.x > canvas.reference_size.x);
    assert(approximate(viewport.logical_rect.size.y, canvas.reference_size.y));

    canvas.use_safe_area = false;
    const auto full = calculate_canvas_viewport(
        canvas,
        {1170.0f, 2532.0f},
        {0.0f, 141.0f, 0.0f, 102.0f}
    );
    assert_vec2(full.safe_rect.position, {0.0f, 0.0f});
    assert_vec2(full.safe_rect.size, {1170.0f, 2532.0f});
}

void test_nested_anchor_and_pivot_layout() {
    const UIRect root{{0.0f, 0.0f}, {1280.0f, 720.0f}};
    UILayoutComponent panel;
    panel.anchor_min = {0.0f, 0.0f};
    panel.anchor_max = {1.0f, 1.0f};
    panel.size_delta = {-80.0f, -80.0f};
    const UIRect panel_rect = resolve_ui_rect(panel, root);
    assert_vec2(panel_rect.position, {40.0f, 40.0f});
    assert_vec2(panel_rect.size, {1200.0f, 640.0f});

    UILayoutComponent button;
    button.anchor_min = {0.5f, 0.5f};
    button.anchor_max = {0.5f, 0.5f};
    button.pivot = {0.5f, 0.5f};
    button.anchored_position = {10.0f, -20.0f};
    button.size_delta = {200.0f, 100.0f};
    const UIRect button_rect = resolve_ui_rect(button, panel_rect);
    assert_vec2(button_rect.position, {550.0f, 290.0f});
    assert_vec2(button_rect.size, {200.0f, 100.0f});
}

void test_canvas_to_screen_mapping() {
    CanvasComponent canvas;
    canvas.reference_size = {1280.0f, 720.0f};
    canvas.scaling_policy = PresentationMode::Fit;
    canvas.use_safe_area = false;
    const auto viewport = calculate_canvas_viewport(canvas, {1000.0f, 1000.0f});
    const UIRect logical{{320.0f, 180.0f}, {640.0f, 360.0f}};
    const UIRect physical = canvas_to_screen(logical, viewport);
    assert_vec2(physical.position, {250.0f, 359.375f});
    assert_vec2(physical.size, {500.0f, 281.25f});
    assert_vec2(screen_to_canvas(physical.position, viewport), logical.position);
    assert_vec2(
        screen_to_canvas(physical.position + physical.size, viewport),
        logical.position + logical.size
    );
}

void test_hierarchy_resolution_preserves_nested_order() {
    EntityManager entities;
    const EntityID canvas_entity = entities.create_entity();
    const EntityID panel_entity = entities.create_entity();
    const EntityID button_entity = entities.create_entity();
    entities.add_component<CanvasComponent>(canvas_entity, CanvasComponent{});
    entities.add_component<HierarchyComponent>(canvas_entity, HierarchyComponent{});
    entities.add_component<HierarchyComponent>(panel_entity, HierarchyComponent{canvas_entity});
    entities.add_component<HierarchyComponent>(button_entity, HierarchyComponent{panel_entity});
    entities.get_component<HierarchyComponent>(canvas_entity).children = {panel_entity};
    entities.get_component<HierarchyComponent>(panel_entity).children = {button_entity};

    UILayoutComponent panel;
    panel.anchor_min = {0.0f, 0.0f};
    panel.anchor_max = {1.0f, 1.0f};
    panel.size_delta = {-80.0f, -80.0f};
    entities.add_component<UILayoutComponent>(panel_entity, panel);
    UILayoutComponent button;
    button.size_delta = {200.0f, 100.0f};
    button.anchored_position = {10.0f, -20.0f};
    entities.add_component<UILayoutComponent>(button_entity, button);

    CanvasViewport viewport;
    viewport.logical_rect = {{0.0f, 0.0f}, {1280.0f, 720.0f}};
    viewport.physical_rect = viewport.logical_rect;
    viewport.safe_rect = viewport.logical_rect;
    const auto resolved = resolve_canvas_hierarchy(entities, canvas_entity, viewport);
    assert(resolved.size() == 2);
    assert(resolved[0].entity == panel_entity);
    assert(resolved[1].entity == button_entity);
    assert_vec2(resolved[0].logical_rect.position, {40.0f, 40.0f});
    assert_vec2(resolved[1].logical_rect.position, {550.0f, 290.0f});
    assert(resolved[0].parent_logical_rect == viewport.logical_rect);
    assert(resolved[1].parent_logical_rect == resolved[0].logical_rect);
    assert(resolved[1].screen_rect == resolved[1].logical_rect);
}

void test_fit_content_drives_the_resolved_rect_and_children() {
    EntityManager entities;
    const EntityID canvas_entity = entities.create_entity();
    const EntityID fitted_entity = entities.create_entity();
    const EntityID child_entity = entities.create_entity();
    entities.add_component<CanvasComponent>(canvas_entity, CanvasComponent{});
    entities.add_component<HierarchyComponent>(canvas_entity, HierarchyComponent{});
    entities.add_component<HierarchyComponent>(fitted_entity, HierarchyComponent{canvas_entity});
    entities.add_component<HierarchyComponent>(child_entity, HierarchyComponent{fitted_entity});
    entities.get_component<HierarchyComponent>(canvas_entity).children = {fitted_entity};
    entities.get_component<HierarchyComponent>(fitted_entity).children = {child_entity};

    UILayoutComponent fitted_layout;
    fitted_layout.size_delta = {160.0f, 48.0f};
    entities.add_component<UILayoutComponent>(fitted_entity, fitted_layout);
    UITextComponent text;
    text.text.clear();
    text.fit_content_width = true;
    text.fit_content_height = true;
    text.fit_padding = {10.0f, 5.0f, 10.0f, 5.0f};
    text.margins.left = {25.0f, UILengthUnit::Percent};
    text.margins.right = {25.0f, UILengthUnit::Percent};
    entities.add_component<UITextComponent>(fitted_entity, text);

    UILayoutComponent child_layout;
    child_layout.anchor_min = {0.0f, 0.0f};
    child_layout.anchor_max = {1.0f, 1.0f};
    child_layout.size_delta = {0.0f, 0.0f};
    entities.add_component<UILayoutComponent>(child_entity, child_layout);

    CanvasViewport viewport;
    viewport.logical_rect = {{0.0f, 0.0f}, {100.0f, 100.0f}};
    viewport.physical_rect = viewport.logical_rect;
    viewport.safe_rect = viewport.logical_rect;
    const auto resolved = resolve_canvas_hierarchy(entities, canvas_entity, viewport);
    assert(resolved.size() == 2);
    assert_vec2(resolved[0].logical_rect.position, {30.0f, 45.0f});
    assert_vec2(resolved[0].logical_rect.size, {40.0f, 10.0f});
    assert(resolved[1].parent_logical_rect == resolved[0].logical_rect);
    assert(resolved[1].logical_rect == resolved[0].logical_rect);
}

void test_editor_canvas_stays_at_its_world_position() {
    CanvasComponent canvas;
    canvas.reference_size = {100.0f, 50.0f};
    Camera2D camera(800.0f, 600.0f);

    auto viewport = calculate_canvas_editor_viewport(canvas, {10.0f, 20.0f}, camera);
    assert_vec2(viewport.physical_rect.position, {360.0f, 255.0f});
    assert_vec2(viewport.physical_rect.size, {100.0f, 50.0f});

    camera.set_position({50.0f, 0.0f});
    viewport = calculate_canvas_editor_viewport(canvas, {10.0f, 20.0f}, camera);
    assert_vec2(viewport.physical_rect.position, {310.0f, 255.0f});

    camera.set_zoom(2.0f);
    viewport = calculate_canvas_editor_viewport(canvas, {10.0f, 20.0f}, camera);
    assert_vec2(viewport.physical_rect.position, {220.0f, 210.0f});
    assert_vec2(viewport.physical_rect.size, {200.0f, 100.0f});
}

void test_ui_drag_converts_screen_motion_to_canvas_pixels() {
    CanvasViewport viewport;
    viewport.scale = {0.5f, 0.5f};
    assert_vec2(
        calculate_ui_drag_position({10.0f, 20.0f}, {20.0f, -10.0f}, viewport),
        {50.0f, 0.0f}
    );
    assert_vec2(
        calculate_ui_drag_position(
            {10.0f, 20.0f}, {20.0f, -10.0f}, viewport, true, 16.0f
        ),
        {48.0f, 0.0f}
    );
}

void test_ui_resize_handles_preserve_the_opposite_edges() {
    const UIRect initial{{100.0f, 80.0f}, {200.0f, 100.0f}};
    const UIRect left = resize_ui_rect(
        initial, {35.0f, 0.0f}, UIResizeHandle::Left
    );
    assert_vec2(left.position, {135.0f, 80.0f});
    assert_vec2(left.size, {165.0f, 100.0f});
    assert_vec2(left.max(), initial.max());

    const UIRect bottom_right = resize_ui_rect(
        initial, {17.0f, 24.0f}, UIResizeHandle::BottomRight, true, 16.0f
    );
    assert_vec2(bottom_right.position, initial.position);
    assert_vec2(bottom_right.max(), {320.0f, 208.0f});
}

void test_ui_rect_conversion_supports_fixed_and_stretched_layouts() {
    const UIRect parent{{20.0f, 30.0f}, {800.0f, 600.0f}};
    UILayoutComponent fixed;
    fixed.anchor_min = {0.5f, 0.5f};
    fixed.anchor_max = fixed.anchor_min;
    const UIRect desired{{120.0f, 140.0f}, {260.0f, 90.0f}};
    const UILayoutComponent fixed_result = layout_for_ui_rect(fixed, parent, desired);
    assert(resolve_ui_rect(fixed_result, parent) == desired);

    UILayoutComponent stretched;
    stretched.anchor_min = {0.1f, 0.2f};
    stretched.anchor_max = {0.9f, 0.8f};
    stretched.pivot = {0.25f, 0.75f};
    const UILayoutComponent stretched_result = layout_for_ui_rect(stretched, parent, desired);
    assert(resolve_ui_rect(stretched_result, parent) == desired);
}

void test_ui_pivot_and_anchor_handles_preserve_visual_rect() {
    const UIRect parent{{0.0f, 0.0f}, {1000.0f, 500.0f}};
    UILayoutComponent initial;
    initial.anchor_min = {0.25f, 0.25f};
    initial.anchor_max = {0.75f, 0.75f};
    initial.pivot = {0.5f, 0.5f};
    initial.anchored_position = {40.0f, -20.0f};
    initial.size_delta = {-100.0f, 30.0f};
    const UIRect before = resolve_ui_rect(initial, parent);

    const UILayoutComponent pivot = move_ui_pivot_preserving_rect(
        initial, parent, before, {0.2f, 0.8f}
    );
    assert_vec2(pivot.pivot, {0.2f, 0.8f});
    assert(resolve_ui_rect(pivot, parent) == before);

    const UILayoutComponent anchors = move_ui_anchors_preserving_rect(
        initial, parent, before, {0.1f, 0.15f}, {0.9f, 0.95f}
    );
    assert_vec2(anchors.anchor_min, {0.1f, 0.15f});
    assert_vec2(anchors.anchor_max, {0.9f, 0.95f});
    assert(resolve_ui_rect(anchors, parent) == before);
}

void test_anchor_presets_follow_unity_style_modifiers() {
    const UIRect parent{{0.0f, 0.0f}, {1280.0f, 720.0f}};
    UILayoutComponent layout;
    layout.anchor_min = {0.5f, 0.5f};
    layout.anchor_max = {0.5f, 0.5f};
    layout.anchored_position = {-220.0f, 80.0f};
    layout.size_delta = {180.0f, 48.0f};
    const UIRect before = resolve_ui_rect(layout, parent);

    const auto& top_left = ui_anchor_preset_definitions().front();
    const UILayoutComponent moved = apply_ui_anchor_preset(layout, parent, top_left);
    assert(moved.anchor_min == glm::vec2(0.0f, 0.0f));
    assert(moved.anchor_max == glm::vec2(0.0f, 0.0f));
    assert(moved.anchored_position == layout.anchored_position);
    assert(resolve_ui_rect(moved, parent) != before);

    const auto& full_stretch = ui_anchor_preset_definitions().back();
    const UILayoutComponent stretched = apply_ui_anchor_preset(
        layout, parent, full_stretch, {true, false}
    );
    assert(resolve_ui_rect(stretched, parent) == parent);
    assert(stretched.anchored_position == glm::vec2(0.0f));
    assert(stretched.size_delta == glm::vec2(0.0f));
    assert(classify_ui_anchor_preset(stretched) == UIAnchorPreset::FullStretch);

    const UILayoutComponent snapped_top_left = apply_ui_anchor_preset(
        layout, parent, top_left, {true, true}
    );
    assert(snapped_top_left.pivot == glm::vec2(0.0f, 0.0f));
    assert(snapped_top_left.anchored_position == glm::vec2(0.0f));
    assert(snapped_top_left.size_delta == before.size);
    assert(resolve_ui_rect(snapped_top_left, parent).position == parent.position);

    UILayoutComponent custom = layout;
    custom.anchor_min = {0.17f, 0.23f};
    custom.anchor_max = {0.81f, 0.92f};
    assert(!classify_ui_anchor_preset(custom).has_value());
}

} // namespace

int main() {
    test_canvas_scaling_policies();
    test_safe_area_changes_the_available_canvas();
    test_nested_anchor_and_pivot_layout();
    test_canvas_to_screen_mapping();
    test_hierarchy_resolution_preserves_nested_order();
    test_fit_content_drives_the_resolved_rect_and_children();
    test_editor_canvas_stays_at_its_world_position();
    test_ui_drag_converts_screen_motion_to_canvas_pixels();
    test_ui_resize_handles_preserve_the_opposite_edges();
    test_ui_rect_conversion_supports_fixed_and_stretched_layouts();
    test_ui_pivot_and_anchor_handles_preserve_visual_rect();
    test_anchor_presets_follow_unity_style_modifiers();
    std::cout << "Canvas layout tests passed\n";
    return 0;
}
