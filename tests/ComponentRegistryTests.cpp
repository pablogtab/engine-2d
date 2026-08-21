#include "ecs/components/AnimationComponents.hpp"
#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/components/UIComponents.hpp"
#include "renderer/material/MaterialAsset.hpp"
#include "renderer/material/ShaderAsset.hpp"
#include "renderer/material/Material.hpp"
#include "renderer/material/SDLGPUMaterialBackend.hpp"
#include "ecs/EntityManager.hpp"
#include "scene/ComponentRegistry.hpp"
#include "editor/inspector/EditorComponentRegistry.hpp"
#include "tilemap/Tilemap.hpp"
#include <cassert>
#include <filesystem>
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

EditorComponentDescriptor editor_registration(
    std::string key,
    std::string drawer_key,
    std::vector<std::string> dependencies = {},
    std::vector<std::string> incompatible = {}
) {
    return {
        .component_key = std::move(key),
        .drawer_key = std::move(drawer_key),
        .display_name = "Test Component",
        .dependencies = std::move(dependencies),
        .incompatible_components = std::move(incompatible),
        .draw = [](EntityManager&, EntityID, const EditorInspectorContext&) {},
        .create_default = [](
            EntityManager&,
            EntityID,
            const EditorInspectorContext&,
            ComponentSnapshotSet&
        ) {}
    };
}

EditorComponentDescriptor hidden_editor_registration(std::string key) {
    return {
        .component_key = std::move(key),
        .display_name = "Hidden Component",
        .show_in_inspector = false,
        .addable = false
    };
}

void test_editor_metadata_rejects_duplicate_keys_and_drawers() {
    EditorComponentRegistry registry;
    std::string error;
    assert(registry.register_component(editor_registration("transform", "TransformDrawer"), &error));
    assert(!registry.register_component(editor_registration("transform", "OtherDrawer"), &error));
    assert(error == "Duplicate editor component key: transform");
    assert(!registry.register_component(editor_registration("sprite", "TransformDrawer"), &error));
    assert(error == "Duplicate component drawer key: TransformDrawer");
}

void test_editor_metadata_validates_runtime_constraints() {
    ComponentRegistry runtime;
    std::string error;
    assert(runtime.register_component(transform_registration(), &error));
    assert(runtime.register_component(sprite_registration(), &error));

    EditorComponentRegistry valid;
    assert(valid.register_component(hidden_editor_registration("transform"), &error));
    assert(valid.register_component(editor_registration(
        "sprite", "SpriteDrawer", {"transform"}
    ), &error));
    assert(valid.validate(runtime, error));

    EditorComponentRegistry incomplete;
    assert(incomplete.register_component(editor_registration("sprite", "SpriteDrawer"), &error));
    assert(!incomplete.validate(runtime, error));
    assert(error == "Runtime component 'transform' is missing editor metadata");

    EditorComponentRegistry missing_dependency;
    assert(missing_dependency.register_component(hidden_editor_registration("transform"), &error));
    assert(missing_dependency.register_component(editor_registration(
        "sprite", "SpriteDrawer", {"missing"}
    ), &error));
    assert(!missing_dependency.validate(runtime, error));
    assert(error.find("depends on unknown component 'missing'") != std::string::npos);

    EditorComponentRegistry repeated_constraint;
    assert(repeated_constraint.register_component(hidden_editor_registration("transform"), &error));
    assert(repeated_constraint.register_component(editor_registration(
        "sprite", "SpriteDrawer", {"transform"}, {"transform"}
    ), &error));
    assert(!repeated_constraint.validate(runtime, error));
    assert(error.find("repeats constraint 'transform'") != std::string::npos);
}

