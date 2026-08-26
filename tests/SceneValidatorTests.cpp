#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/UIComponents.hpp"
#include "ecs/components/ScriptingComponents.hpp"
#include "core/AssetDatabase.hpp"
#include "scene/SceneValidator.hpp"
#include "tilemap/Tilemap.hpp"

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
    const auto missing_child = std::find_if(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.code == SceneDiagnosticCode::MissingHierarchyChild;
    });
    assert(missing_child != diagnostics.end());
    assert(missing_child->related_entity == 8888);
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

void test_missing_assets_are_reported_when_a_database_is_available() {
    EntityManager entities;
    AssetDatabase asset_database;

    const EntityID sprite_entity = entities.create_entity();
    SpriteComponent sprite;
    sprite.texture_handle = AssetHandle{101};
    sprite.material_handle = AssetHandle{102};
    entities.add_component<SpriteComponent>(sprite_entity, sprite);
    entities.add_component<RenderEffectComponent>(
        sprite_entity, RenderEffectComponent{AssetHandle{103}, true}
    );
    ScriptComponent script;
    script.instances.front().script_handle = AssetHandle{108};
    entities.add_component<ScriptComponent>(sprite_entity, script);

    const EntityID prefab_entity = entities.create_entity();
    entities.add_component<PrefabLinkComponent>(prefab_entity, PrefabLinkComponent{"assets/prefabs/missing.prefab"});

    const EntityID tilemap_entity = entities.create_entity();
    TilemapComponent tilemap;
    tilemap.tileset_path = "assets/textures/missing.png";
    entities.add_component<TilemapComponent>(tilemap_entity, tilemap);

    const EntityID image_entity = entities.create_entity();
    UIImageComponent image;
    image.texture_handle = AssetHandle{104};
    image.material_handle = AssetHandle{106};
    entities.add_component<UIImageComponent>(image_entity, image);

    const EntityID text_entity = entities.create_entity();
    UITextComponent text;
    text.font_handle = AssetHandle{105};
    text.material_handle = AssetHandle{107};
    entities.add_component<UITextComponent>(text_entity, text);

    const auto diagnostics = SceneValidator::validate(entities, &asset_database);
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingTextureAsset));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingMaterialAsset));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingRenderEffectAsset));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingScriptAsset));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingPrefabAsset));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingTilemapTileset));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingUIImageTexture));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingUIFont));
    const auto missing_texture = std::find_if(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.code == SceneDiagnosticCode::MissingTextureAsset;
    });
    assert(missing_texture != diagnostics.end());
    assert(missing_texture->asset_handle == AssetHandle{101});
    const auto missing_material = std::find_if(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.code == SceneDiagnosticCode::MissingMaterialAsset;
    });
    assert(missing_material != diagnostics.end());
    assert(missing_material->asset_handle == AssetHandle{102});
    const auto missing_effect = std::find_if(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.code == SceneDiagnosticCode::MissingRenderEffectAsset;
    });
    assert(missing_effect != diagnostics.end());
    assert(missing_effect->asset_handle == AssetHandle{103});
    const auto missing_image = std::find_if(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.code == SceneDiagnosticCode::MissingUIImageTexture;
    });
    assert(missing_image != diagnostics.end());
    assert(missing_image->asset_handle == AssetHandle{104});
    const auto missing_font = std::find_if(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.code == SceneDiagnosticCode::MissingUIFont;
    });
    assert(missing_font != diagnostics.end());
    assert(missing_font->asset_handle == AssetHandle{105});
    const auto missing_prefab = std::find_if(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.code == SceneDiagnosticCode::MissingPrefabAsset;
    });
    assert(missing_prefab != diagnostics.end());
    assert(missing_prefab->asset_path == "assets/prefabs/missing.prefab");
    const auto missing_tileset = std::find_if(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.code == SceneDiagnosticCode::MissingTilemapTileset;
    });
    assert(missing_tileset != diagnostics.end());
    assert(missing_tileset->asset_path == "assets/textures/missing.png");
}

