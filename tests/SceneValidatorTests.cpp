#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "scene/SceneValidator.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>

namespace {

using namespace Engine;

bool contains_code(const std::vector<SceneDiagnostic>& diagnostics, SceneDiagnosticCode code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [code](const SceneDiagnostic& diagnostic) {
        return diagnostic.code == code;
    });
}

void test_valid_scene_has_no_errors() {
    EntityManager entities;
    const EntityID parent = entities.create_entity();
    const EntityID child = entities.create_entity();
    entities.add_component<HierarchyComponent>(parent, HierarchyComponent{});
    entities.add_component<HierarchyComponent>(child, HierarchyComponent{parent});
    entities.get_component<HierarchyComponent>(parent).children.push_back(child);

    const EntityID camera = entities.create_entity();
    CameraComponent primary_camera;
    primary_camera.is_primary = true;
    entities.add_component<CameraComponent>(camera, primary_camera);

    const auto diagnostics = SceneValidator::validate(entities);
    assert(diagnostics.empty());
    assert(!SceneValidator::has_errors(diagnostics));
}

void test_missing_primary_camera_is_warning() {
    EntityManager entities;
    entities.create_entity();

    const auto diagnostics = SceneValidator::validate(entities);
    assert(diagnostics.size() == 1);
    assert(diagnostics.front().code == SceneDiagnosticCode::MissingPrimaryCamera);
    assert(diagnostics.front().severity == SceneDiagnosticSeverity::Warning);
    assert(!SceneValidator::has_errors(diagnostics));
}

void test_duplicate_primary_cameras_are_errors() {
    EntityManager entities;
    for (int i = 0; i < 2; ++i) {
        const EntityID camera = entities.create_entity();
        CameraComponent component;
        component.is_primary = true;
        entities.add_component<CameraComponent>(camera, component);
    }

    const auto diagnostics = SceneValidator::validate(entities);
    assert(diagnostics.size() == 2);
    assert(contains_code(diagnostics, SceneDiagnosticCode::DuplicatePrimaryCamera));
    assert(SceneValidator::has_errors(diagnostics));
}

void test_hierarchy_errors_are_reported_without_mutation() {
    EntityManager entities;
    const EntityID parent = entities.create_entity();
    const EntityID child = entities.create_entity();
    entities.add_component<HierarchyComponent>(parent, HierarchyComponent{});
    entities.add_component<HierarchyComponent>(child, HierarchyComponent{9999});
    entities.get_component<HierarchyComponent>(parent).children = {child, 8888};

    const auto diagnostics = SceneValidator::validate(entities);
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingHierarchyParent));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingHierarchyChild));
    assert(contains_code(diagnostics, SceneDiagnosticCode::HierarchyLinkMismatch));
    assert(SceneValidator::has_errors(diagnostics));
    assert(entities.get_component<HierarchyComponent>(child).parent == 9999);
    assert(entities.get_component<HierarchyComponent>(parent).children == std::vector<EntityID>({child, 8888}));
}

void test_hierarchy_cycles_are_reported() {
    EntityManager entities;
    const EntityID first = entities.create_entity();
    const EntityID second = entities.create_entity();
    entities.add_component<HierarchyComponent>(first, HierarchyComponent{second});
    entities.add_component<HierarchyComponent>(second, HierarchyComponent{first});
    entities.get_component<HierarchyComponent>(first).children = {second};
    entities.get_component<HierarchyComponent>(second).children = {first};

    const auto diagnostics = SceneValidator::validate(entities);
    assert(contains_code(diagnostics, SceneDiagnosticCode::HierarchyCycle));
    assert(SceneValidator::has_errors(diagnostics));
}

} // namespace

int main() {
    test_valid_scene_has_no_errors();
    test_missing_primary_camera_is_warning();
    test_duplicate_primary_cameras_are_errors();
    test_hierarchy_errors_are_reported_without_mutation();
    test_hierarchy_cycles_are_reported();
    std::cout << "SceneValidatorTests passed\n";
    return 0;
}