void test_editor_default_snapshot_includes_dependency_chain() {
    EditorComponentRegistry registry;
    std::string error;
    auto transform = editor_registration("transform", "TransformDrawer");
    transform.addable = false;
    transform.create_default = [](
        EntityManager&, EntityID, const EditorInspectorContext&, ComponentSnapshotSet& snapshot
    ) { snapshot.set(TransformComponent{}); };
    auto layout = editor_registration("ui_layout", "UILayoutDrawer", {"transform"});
    layout.create_default = [](
        EntityManager&, EntityID, const EditorInspectorContext&, ComponentSnapshotSet& snapshot
    ) { snapshot.set(UILayoutComponent{}); };
    auto text = editor_registration("ui_text", "UITextDrawer", {"ui_layout"});
    text.create_default = [](
        EntityManager&, EntityID, const EditorInspectorContext&, ComponentSnapshotSet& snapshot
    ) { snapshot.set(UITextComponent{}); };
    auto image = editor_registration("ui_image", "UIImageDrawer", {"ui_layout"});
    image.create_default = [](
        EntityManager&, EntityID, const EditorInspectorContext&, ComponentSnapshotSet& snapshot
    ) { snapshot.set(UIImageComponent{}); };
    auto button = editor_registration("ui_button", "UIButtonDrawer", {"ui_image"});
    button.create_default = [](
        EntityManager&, EntityID, const EditorInspectorContext&, ComponentSnapshotSet& snapshot
    ) { snapshot.set(UIButtonComponent{}); };
    assert(registry.register_component(std::move(transform), &error));
    assert(registry.register_component(std::move(layout), &error));
    assert(registry.register_component(std::move(text), &error));
    assert(registry.register_component(std::move(image), &error));
    assert(registry.register_component(std::move(button), &error));

    EntityManager em;
    ComponentSnapshotSet snapshot;
    assert(registry.build_default_snapshot(
        "ui_text", em, 0, EditorInspectorContext{}, snapshot, error
    ));
    assert(snapshot.contains<TransformComponent>());
    assert(snapshot.contains<UILayoutComponent>());
    assert(snapshot.contains<UITextComponent>());
    assert(snapshot.size() == 3);

    ComponentSnapshotSet button_snapshot;
    assert(registry.build_default_snapshot(
        "ui_button", em, 0, EditorInspectorContext{}, button_snapshot, error
    ));
    assert(button_snapshot.contains<TransformComponent>());
    assert(button_snapshot.contains<UILayoutComponent>());
    assert(button_snapshot.contains<UIImageComponent>());
    assert(button_snapshot.contains<UIButtonComponent>());
    assert(button_snapshot.size() == 4);

    ComponentSnapshotSet invalid;
    assert(!registry.build_default_snapshot(
        "missing", em, 0, EditorInspectorContext{}, invalid, error
    ));
    assert(error == "Unknown editor component: missing");
}

