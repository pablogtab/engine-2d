#include "SceneValidator.hpp"

#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../core/AssetDatabase.hpp"
#include "../tilemap/Tilemap.hpp"

#include <algorithm>
#include <unordered_set>

namespace Engine {
namespace {

void add_diagnostic(
    std::vector<SceneDiagnostic>& diagnostics,
    SceneDiagnosticSeverity severity,
    SceneDiagnosticCode code,
    EntityID entity,
    std::string message,
    EntityID related_entity = 0,
    std::string asset_path = {},
    AssetHandle asset_handle = {}
) {
    diagnostics.push_back({
        severity, code, entity, std::move(message), related_entity, std::move(asset_path), asset_handle
    });
}

bool parent_chain_has_cycle(const EntityManager& entities, EntityID entity) {
    std::unordered_set<EntityID> visited;
    EntityID current = entity;
    while (current != 0) {
        if (!entities.is_alive(current) || !entities.has_component<HierarchyComponent>(current)) return false;
        if (!visited.insert(current).second) return true;
        current = entities.get_component<HierarchyComponent>(current).parent;
    }
    return false;
}

bool has_ready_asset(
    const AssetDatabase& asset_database,
    AssetHandle handle,
    AssetType expected_type
) {
    const AssetRecord* record = asset_database.find(handle);
    return record && record->status == AssetStatus::Ready && record->type == expected_type;
}

bool has_ready_asset(
    const AssetDatabase& asset_database,
    const std::string& path,
    AssetType expected_type
) {
    const AssetRecord* record = asset_database.find_by_path(path);
    return record && record->status == AssetStatus::Ready && record->type == expected_type;
}

} // namespace

std::vector<SceneDiagnostic> SceneValidator::validate(
    const EntityManager& entities,
    const AssetDatabase* asset_database
) {
    std::vector<SceneDiagnostic> diagnostics;

    std::vector<EntityID> primary_cameras;
    for (const EntityID entity : entities.get_living_entities()) {
        if (entities.has_component<CameraComponent>(entity) &&
            entities.get_component<CameraComponent>(entity).is_primary) {
            primary_cameras.push_back(entity);
        }
    }
    if (primary_cameras.empty()) {
        add_diagnostic(
            diagnostics,
            SceneDiagnosticSeverity::Warning,
            SceneDiagnosticCode::MissingPrimaryCamera,
            0,
            "Scene has no primary camera"
        );
    } else if (primary_cameras.size() > 1) {
        for (const EntityID entity : primary_cameras) {
            add_diagnostic(
                diagnostics,
                SceneDiagnosticSeverity::Error,
                SceneDiagnosticCode::DuplicatePrimaryCamera,
                entity,
                "Scene has multiple primary cameras"
            );
        }
    }

    for (const EntityID entity : entities.get_living_entities()) {
        if (!entities.has_component<HierarchyComponent>(entity)) continue;
        const auto& hierarchy = entities.get_component<HierarchyComponent>(entity);

        if (hierarchy.parent != 0) {
            if (!entities.is_alive(hierarchy.parent)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingHierarchyParent,
                    entity,
                    "Hierarchy parent does not exist"
                );
            } else if (!entities.has_component<HierarchyComponent>(hierarchy.parent) ||
                       std::find(
                           entities.get_component<HierarchyComponent>(hierarchy.parent).children.begin(),
                           entities.get_component<HierarchyComponent>(hierarchy.parent).children.end(),
                           entity
                       ) == entities.get_component<HierarchyComponent>(hierarchy.parent).children.end()) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::HierarchyLinkMismatch,
                    entity,
                    "Hierarchy parent does not reference this child"
                );
            }
        }

        for (const EntityID child : hierarchy.children) {
            if (!entities.is_alive(child)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingHierarchyChild,
                    entity,
                    "Hierarchy child does not exist",
                    child
                );
            } else if (!entities.has_component<HierarchyComponent>(child) ||
                       entities.get_component<HierarchyComponent>(child).parent != entity) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::HierarchyLinkMismatch,
                    entity,
                    "Hierarchy child does not reference this parent"
                );
            }
        }

        if (parent_chain_has_cycle(entities, entity)) {
            add_diagnostic(
                diagnostics,
                SceneDiagnosticSeverity::Error,
                SceneDiagnosticCode::HierarchyCycle,
                entity,
                "Hierarchy parent chain contains a cycle"
            );
        }
    }

    for (const EntityID entity : entities.get_living_entities()) {
        if (asset_database && entities.has_component<SpriteComponent>(entity)) {
            const auto& sprite = entities.get_component<SpriteComponent>(entity);
            if (sprite.texture_handle.is_valid() &&
                !has_ready_asset(*asset_database, sprite.texture_handle, AssetType::Texture)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingTextureAsset,
                    entity,
                    "Sprite texture asset is missing or unavailable",
                    0,
                    {},
                    sprite.texture_handle
                );
            }
            if (sprite.material_handle.is_valid() &&
                !has_ready_asset(*asset_database, sprite.material_handle, AssetType::Material)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingMaterialAsset,
                    entity,
                    "Sprite material asset is missing or unavailable",
                    0,
                    {},
                    sprite.material_handle
                );
            }
        }

        if (asset_database && entities.has_component<PrefabLinkComponent>(entity)) {
            const auto& prefab = entities.get_component<PrefabLinkComponent>(entity);
            if (!prefab.prefab_asset_path.empty() &&
                !has_ready_asset(*asset_database, prefab.prefab_asset_path, AssetType::Prefab)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingPrefabAsset,
                    entity,
                    "Prefab asset is missing or unavailable",
                    0,
                    prefab.prefab_asset_path
                );
            }
        }

        if (entities.has_component<ColliderComponent>(entity)) {
            const auto& collider = entities.get_component<ColliderComponent>(entity);
            const bool invalid_shape = std::any_of(collider.shapes.begin(), collider.shapes.end(), [](const ColliderShape& shape) {
                return (shape.type == ColliderShapeType::Box && (shape.size.x <= 0.0f || shape.size.y <= 0.0f)) ||
                    (shape.type == ColliderShapeType::Circle && shape.radius <= 0.0f) ||
                    (shape.type == ColliderShapeType::Polygon && shape.vertices.size() < 3);
            });
            if (invalid_shape) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::InvalidColliderShape,
                    entity,
                    "Collider contains an invalid shape"
                );
            }
        }

        if (entities.has_component<TilemapComponent>(entity)) {
            const auto& tilemap = entities.get_component<TilemapComponent>(entity);
            if (tilemap.tile_size <= 0.0f || tilemap.tile_pixel_size <= 0 ||
                tilemap.tileset_cols <= 0 || tilemap.tileset_rows <= 0) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::InvalidTilemapDimensions,
                    entity,
                    "Tilemap dimensions must be positive"
                );
            }
            if (asset_database && !tilemap.tileset_path.empty() &&
                !has_ready_asset(*asset_database, tilemap.tileset_path, AssetType::Texture)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingTilemapTileset,
                    entity,
                    "Tilemap tileset asset is missing or unavailable",
                    0,
                    tilemap.tileset_path
                );
            }
        }
    }

    return diagnostics;
}

bool SceneValidator::has_errors(const std::vector<SceneDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.severity == SceneDiagnosticSeverity::Error;
    });
}

} // namespace Engine
