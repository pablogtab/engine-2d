#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/UIComponents.hpp"
#include "events/Event.hpp"
#include "events/EventDispatcher.hpp"
#include "ui/UIInputSystem.hpp"

#include <cassert>
#include <iostream>
#include <vector>

namespace {

using namespace Engine;

EntityID add_button(
    EntityManager& entities,
    EntityID parent,
    const UILayoutComponent& layout = UILayoutComponent{}
) {
    const EntityID entity = entities.create_entity();
    entities.add_component<HierarchyComponent>(entity, HierarchyComponent{parent});
    entities.add_component<TransformComponent>(entity, TransformComponent{});
    entities.add_component<UILayoutComponent>(entity, layout);
    entities.add_component<UIButtonComponent>(entity, UIButtonComponent{});
    entities.get_component<HierarchyComponent>(parent).children.push_back(entity);
    return entity;
}

EntityID add_canvas(EntityManager& entities, std::int16_t sorting_order = 0) {
    const EntityID entity = entities.create_entity();
    CanvasComponent canvas;
    canvas.reference_size = {100.0f, 100.0f};
    canvas.scaling_policy = PresentationMode::Stretch;
    canvas.sorting_order = sorting_order;
    canvas.use_safe_area = false;
    entities.add_component<CanvasComponent>(entity, canvas);
    entities.add_component<HierarchyComponent>(entity, HierarchyComponent{});
    return entity;
}

UILayoutComponent fullscreen_layout() {
    UILayoutComponent layout;
    layout.anchor_min = {0.0f, 0.0f};
    layout.anchor_max = {1.0f, 1.0f};
    layout.pivot = {0.5f, 0.5f};
    layout.size_delta = {0.0f, 0.0f};
    return layout;
}

void test_overlap_uses_canvas_and_authored_order() {
    EntityManager entities;
    const EntityID canvas = add_canvas(entities);
    const EntityID bottom = add_button(entities, canvas, fullscreen_layout());
    const EntityID top = add_button(entities, canvas, fullscreen_layout());

    UIInputSystem input;
    auto result = input.update(entities, {100.0f, 100.0f}, {}, {{50.0f, 50.0f}});
    assert(result.hovered == top);

    entities.add_component<UIImageComponent>(top, UIImageComponent{});
    entities.get_component<UIImageComponent>(top).raycast_target = false;
    result = input.update(entities, {100.0f, 100.0f}, {}, {{50.0f, 50.0f}});
    assert(result.hovered == bottom);

    const EntityID upper_canvas = add_canvas(entities, 2);
    const EntityID upper = add_button(entities, upper_canvas, fullscreen_layout());
    result = input.update(entities, {100.0f, 100.0f}, {}, {{50.0f, 50.0f}});
    assert(result.hovered == upper);
}

void test_pointer_capture_and_click_contract() {
    EntityManager entities;
    const EntityID canvas = add_canvas(entities);
    const EntityID button = add_button(entities, canvas, fullscreen_layout());
    EventDispatcher dispatcher;
    std::vector<EntityID> published;
    dispatcher.subscribe<UIButtonClickedEvent>([&](const UIButtonClickedEvent& event) {
        published.push_back(event.entity);
    });

    UIInputSystem input;
    auto result = input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {50.0f, 50.0f}, .pointer_down = true, .pointer_pressed = true},
        &dispatcher
    );
    assert(result.captured == button);
    assert(result.focused == button);
    assert(input.state_for(entities, button) == UIInteractionState::Pressed);

    result = input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {150.0f, 50.0f}, .pointer_down = true},
        &dispatcher
    );
    assert(result.hovered == 0);
    assert(result.captured == button);
    assert(input.state_for(entities, button) == UIInteractionState::Pressed);

    result = input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {150.0f, 50.0f}, .pointer_released = true},
        &dispatcher
    );
    assert(result.clicked.empty());
    assert(published.empty());

    static_cast<void>(input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {50.0f, 50.0f}, .pointer_down = true, .pointer_pressed = true},
        &dispatcher
    ));
    result = input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {50.0f, 50.0f}, .pointer_released = true},
        &dispatcher
    );
    assert(result.clicked == std::vector<EntityID>{button});
    assert(published == std::vector<EntityID>{button});
}

