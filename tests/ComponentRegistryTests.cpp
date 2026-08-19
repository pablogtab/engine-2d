#include "ecs/Components.hpp"
#include "ecs/EntityManager.hpp"
#include "scene/ComponentRegistry.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <typeindex>

namespace {

using namespace Engine;

template<std::size_t Index>
struct OverflowComponent {
    int value{static_cast<int>(Index)};
};

TypedComponentRegistration<TransformComponent> transform_registration(
    std::string key = "transform"
) {
    return {
        .display_name = "Transform",
        .serialized_key = std::move(key),
        .schema_version = 1,
        .required = true,
        .serialize = [](const TransformComponent&, const ComponentSerializationContext&) {
            return nlohmann::json::object();
        },
        .deserialize = [](
            TransformComponent&,
            const nlohmann::json&,
            const ComponentSerializationContext&,
            std::string&
        ) { return true; },
        .validate = {}
    };
}

TypedComponentRegistration<SpriteComponent> sprite_registration(
    std::string key = "sprite"
) {
    return {
        .display_name = "Sprite Renderer",
        .serialized_key = std::move(key),
        .schema_version = 1,
        .required = false,
        .serialize = [](const SpriteComponent&, const ComponentSerializationContext&) {
            return nlohmann::json::object();
        },
        .deserialize = [](
            SpriteComponent&,
            const nlohmann::json&,
            const ComponentSerializationContext&,
            std::string&
        ) { return true; },
        .validate = {}
    };
}

void test_registration_rejects_duplicate_keys_and_types() {
    ComponentRegistry registry;
    std::string error;
    assert(registry.register_component(transform_registration(), &error));
    assert(!registry.register_component(transform_registration("other_transform"), &error));
    assert(error == "Component runtime type is already registered");
    assert(!registry.register_component(sprite_registration("transform"), &error));
    assert(error == "Duplicate component serialized key: transform");
    assert(registry.descriptors().size() == 1);
}

void test_default_registry_metadata_is_stable() {
    const ComponentRegistry& registry = default_component_registry();
    const ComponentDescriptor* transform = registry.find_by_key("transform");
    const ComponentDescriptor* sprite = registry.find_by_type(typeid(SpriteComponent));
    assert(transform);
    assert(transform->display_name == "Transform");
    assert(transform->schema_version == 1);
    assert(transform->required);
    assert(sprite);
    assert(sprite->serialized_key == "sprite");
    assert(!sprite->required);
}

void test_registered_snapshot_capture_and_restore() {
    EntityManager em;
    const EntityID entity = em.create_entity();
    TransformComponent transform;
    transform.position = {12.0f, 34.0f, 5.0f};
    SpriteComponent sprite;
    sprite.size = {80.0f, 24.0f};
    sprite.has_outline = true;
    em.add_component<TransformComponent>(entity, transform);
    em.add_component<SpriteComponent>(entity, sprite);

    ComponentSnapshotSet snapshot;
    default_component_registry().capture_entity(em, entity, snapshot);
    assert(snapshot.size() == 2);
    assert(snapshot.contains<TransformComponent>());
    assert(snapshot.contains<SpriteComponent>());

    em.destroy_entity(entity);
    assert(em.create_entity_with_id(entity) == entity);
    default_component_registry().restore_entity(em, entity, snapshot);
    assert(em.get_component<TransformComponent>(entity).position == transform.position);
    assert(em.get_component<SpriteComponent>(entity).size == sprite.size);
    assert(em.get_component<SpriteComponent>(entity).has_outline);

    const EntityID clone = em.create_entity();
    default_component_registry().clone_entity_components(em, entity, clone);
    assert(em.get_component<TransformComponent>(clone).position == transform.position);
    assert(em.get_component<SpriteComponent>(clone).size == sprite.size);
}

void test_registered_add_and_remove_operations_respect_metadata() {
    EntityManager em;
    const EntityID entity = em.create_entity();
    std::string error;
    assert(default_component_registry().add_default_component("transform", em, entity, error));
    assert(default_component_registry().add_default_component("sprite", em, entity, error));
    assert(em.has_component<TransformComponent>(entity));
    assert(em.has_component<SpriteComponent>(entity));
    assert(default_component_registry().remove_component("sprite", em, entity, error));
    assert(!em.has_component<SpriteComponent>(entity));
    assert(!default_component_registry().remove_component("transform", em, entity, error));
    assert(error == "Component cannot be removed: transform");
    assert(em.has_component<TransformComponent>(entity));
}

void test_registered_json_round_trip_and_validation() {
    EntityManager em;
    const EntityID source = em.create_entity();
    TransformComponent transform;
    transform.position = {-8.0f, 16.0f, 2.0f};
    transform.scale = {2.0f, 3.0f};
    transform.rotation = 25.0f;
    SpriteComponent sprite;
    sprite.color = {10, 20, 30, 40};
    sprite.outline_thickness = 3.5f;
    em.add_component<TransformComponent>(source, transform);
    em.add_component<SpriteComponent>(source, sprite);

    nlohmann::json entity_json;
    std::string error;
    const ComponentSerializationContext context{};
    assert(default_component_registry().serialize_entity(
        em, source, entity_json, context, error
    ));
    assert(entity_json.contains("transform"));
    assert(entity_json.contains("sprite"));

    const EntityID target = em.create_entity();
    assert(default_component_registry().deserialize_entity(
        em, target, entity_json, context, error
    ));
    assert(em.get_component<TransformComponent>(target).position == transform.position);
    assert(em.get_component<TransformComponent>(target).world_position == transform.position);
    assert(em.get_component<SpriteComponent>(target).color.r == 10);
    assert(em.get_component<SpriteComponent>(target).outline_thickness == 3.5f);

    em.get_component<TransformComponent>(target).scale.x = 0.0f;
    const auto issues = default_component_registry().validate_entity(em, target);
    assert(issues.size() == 1);
    assert(issues.front().component_key == "transform");
}

void test_missing_required_component_is_rejected() {
    EntityManager em;
    const EntityID entity = em.create_entity();
    std::string error;
    nlohmann::json empty_entity = nlohmann::json::object();
    assert(!default_component_registry().deserialize_entity(
        em, entity, empty_entity, {}, error
    ));
    assert(error.find("missing required component 'transform'") != std::string::npos);
}

template<std::size_t... Indices>
void request_overflow_component_ids(std::index_sequence<Indices...>) {
    (static_cast<void>(get_component_type_id<OverflowComponent<Indices>>()), ...);
}

void test_component_type_limit_has_explicit_diagnostic() {
    bool rejected = false;
    try {
        request_overflow_component_ids(std::make_index_sequence<MAX_COMPONENTS + 1>{});
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()) == "ECS component type limit exceeded (64)";
    }
    assert(rejected);
}

} // namespace

int main() {
    test_registration_rejects_duplicate_keys_and_types();
    test_default_registry_metadata_is_stable();
    test_registered_snapshot_capture_and_restore();
    test_registered_add_and_remove_operations_respect_metadata();
    test_registered_json_round_trip_and_validation();
    test_missing_required_component_is_rejected();
    test_component_type_limit_has_explicit_diagnostic();
    std::cout << "ComponentRegistryTests passed\n";
    return 0;
}