void test_invalid_colliders_and_tilemaps_are_reported() {
    EntityManager entities;

    const EntityID collider_entity = entities.create_entity();
    entities.add_component<ColliderComponent>(collider_entity, ColliderComponent::Box({0.0f, 10.0f}));

    const EntityID tilemap_entity = entities.create_entity();
    TilemapComponent tilemap;
    tilemap.tile_size = 0.0f;
    tilemap.tileset_cols = 0;
    entities.add_component<TilemapComponent>(tilemap_entity, tilemap);

    const auto diagnostics = SceneValidator::validate(entities);
    assert(contains_code(diagnostics, SceneDiagnosticCode::InvalidColliderShape));
    assert(contains_code(diagnostics, SceneDiagnosticCode::InvalidTilemapDimensions));
}

void test_invalid_lighting_is_reported() {
    EntityManager entities;

    const EntityID light_entity = entities.create_entity();
    Light2DComponent light;
    light.range = 0.0f;
    light.intensity = 2.0f;
    light.affected_layers = 0;
    entities.add_component<Light2DComponent>(light_entity, light);

    const EntityID collider_caster = entities.create_entity();
    entities.add_component<ShadowCaster2DComponent>(collider_caster, ShadowCaster2DComponent{});

    const EntityID static_caster = entities.create_entity();
    ShadowCaster2DComponent caster;
    caster.source = ShadowGeometrySource::StaticPolygon;
    caster.vertices.resize(2);
    caster.affected_layers = 0;
    entities.add_component<ShadowCaster2DComponent>(static_caster, caster);

    const auto diagnostics = SceneValidator::validate(entities);
    assert(contains_code(diagnostics, SceneDiagnosticCode::InvalidLight2D));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingShadowCasterCollider));
    assert(contains_code(diagnostics, SceneDiagnosticCode::InvalidShadowCaster2D));
}

void test_invalid_ui_layout_is_reported() {
    EntityManager entities;

    const EntityID canvas_entity = entities.create_entity();
    CanvasComponent canvas;
    canvas.reference_size = {0.0f, 720.0f};
    entities.add_component<CanvasComponent>(canvas_entity, canvas);

    const EntityID layout_entity = entities.create_entity();
    UILayoutComponent layout;
    layout.anchor_min = {0.8f, 0.2f};
    layout.anchor_max = {0.4f, 1.2f};
    entities.add_component<UILayoutComponent>(layout_entity, layout);

    const EntityID text_entity = entities.create_entity();
    UITextComponent text;
    text.font_size = 0.0f;
    text.fit_content_width = true;
    text.margins.left = {60.0f, UILengthUnit::Percent};
    text.margins.right = {40.0f, UILengthUnit::Percent};
    text.fit_padding.x = -1.0f;
    entities.add_component<UITextComponent>(text_entity, text);

    const auto diagnostics = SceneValidator::validate(entities);
    assert(contains_code(diagnostics, SceneDiagnosticCode::InvalidCanvas));
    assert(contains_code(diagnostics, SceneDiagnosticCode::InvalidUILayout));
    assert(contains_code(diagnostics, SceneDiagnosticCode::InvalidUIText));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingCanvasAncestor));
    assert(contains_code(diagnostics, SceneDiagnosticCode::MissingUILayout));
}

} // namespace

int main() {
    test_valid_scene_has_no_errors();
    test_missing_primary_camera_is_warning();
    test_duplicate_primary_cameras_are_errors();
    test_hierarchy_errors_are_reported_without_mutation();
    test_hierarchy_cycles_are_reported();
    test_missing_assets_are_reported_when_a_database_is_available();
    test_invalid_colliders_and_tilemaps_are_reported();
    test_invalid_lighting_is_reported();
    test_invalid_ui_layout_is_reported();
    std::cout << "SceneValidatorTests passed\n";
    return 0;
}