void test_focus_navigation_activation_and_invalidation() {
    EntityManager entities;
    const EntityID canvas = add_canvas(entities);
    const EntityID first = add_button(entities, canvas, fullscreen_layout());
    const EntityID second = add_button(entities, canvas, fullscreen_layout());
    EventDispatcher dispatcher;
    std::vector<EntityID> clicked;
    dispatcher.subscribe<UIButtonClickedEvent>([&](const UIButtonClickedEvent& event) {
        clicked.push_back(event.entity);
    });

    UIInputSystem input;
    auto result = input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {-1.0f, -1.0f}, .focus_next = true},
        &dispatcher
    );
    assert(result.focused == first);
    result = input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {-1.0f, -1.0f}, .focus_next = true},
        &dispatcher
    );
    assert(result.focused == second);
    result = input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {-1.0f, -1.0f}, .focus_previous = true},
        &dispatcher
    );
    assert(result.focused == first);

    static_cast<void>(input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {-1.0f, -1.0f}, .activate_down = true, .activate_pressed = true},
        &dispatcher
    ));
    result = input.update(
        entities, {100.0f, 100.0f}, {},
        {.pointer_position = {-1.0f, -1.0f}, .activate_released = true},
        &dispatcher
    );
    assert(result.clicked == std::vector<EntityID>{first});
    assert(clicked == std::vector<EntityID>{first});

    entities.get_component<UIButtonComponent>(first).interactable = false;
    result = input.update(entities, {100.0f, 100.0f}, {}, {{-1.0f, -1.0f}}, &dispatcher);
    assert(result.focused == 0);
    assert(input.state_for(entities, first) == UIInteractionState::Disabled);
}

void test_hit_testing_uses_canvas_viewport_conversion() {
    EntityManager entities;
    const EntityID canvas = add_canvas(entities);
    auto& canvas_component = entities.get_component<CanvasComponent>(canvas);
    canvas_component.scaling_policy = PresentationMode::Fit;
    const EntityID button = add_button(entities, canvas, fullscreen_layout());

    UIInputSystem input;
    auto result = input.update(entities, {200.0f, 100.0f}, {}, {{25.0f, 50.0f}});
    assert(result.hovered == 0);
    result = input.update(entities, {200.0f, 100.0f}, {}, {{100.0f, 50.0f}});
    assert(result.hovered == button);
}

void test_hit_testing_uses_the_fit_content_rect() {
    EntityManager entities;
    const EntityID canvas = add_canvas(entities);
    UILayoutComponent layout;
    layout.size_delta = {80.0f, 80.0f};
    const EntityID button = add_button(entities, canvas, layout);
    UITextComponent text;
    text.text.clear();
    text.fit_content_width = true;
    text.fit_content_height = true;
    text.fit_padding = glm::vec4(10.0f);
    entities.add_component<UITextComponent>(button, text);

    UIInputSystem input;
    auto result = input.update(entities, {100.0f, 100.0f}, {}, {{30.0f, 50.0f}});
    assert(result.hovered == 0);
    result = input.update(entities, {100.0f, 100.0f}, {}, {{50.0f, 50.0f}});
    assert(result.hovered == button);
}

void test_hit_testing_uses_rotated_ui_geometry() {
    EntityManager entities;
    const EntityID canvas = add_canvas(entities);
    UILayoutComponent layout;
    layout.size_delta = {80.0f, 20.0f};
    const EntityID button = add_button(entities, canvas, layout);
    entities.get_component<TransformComponent>(button).rotation = 90.0f;

    UIInputSystem input;
    auto result = input.update(entities, {100.0f, 100.0f}, {}, {{50.0f, 85.0f}});
    assert(result.hovered == button);
    result = input.update(entities, {100.0f, 100.0f}, {}, {{80.0f, 50.0f}});
    assert(result.hovered == 0);
}

} // namespace

int main() {
    test_overlap_uses_canvas_and_authored_order();
    test_pointer_capture_and_click_contract();
    test_focus_navigation_activation_and_invalidation();
    test_hit_testing_uses_canvas_viewport_conversion();
    test_hit_testing_uses_the_fit_content_rect();
    test_hit_testing_uses_rotated_ui_geometry();
    std::cout << "UI input tests passed\n";
    return 0;
}