void test_editor_component_availability_reports_dependencies_and_conflicts() {
    ComponentRegistry runtime;
    std::string error;
    assert(runtime.register_component(transform_registration(), &error));
    assert(runtime.register_component(sprite_registration(), &error));

    EditorComponentRegistry editor;
    assert(editor.register_component(editor_registration(
        "sprite", "SpriteDrawer", {"transform"}
    ), &error));
    const EditorComponentDescriptor& sprite = editor.descriptors().front();
    EntityManager em;
    const EntityID entity = em.create_entity();

    auto availability = editor.availability(sprite, runtime, em, entity);
    assert(!availability.available);
    assert(availability.reason == "Requires transform");
    em.add_component<TransformComponent>(entity, TransformComponent{});
    availability = editor.availability(sprite, runtime, em, entity);
    assert(availability.available);
    em.add_component<SpriteComponent>(entity, SpriteComponent{});
    availability = editor.availability(sprite, runtime, em, entity);
    assert(!availability.available);
    assert(availability.reason == "Entity already has this component");

    EditorComponentRegistry conflicting;
    assert(conflicting.register_component(editor_registration(
        "sprite", "ConflictingSpriteDrawer", {}, {"transform"}
    ), &error));
    availability = conflicting.availability(conflicting.descriptors().front(), runtime, em, entity);
    assert(!availability.available);
    assert(availability.reason == "Entity already has this component");
    em.remove_component<SpriteComponent>(entity);
    availability = conflicting.availability(conflicting.descriptors().front(), runtime, em, entity);
    assert(!availability.available);
    assert(availability.reason == "Incompatible with transform");
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

void test_component_migration_runs_for_every_entity() {
    ComponentRegistry registry;
    auto registration = transform_registration();
    registration.schema_version = 3;
    int migration_calls = 0;
    registration.migrate = [&migration_calls](
        std::uint32_t from_version,
        nlohmann::json& payload,
        std::string&
    ) {
        payload["last_migrated_from"] = from_version;
        ++migration_calls;
        return true;
    };
    std::string error;
    assert(registry.register_component(std::move(registration), &error));

    const nlohmann::json versions{{"transform", 1}};
    nlohmann::json first{{"transform", nlohmann::json::object()}};
    nlohmann::json second{{"transform", nlohmann::json::object()}};
    assert(registry.migrate_entity_components(first, versions, error));
    assert(registry.migrate_entity_components(second, versions, error));
    assert(migration_calls == 4);
    assert(first.at("transform").at("last_migrated_from") == 2);
    assert(second.at("transform").at("last_migrated_from") == 2);
    assert(versions.at("transform") == 1);
}

void test_component_migration_rejects_missing_and_future_versions() {
    ComponentRegistry registry;
    auto registration = transform_registration();
    registration.schema_version = 2;
    std::string error;
    assert(registry.register_component(std::move(registration), &error));
    nlohmann::json entity{{"transform", nlohmann::json::object()}};

    assert(!registry.migrate_entity_components(entity, nlohmann::json::object(), error));
    assert(error.find("Missing or invalid schema version") != std::string::npos);
    error.clear();
    assert(!registry.migrate_entity_components(entity, nlohmann::json{{"transform", 3}}, error));
    assert(error.find("future schema version") != std::string::npos);
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
    assert(registry.descriptors().size() == 18);
    assert(registry.find_by_type(typeid(TagComponent)));
    assert(registry.find_by_type(typeid(GroupComponent)));
    assert(registry.find_by_type(typeid(RenderEffectComponent)));
    assert(registry.find_by_type(typeid(Light2DComponent)));
    assert(registry.find_by_type(typeid(ShadowCaster2DComponent)));
    assert(registry.find_by_type(typeid(PrefabLinkComponent)));
    assert(registry.find_by_type(typeid(CameraComponent)));
    assert(registry.find_by_type(typeid(CanvasComponent)));
    assert(registry.find_by_type(typeid(UILayoutComponent)));
    assert(registry.find_by_type(typeid(UITextComponent)));
    assert(registry.find_by_type(typeid(UIImageComponent)));
    assert(registry.find_by_type(typeid(UIButtonComponent)));
    assert(registry.find_by_type(typeid(AnimationComponent)));
    assert(registry.find_by_type(typeid(RigidBodyComponent)));
    assert(registry.find_by_type(typeid(ColliderComponent)));
    assert(registry.find_by_type(typeid(TilemapComponent)));
}


void test_registered_snapshot_capture_and_restore() {
    EntityManager em;
    const EntityID entity = em.create_entity();
    TransformComponent transform;
    transform.position = {12.0f, 34.0f, 5.0f};
    SpriteComponent sprite;
    sprite.size = {80.0f, 24.0f};
    sprite.has_outline = true;
    sprite.render_layer = 4;
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
    sprite.render_layer = 4;
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
    assert(em.get_component<SpriteComponent>(target).render_layer == 4);

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

void test_builtin_components_round_trip_through_registry() {
    EntityManager em;
    const EntityID source = em.create_entity();
    em.add_component<TransformComponent>(source, TransformComponent{});
    em.add_component<TagComponent>(source, TagComponent{"RegistryEntity"});
    em.add_component<GroupComponent>(source, GroupComponent{});
    em.add_component<SpriteComponent>(source, SpriteComponent{});
    em.add_component<RenderEffectComponent>(source, RenderEffectComponent{AssetHandle{0x1234}, false});
    Light2DComponent light;
    light.type = Light2DType::Point;
    light.color = {40, 80, 160, 220};
    light.intensity = 0.65f;
    light.radius = 480.0f;
    light.falloff = 0.75f;
    light.blend_mode = Light2DBlendMode::Screen;
    light.affected_layers = render_layer_mask(2) | render_layer_mask(5);
    light.casts_shadows = false;
    em.add_component<Light2DComponent>(source, light);

    ShadowCaster2DComponent caster;
    caster.source = ShadowGeometrySource::StaticPolygon;
    caster.vertices = {{-10.0f, -8.0f}, {12.0f, -6.0f}, {0.0f, 16.0f}};
    caster.affected_layers = render_layer_mask(5);
    em.add_component<ShadowCaster2DComponent>(source, caster);

    CameraComponent camera;
    camera.clear_color = {12, 34, 56, 78};
    camera.zoom = 2.5f;
    camera.projection = CameraProjectionMode::Expand;
    camera.follow_offset = {3.0f, 4.0f};
    em.add_component<CameraComponent>(source, camera);

    CanvasComponent canvas;
    canvas.reference_size = {1920.0f, 1080.0f};
    canvas.scaling_policy = PresentationMode::Fill;
    canvas.sorting_order = 4;
    canvas.use_safe_area = false;
    em.add_component<CanvasComponent>(source, canvas);

    UILayoutComponent ui_layout;
    ui_layout.anchor_min = {0.0f, 0.5f};
    ui_layout.anchor_max = {1.0f, 0.5f};
    ui_layout.pivot = {0.25f, 0.75f};
    ui_layout.anchored_position = {12.0f, -8.0f};
    ui_layout.size_delta = {-40.0f, 72.0f};
    em.add_component<UILayoutComponent>(source, ui_layout);

    UITextComponent ui_text;
    ui_text.text = "Play";
    ui_text.font_handle = AssetHandle{0x4567};
    ui_text.material_handle = AssetHandle{0x4568};
    ui_text.font_size = 36.0f;
    ui_text.color = {20, 40, 80, 220};
    ui_text.horizontal_alignment = UIHorizontalAlignment::Right;
    ui_text.vertical_alignment = UIVerticalAlignment::Bottom;
    ui_text.margins.left = {8.0f, UILengthUnit::Pixels};
    ui_text.margins.top = {12.0f, UILengthUnit::Percent};
    ui_text.margins.right = {-16.0f, UILengthUnit::Pixels};
    ui_text.margins.bottom = {-20.0f, UILengthUnit::Percent};
    ui_text.fit_content_width = true;
    ui_text.fit_content_height = true;
    ui_text.fit_padding = {4.0f, 6.0f, 8.0f, 10.0f};
    ui_text.wrap = true;
    em.add_component<UITextComponent>(source, ui_text);

    UIImageComponent ui_image;
    ui_image.texture_handle = AssetHandle{0x5678};
    ui_image.material_handle = AssetHandle{0x5679};
    ui_image.color = {120, 140, 160, 180};
    ui_image.scale_mode = UIImageScaleMode::Fit;
    ui_image.raycast_target = false;
    em.add_component<UIImageComponent>(source, ui_image);

    UIButtonComponent ui_button;
    ui_button.normal_color = {10, 20, 30, 255};
    ui_button.hovered_color = {40, 50, 60, 255};
    ui_button.pressed_color = {70, 80, 90, 255};
    ui_button.disabled_color = {100, 110, 120, 128};
    ui_button.interactable = false;
    em.add_component<UIButtonComponent>(source, ui_button);

    AnimationComponent animation;
    animation.columns = 4;
    animation.rows = 2;
    animation.default_clip_index = 0;
    animation.add_clip("Run", 2, 6, 12.0f, true);
    em.add_component<AnimationComponent>(source, animation);

    RigidBodyComponent body = RigidBodyComponent::Dynamic(0.75f, 0.1f, 0.6f, false);
    body.mass = 3.0f;
    em.add_component<RigidBodyComponent>(source, body);
    em.add_component<ColliderComponent>(source, ColliderComponent::Circle(14.0f, {2.0f, -1.0f}, true));

    TilemapComponent tilemap;
    tilemap.generate_colliders = false;
    tilemap.tile_size = 24.0f;
    tilemap.set_tile(-2, 5, 7);
    em.add_component<TilemapComponent>(source, tilemap);

    nlohmann::json value;
    std::string error;
    assert(default_component_registry().serialize_entity(em, source, value, {}, error));
    for (const char* key : {"tag", "group", "render_effect", "light_2d", "shadow_caster_2d",
                            "camera", "canvas", "ui_layout", "ui_text", "ui_image", "ui_button",
                            "animation", "rigidbody", "collider", "tilemap"}) {
        assert(value.contains(key));
    }

    const EntityID target = em.create_entity();
    assert(default_component_registry().deserialize_entity(em, target, value, {}, error));
    assert(default_component_registry().finalize_entity(
        em,
        target,
        ComponentFinalizeContext{.reason = ComponentFinalizeReason::DeserializationCommit},
        error
    ));
    assert(em.get_component<TagComponent>(target).tag == "RegistryEntity");
    assert(em.has_component<GroupComponent>(target));
    assert(em.get_component<RenderEffectComponent>(target) == RenderEffectComponent(AssetHandle{0x1234}, false));
    assert(em.get_component<Light2DComponent>(target) == light);
    assert(em.get_component<ShadowCaster2DComponent>(target) == caster);
    assert(em.get_component<CameraComponent>(target).zoom == 2.5f);
    assert(em.get_component<CameraComponent>(target).clear_color.r == 12);
    assert(em.get_component<CameraComponent>(target).clear_color.a == 78);
    assert(em.get_component<CanvasComponent>(target) == canvas);
    assert(em.get_component<UILayoutComponent>(target) == ui_layout);
    assert(em.get_component<UITextComponent>(target) == ui_text);
    assert(em.get_component<UIImageComponent>(target) == ui_image);
    assert(em.get_component<UIButtonComponent>(target) == ui_button);
    assert(em.get_component<AnimationComponent>(target).clips.front().name == "Run");
    assert(em.get_component<RigidBodyComponent>(target).mass == 3.0f);
    assert(em.get_component<ColliderComponent>(target).shapes.front().radius == 14.0f);
    assert(em.get_component<TilemapComponent>(target).get_tile(-2, 5) == 7);

    ComponentSnapshotSet snapshot;
    default_component_registry().capture_entity(em, source, snapshot);
    const EntityID restored = em.create_entity();
    default_component_registry().restore_entity(em, restored, snapshot);
    assert(default_component_registry().finalize_entity(
        em,
        restored,
        ComponentFinalizeContext{.reason = ComponentFinalizeReason::SnapshotRestore},
        error
    ));
    assert(snapshot.size() + 1 == default_component_registry().descriptors().size());
    assert(em.get_component<TagComponent>(restored).tag == "RegistryEntity");
    assert(em.has_component<GroupComponent>(restored));
    assert(em.get_component<RenderEffectComponent>(restored) == RenderEffectComponent(AssetHandle{0x1234}, false));
    assert(em.get_component<Light2DComponent>(restored) == light);
    assert(em.get_component<ShadowCaster2DComponent>(restored) == caster);
    assert(em.get_component<CameraComponent>(restored).projection == CameraProjectionMode::Expand);
    assert(em.get_component<CameraComponent>(restored).clear_color.g == 34);
    assert(em.get_component<CanvasComponent>(restored) == canvas);
    assert(em.get_component<UILayoutComponent>(restored) == ui_layout);
    assert(em.get_component<UITextComponent>(restored) == ui_text);
    assert(em.get_component<UIImageComponent>(restored) == ui_image);
    assert(em.get_component<UIButtonComponent>(restored) == ui_button);
    assert(em.get_component<AnimationComponent>(restored).clips.size() == 1);
    assert(em.get_component<RigidBodyComponent>(restored).body_type == BodyType::Dynamic);
    assert(em.get_component<ColliderComponent>(restored).shapes.size() == 1);
    assert(em.get_component<TilemapComponent>(restored).get_tile(-2, 5) == 7);
}


void test_finalization_runs_after_entity_references_exist() {
    EntityManager em;
    const EntityID camera_entity = em.create_entity_with_id(1);
    em.add_component<TransformComponent>(camera_entity, TransformComponent{});

    nlohmann::json camera_json{
        {"is_primary", true}, {"clear_color", {24, 28, 36, 255}}, {"zoom", 1.0f}, {"projection", "fixed_height"},
        {"follow_target", 2}, {"follow_offset", {0.0f, 0.0f}},
        {"follow_speed", 6.0f}, {"enable_bounds", false},
        {"bounds", {-10.0f, -10.0f, 10.0f, 10.0f}}
    };
    std::string error;
    const auto* descriptor = default_component_registry().find_by_key("camera");
    assert(descriptor);
    assert(descriptor->deserialize(em, camera_entity, camera_json, {}, error));
    assert(!default_component_registry().finalize_entity(
        em, camera_entity, ComponentFinalizeContext{}, error
    ));
    assert(error.find("follow target") != std::string::npos);

    const EntityID target = em.create_entity_with_id(2);
    em.add_component<TransformComponent>(target, TransformComponent{});
    error.clear();
    assert(default_component_registry().finalize_entity(
        em, camera_entity, ComponentFinalizeContext{}, error
    ));
}

void test_animation_lifecycle_clears_runtime_playback() {
    EntityManager em;
    const EntityID entity = em.create_entity();
    em.add_component<TransformComponent>(entity, TransformComponent{});
    em.add_component<AnimationComponent>(entity, AnimationComponent{});
    em.add_component<AnimationPlaybackStateComponent>(entity, AnimationPlaybackStateComponent{});

    std::string error;
    assert(default_component_registry().finalize_entity(
        em, entity, ComponentFinalizeContext{.reason = ComponentFinalizeReason::SnapshotRestore}, error
    ));
    assert(!em.has_component<AnimationPlaybackStateComponent>(entity));

    em.add_component<AnimationPlaybackStateComponent>(entity, AnimationPlaybackStateComponent{});
    assert(default_component_registry().remove_component("animation", em, entity, error));
    assert(!em.has_component<AnimationComponent>(entity));
    assert(!em.has_component<AnimationPlaybackStateComponent>(entity));
}

void test_prefab_links_persist_asset_handles() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_component_registry_asset_handles";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root / "assets/prefabs");
    assert(FileSystem::write_text((root / "assets/prefabs/Hero.prefab").string(), "{}"));

    AssetDatabase database(root);
    [[maybe_unused]] const AssetScanResult scan = database.scan(root / "assets");
    assert(database.find_by_path("assets/prefabs/Hero.prefab"));
    EntityManager em;
    const EntityID source = em.create_entity();
    em.add_component<TransformComponent>(source, TransformComponent{});
    em.add_component<PrefabLinkComponent>(source, PrefabLinkComponent{"assets/prefabs/Hero.prefab"});
    ComponentSerializationContext context{.asset_database = &database};
    nlohmann::json serialized;
    std::string error;
    assert(default_component_registry().serialize_entity(em, source, serialized, context, error));
    assert(serialized["prefab_link"].contains("asset_handle"));
    assert(!serialized["prefab_link"].contains("asset_path"));

    const fs::path renamed = root / "assets/prefabs/HeroRenamed.prefab";
    fs::rename(root / "assets/prefabs/Hero.prefab", renamed, ignored);
    assert(!ignored);
    assert(database.move_asset("assets/prefabs/Hero.prefab", "assets/prefabs/HeroRenamed.prefab"));

    const EntityID target = em.create_entity();
    assert(default_component_registry().deserialize_entity(em, target, serialized, context, error));
    assert(em.get_component<PrefabLinkComponent>(target).prefab_asset_path == "assets/prefabs/HeroRenamed.prefab");
    fs::remove_all(root, ignored);
}

void test_sprite_textures_persist_asset_handles() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_sprite_asset_handles";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root / "assets/textures");
    assert(FileSystem::write_text((root / "assets/textures/Icon.png").string(), "not decoded in this registry test"));

    AssetDatabase database(root);
    [[maybe_unused]] const AssetScanResult scan = database.scan(root / "assets");
    const AssetRecord* texture_record = database.find_by_path("assets/textures/Icon.png");
    assert(texture_record && texture_record->type == AssetType::Texture);
    const std::uint32_t pixel = 0xffffffff;
    Texture texture(nullptr, 1, 1, &pixel);
    texture.set_filepath("assets/textures/Icon.png");

    EntityManager em;
    const EntityID source = em.create_entity();
    em.add_component<TransformComponent>(source, TransformComponent{});
    SpriteComponent sprite{&texture};
    sprite.texture_handle = texture_record->handle;
    em.add_component<SpriteComponent>(source, sprite);
    ComponentSerializationContext context{.asset_database = &database};
    nlohmann::json serialized;
    std::string error;
    assert(default_component_registry().serialize_entity(em, source, serialized, context, error));
    assert(serialized["sprite"].contains("texture_handle"));
    assert(!serialized["sprite"].contains("texture"));
    const EntityID target = em.create_entity();
    assert(default_component_registry().deserialize_entity(em, target, serialized, context, error));
    assert(em.get_component<SpriteComponent>(target).texture_handle == texture_record->handle);

    nlohmann::json missing_asset_serialized = serialized;
    missing_asset_serialized["sprite"]["texture_handle"] = AssetHandle{999}.to_string();
    missing_asset_serialized["sprite"]["material_handle"] = AssetHandle{1000}.to_string();
    const EntityID missing_target = em.create_entity();
    assert(default_component_registry().deserialize_entity(em, missing_target, missing_asset_serialized, context, error));
    const auto& missing_sprite = em.get_component<SpriteComponent>(missing_target);
    assert(missing_sprite.texture_handle == AssetHandle{999});
    assert(missing_sprite.material_handle == AssetHandle{1000});

    fs::remove_all(root, ignored);
}

