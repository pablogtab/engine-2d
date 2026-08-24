#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/components/UIComponents.hpp"
#include "editor/state/EditorEntityStateStore.hpp"
#include "editor/tools/EditorEntityVisibilityFilter.hpp"
#include "editor/tools/ViewportSelectionTool.hpp"
#include "editor/tools/ViewportNavigation.hpp"
#include "scene/Camera2D.hpp"
#include "scene/Scene.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace Engine;

namespace {

class ContractScene final : public Scene {
public:
    ContractScene() : Scene("ContractScene"), editor_camera(800.0f, 600.0f), game_camera(1280.0f, 720.0f) {}

    Camera2D editor_camera;
    Camera2D game_camera;
};

bool near(float lhs, float rhs) {
    return std::abs(lhs - rhs) < 0.001f;
}

} // namespace

void test_visibility_filter_behavior() {
    EditorEntityStateStore store;
    SceneViewEntityFilter filter(&store);

    EntityID e1 = 1;
    EntityID e2 = 2;

    assert(filter.should_render(e1));
    assert(filter.should_render(e2));
    assert(filter.should_interact(e1));
    assert(filter.should_interact(e2));

    store.set_visible(e1, false);
    assert(!filter.should_render(e1));
    assert(!filter.should_interact(e1)); // Hidden in scene view -> not selectable in viewport
    assert(filter.should_render(e2));

    store.set_locked(e2, true);
    assert(filter.should_render(e2));
    assert(!filter.should_interact(e2)); // Locked -> not selectable in viewport

    std::cout << "[PASS] test_visibility_filter_behavior" << std::endl;
}

void test_selection_tool_filtering() {
    EntityManager em;
    EditorEntityStateStore store;
    SceneViewEntityFilter filter(&store);
    Camera2D camera(800.0f, 600.0f);
    camera.set_position(glm::vec2(0.0f, 0.0f));

    ViewportSelectionTool selection_tool;

    // Entity 1: at (10, 0) with size (100, 100)
    EntityID e1 = em.create_entity();
    em.add_component<TransformComponent>(e1, TransformComponent{glm::vec3(10.0f, 0.0f, 0.0f), glm::vec2(1.0f, 1.0f)});
    em.add_component<SpriteComponent>(e1, SpriteComponent{nullptr, glm::vec2(100.0f, 100.0f)});

    // Entity 2: at (0, 0) with size (100, 100)
    EntityID e2 = em.create_entity();
    em.add_component<TransformComponent>(e2, TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(1.0f, 1.0f)});
    em.add_component<SpriteComponent>(e2, SpriteComponent{nullptr, glm::vec2(100.0f, 100.0f)});

    // Pick at (0, 0) -> picks topmost e2
    EntityID picked = selection_tool.pick_entity(em, glm::vec2(0.0f, 0.0f), &camera, &filter);
    assert(picked == e2);

    // Hide e2 -> picking at (0, 0) now picks e1
    store.set_visible(e2, false);
    picked = selection_tool.pick_entity(em, glm::vec2(0.0f, 0.0f), &camera, &filter);
    assert(picked == e1);

    // Lock e1 -> picking at (0, 0) picks nothing (returns 0)
    store.set_locked(e1, true);
    picked = selection_tool.pick_entity(em, glm::vec2(0.0f, 0.0f), &camera, &filter);
    assert(picked == 0);

    std::cout << "[PASS] test_selection_tool_filtering" << std::endl;
}

void test_hierarchical_visibility_inheritance() {
    EntityManager em;
    EditorEntityStateStore store;
    SceneViewEntityFilter filter(&store, &em);

    EntityID parent = em.create_entity();
    em.add_component<TransformComponent>(parent, TransformComponent{glm::vec3(0.0f)});

    EntityID child = em.create_entity();
    em.add_component<TransformComponent>(child, TransformComponent{glm::vec3(10.0f)});
    assert(em.set_parent(child, parent).succeeded());

    EntityID grandchild = em.create_entity();
    em.add_component<TransformComponent>(grandchild, TransformComponent{glm::vec3(20.0f)});
    assert(em.set_parent(grandchild, child).succeeded());

    // Initially all are visible
    assert(filter.should_render(parent));
    assert(filter.should_render(child));
    assert(filter.should_render(grandchild));
    assert(filter.should_interact(grandchild));

    // Hide top parent -> child and grandchild are also hidden
    store.set_visible(parent, false);
    assert(!filter.should_render(parent));
    assert(!filter.should_render(child));
    assert(!filter.should_render(grandchild));
    assert(!filter.should_interact(grandchild));

    // Show parent again -> all visible
    store.set_visible(parent, true);
    assert(filter.should_render(grandchild));

    // Hide intermediate child -> grandchild is hidden, parent stays visible
    store.set_visible(child, false);
    assert(filter.should_render(parent));
    assert(!filter.should_render(child));
    assert(!filter.should_render(grandchild));

    std::cout << "[PASS] test_hierarchical_visibility_inheritance" << std::endl;
}

