#include <cassert>
#include <iostream>
#include "enginescript/di/DependencyInjection.hpp"

using namespace EngineScript;

struct FakeInputService {};
struct FakeAudioService {};

int main() {
    std::cout << "[TEST] Running EngineScriptDITests...\n";

    DIContainer container;

    // Test 1: Register and resolve application services
    FakeInputService fake_input;
    container.register_instance("Input", &fake_input, DIScopeLevel::Application);

    FakeAudioService fake_audio;
    container.register_instance("Audio", &fake_audio, DIScopeLevel::Application);

    assert(container.resolve_service("Input") == &fake_input);
    assert(container.resolve_service("Audio") == &fake_audio);
    assert(container.resolve_service("NonExistent") == nullptr);

    // Test 2: Dependency cycle detection
    ServiceDescriptor s_a;
    s_a.type_name = "ServiceA";
    s_a.dependencies = {"ServiceB"};
    container.register_service(s_a);

    ServiceDescriptor s_b;
    s_b.type_name = "ServiceB";
    s_b.dependencies = {"ServiceA"};
    container.register_service(s_b);

    std::vector<std::string> cycle;
    assert(container.check_service_cycles(&cycle));
    assert(cycle.size() >= 2);

    // Test 3: Behaviour entity requirements validation
    EntityContext valid_entity;
    valid_entity.entity_id = 42;
    valid_entity.attached_components = {"RigidBody2D", "SpriteRenderer2D"};

    auto res_valid = container.validate_behaviour_requirements(
        "PlatformerPlayer",
        {{"input", "Input"}},
        {{"body", "RigidBody2D"}},
        {{"animator", "Animator2D"}},
        valid_entity
    );
    assert(res_valid.is_valid);
    assert(res_valid.errors.empty());

    // Entity missing required RigidBody2D
    EntityContext invalid_entity;
    invalid_entity.entity_id = 43;
    invalid_entity.attached_components = {"SpriteRenderer2D"}; // Missing RigidBody2D!

    auto res_invalid = container.validate_behaviour_requirements(
        "PlatformerPlayer",
        {{"input", "Input"}},
        {{"body", "RigidBody2D"}},
        {{"animator", "Animator2D"}},
        invalid_entity
    );
    assert(!res_invalid.is_valid);
    assert(res_invalid.missing_required_components.size() == 1);
    assert(res_invalid.missing_required_components[0] == "RigidBody2D");

    std::cout << "[PASS] EngineScriptDITests passed!\n";
    return 0;
}