void test_material_assets_load_by_handle() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_material_asset_handles";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root / "assets/shaders");
    fs::create_directories(root / "assets/materials");
    assert(FileSystem::write_text((root / "assets/shaders/Test.msl").string(), "fragment float4 main0() { return float4(1); }\n"));

    AssetDatabase database(root);
    [[maybe_unused]] const AssetScanResult shader_scan = database.scan(root / "assets");
    const AssetRecord* source = database.find_by_path("assets/shaders/Test.msl");
    assert(source && source->status == AssetStatus::Ready);
    const AssetHandle source_handle = source->handle;
    const nlohmann::json shader_json{
        {"name", "Test Shader"},
        {"sampler_count", 1},
        {"uniform_buffers", nlohmann::json::array({{{"name", "TestUniforms"}, {"size", 4}}})},
        {"variants", nlohmann::json::array({{{"format", "msl"}, {"source_handle", source_handle.to_string()}, {"entry_point", "main0"}}})}
    };
    assert(FileSystem::write_text((root / "assets/shaders/Test.shader").string(), shader_json.dump()));
    const auto shader_handle = database.import_asset(root / "assets/shaders/Test.shader");
    assert(shader_handle);
    const nlohmann::json material_json{
        {"name", "Test Material"},
        {"shader_handle", shader_handle->to_string()},
        {"uniforms", nlohmann::json::array({nlohmann::json::array({1, 2, 3, 4})})}
    };
    assert(FileSystem::write_text((root / "assets/materials/Test.material").string(), material_json.dump()));
    assert(database.import_asset(root / "assets/materials/Test.material"));
    const AssetRecord* material = database.find_by_path("assets/materials/Test.material");
    assert(material && material->type == AssetType::Material);
    const auto loaded = MaterialAsset::load(material->handle, database);
    assert(loaded && loaded->name() == "Test Material");
    assert(loaded->uniform_bytes(0).size() == 4);
    assert(loaded->shader().variants().front().entry_point == "main0");
    auto description = MaterialAsset::read(material->handle, database);
    assert(description && description->shader_handle == *shader_handle);
    description->name = "Edited Material";
    description->uniforms[0] = {std::byte{9}, std::byte{8}};
    std::string error;
    assert(!MaterialAsset::save(material->handle, *description, database, &error));
    assert(error.find("must contain 4 bytes") != std::string::npos);
    description->uniforms[0] = {std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6}};
    assert(MaterialAsset::save(material->handle, *description, database));
    const auto edited = MaterialAsset::load(material->handle, database);
    assert(edited && edited->name() == "Edited Material");
    assert(edited->uniform_bytes(0).size() == 4);

    EntityManager entities;
    const EntityID entity = entities.create_entity();
    SpriteComponent sprite;
    sprite.material_handle = material->handle;
    sprite.material = edited;
    entities.add_component<SpriteComponent>(entity, sprite);
    UITextComponent text;
    text.material_handle = material->handle;
    text.material = edited;
    entities.add_component<UITextComponent>(entity, text);
    UIImageComponent image;
    image.material_handle = material->handle;
    image.material = edited;
    entities.add_component<UIImageComponent>(entity, image);
    const auto initial_refresh = MaterialAsset::refresh_assigned(entities, database);
    assert(initial_refresh.reloaded == 3);
    assert(entities.get_component<SpriteComponent>(entity).material);
    assert(entities.get_component<UITextComponent>(entity).material);
    assert(entities.get_component<UIImageComponent>(entity).material);

    fs::remove(root / "assets/shaders/Test.msl", ignored);
    [[maybe_unused]] const auto missing_scan = database.rescan();
    const auto unavailable_refresh = MaterialAsset::refresh_assigned(entities, database);
    assert(unavailable_refresh.unavailable == 3);
    assert(!entities.get_component<SpriteComponent>(entity).material);
    assert(!entities.get_component<UITextComponent>(entity).material);
    assert(!entities.get_component<UIImageComponent>(entity).material);
    assert(entities.get_component<SpriteComponent>(entity).material_handle == material->handle);

    assert(FileSystem::write_text((root / "assets/shaders/Test.msl").string(), "fragment float4 main0() { return float4(1); }\n"));
    [[maybe_unused]] const auto restored_scan = database.rescan();
    const auto restored_refresh = MaterialAsset::refresh_assigned(entities, database);
    assert(restored_refresh.reloaded == 3);
    assert(entities.get_component<SpriteComponent>(entity).material);
    assert(entities.get_component<UITextComponent>(entity).material);
    assert(entities.get_component<UIImageComponent>(entity).material);

    fs::remove_all(root, ignored);
}