void test_canvas_ui_picking_matches_editor_preview() {
    EntityManager em;
    EditorEntityStateStore store;
    SceneViewEntityFilter filter(&store, &em);
    Camera2D camera(800.0f, 600.0f);
    ViewportSelectionTool selection_tool;

    const EntityID world_sprite = em.create_entity();
    em.add_component<TransformComponent>(world_sprite, TransformComponent{});
    em.add_component<SpriteComponent>(
        world_sprite, SpriteComponent{nullptr, glm::vec2(100.0f, 100.0f)}
    );

    const EntityID canvas = em.create_entity();
    CanvasComponent canvas_component;
    canvas_component.reference_size = {100.0f, 100.0f};
    em.add_component<CanvasComponent>(canvas, canvas_component);
    em.add_component<TransformComponent>(
        canvas, TransformComponent{glm::vec3(120.0f, -80.0f, 0.0f)}
    );

    const EntityID button = em.create_entity();
    UILayoutComponent layout;
    layout.size_delta = {40.0f, 20.0f};
    em.add_component<TransformComponent>(button, TransformComponent{});
    em.add_component<UILayoutComponent>(button, layout);
    em.add_component<UIButtonComponent>(button, UIButtonComponent{});
    em.add_component<UITextComponent>(button, UITextComponent{});
    assert(em.set_parent(button, canvas).succeeded());

    EntityID picked = selection_tool.pick_entity(em, {120.0f, -80.0f}, &camera, &filter);
    assert(picked == button);

    store.set_visible(button, false);
    picked = selection_tool.pick_entity(em, {120.0f, -80.0f}, &camera, &filter);
    assert(picked == 0);

    picked = selection_tool.pick_entity(em, {0.0f, 0.0f}, &camera, &filter);
    assert(picked == world_sprite);

    std::cout << "[PASS] test_canvas_ui_picking_matches_editor_preview" << std::endl;
}

void test_navigation_uses_scene_render_camera_contract() {
    ContractScene scene;
    scene.game_camera.set_position({77.0f, 88.0f});

    assert(ViewportNavigation::pan_world(&scene.editor_camera, {25.0f, -10.0f}));
    assert(near(scene.editor_camera.get_position().x, 25.0f));
    assert(near(scene.editor_camera.get_position().y, -10.0f));
    assert(near(scene.game_camera.get_position().x, 77.0f));

    assert(ViewportNavigation::zoom(&scene.editor_camera, 2.0f));
    assert(near(scene.editor_camera.get_zoom(), 2.0f));
    assert(ViewportNavigation::reset(&scene.editor_camera));
    assert(near(scene.editor_camera.get_position().x, 0.0f));
    assert(near(scene.editor_camera.get_position().y, 0.0f));
    assert(near(scene.editor_camera.get_zoom(), 1.0f));

    std::cout << "[PASS] test_navigation_uses_scene_render_camera_contract" << std::endl;
}

void test_navigation_frames_entities_without_generic_scene() {
    ContractScene scene;
    auto& manager = scene.get_entity_manager();

    const EntityID left = manager.create_entity();
    TransformComponent left_transform;
    left_transform.position = {-100.0f, 25.0f, 0.0f};
    manager.add_component<TransformComponent>(left, left_transform);

    const EntityID right = manager.create_entity();
    TransformComponent right_transform;
    right_transform.position = {100.0f, 25.0f, 0.0f};
    manager.add_component<TransformComponent>(right, right_transform);

    assert(ViewportNavigation::frame_entities(&scene, &scene.editor_camera, {left, right}, 0.0f));
    assert(near(scene.editor_camera.get_position().x, 0.0f));
    assert(near(scene.editor_camera.get_position().y, 25.0f));
    assert(scene.editor_camera.get_zoom() > 1.0f);
    assert(!ViewportNavigation::frame_entities(&scene, &scene.editor_camera, {999999}, 0.0f));

    std::cout << "[PASS] test_navigation_frames_entities_without_generic_scene" << std::endl;
}

int main() {
    test_visibility_filter_behavior();
    test_selection_tool_filtering();
    test_hierarchical_visibility_inheritance();
    test_canvas_ui_picking_matches_editor_preview();
    test_navigation_uses_scene_render_camera_contract();
    test_navigation_frames_entities_without_generic_scene();
    std::cout << "All ViewportVisibilityTests passed successfully!" << std::endl;
    return 0;
}