void test_shader_preflight_reports_missing_renderer() {
    FragmentShader shader("Test");
    std::string error;
    assert(!SDLGPUMaterialBackend::supports(nullptr, shader, &error));
    assert(error == "The current renderer does not provide an SDL GPU shader backend");
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
    test_editor_metadata_rejects_duplicate_keys_and_drawers();
    test_editor_metadata_validates_runtime_constraints();
    test_editor_default_snapshot_includes_dependency_chain();
    test_editor_component_availability_reports_dependencies_and_conflicts();
    test_registration_rejects_duplicate_keys_and_types();
    test_component_migration_runs_for_every_entity();
    test_component_migration_rejects_missing_and_future_versions();
    test_default_registry_metadata_is_stable();
    test_registered_snapshot_capture_and_restore();
    test_registered_add_and_remove_operations_respect_metadata();
    test_registered_json_round_trip_and_validation();
    test_missing_required_component_is_rejected();
    test_builtin_components_round_trip_through_registry();
    test_prefab_links_persist_asset_handles();
    test_sprite_textures_persist_asset_handles();
    test_material_assets_load_by_handle();
    test_shader_preflight_reports_missing_renderer();
    test_finalization_runs_after_entity_references_exist();
    test_animation_lifecycle_clears_runtime_playback();
    test_component_type_limit_has_explicit_diagnostic();
    std::cout << "ComponentRegistryTests passed\n";
    return 0;
}
