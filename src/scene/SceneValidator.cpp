#include "SceneValidator.hpp"

#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../ecs/components/ScriptingComponents.hpp"
#include "../ecs/components/UIComponents.hpp"
#include "../core/AssetDatabase.hpp"
#include "../tilemap/Tilemap.hpp"

#include <algorithm>
#include <cmath>
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

bool has_canvas_ancestor(const EntityManager& entities, EntityID entity) {
    std::unordered_set<EntityID> visited;
    EntityID current = entity;
    while (current != 0 && entities.is_alive(current) && visited.insert(current).second) {
        if (entities.has_component<CanvasComponent>(current)) return true;
        if (!entities.has_component<HierarchyComponent>(current)) return false;
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

        if (asset_database && entities.has_component<RenderEffectComponent>(entity)) {
            const auto& effect = entities.get_component<RenderEffectComponent>(entity);
            if (effect.profile_handle.is_valid() &&
                !has_ready_asset(*asset_database, effect.profile_handle, AssetType::RenderLayerEffectProfile)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingRenderEffectAsset,
                    entity,
                    "Render effect profile is missing or unavailable",
                    0,
                    {},
                    effect.profile_handle
                );
            }
        }

        if (asset_database && entities.has_component<ScriptComponent>(entity)) {
            const auto& script = entities.get_component<ScriptComponent>(entity);
            if (!script.script_handle.is_valid() ||
                !has_ready_asset(*asset_database, script.script_handle, AssetType::Script)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingScriptAsset,
                    entity,
                    "Script asset is missing or unavailable",
                    0,
                    {},
                    script.script_handle
                );
            }
        }

        if (asset_database && entities.has_component<UIImageComponent>(entity)) {
            const auto& image = entities.get_component<UIImageComponent>(entity);
            if (image.texture_handle.is_valid() &&
                !has_ready_asset(*asset_database, image.texture_handle, AssetType::Texture)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingUIImageTexture,
                    entity,
                    "UI Image texture asset is missing or unavailable",
                    0,
                    {},
                    image.texture_handle
                );
            }
            if (image.material_handle.is_valid() &&
                !has_ready_asset(*asset_database, image.material_handle, AssetType::Material)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingMaterialAsset,
                    entity,
                    "UI Image material asset is missing or unavailable",
                    0,
                    {},
                    image.material_handle
                );
            }
        }

        if (asset_database && entities.has_component<UITextComponent>(entity)) {
            const auto& text = entities.get_component<UITextComponent>(entity);
            if (text.font_handle.is_valid() &&
                !has_ready_asset(*asset_database, text.font_handle, AssetType::Font)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingUIFont,
                    entity,
                    "UI Text font asset is missing or unavailable",
                    0,
                    {},
                    text.font_handle
                );
            }
            if (text.material_handle.is_valid() &&
                !has_ready_asset(*asset_database, text.material_handle, AssetType::Material)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingMaterialAsset,
                    entity,
                    "UI Text material asset is missing or unavailable",
                    0,
                    {},
                    text.material_handle
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

        if (entities.has_component<Light2DComponent>(entity)) {
            const auto& light = entities.get_component<Light2DComponent>(entity);
            if (!std::isfinite(light.intensity) || light.intensity < 0.0f || light.intensity > 1.0f ||
                !std::isfinite(light.radius) ||
                (light.type == Light2DType::Point && light.radius <= 0.0f) ||
                !std::isfinite(light.falloff) || light.falloff < 0.1f || light.falloff > 8.0f ||
                light.affected_layers == 0) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::InvalidLight2D,
                    entity,
                    "Light 2D has invalid intensity, radius, falloff, or affected layers"
                );
            }
        }

        if (entities.has_component<ShadowCaster2DComponent>(entity)) {
            const auto& caster = entities.get_component<ShadowCaster2DComponent>(entity);
            if (caster.source == ShadowGeometrySource::Collider &&
                !entities.has_component<ColliderComponent>(entity)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingShadowCasterCollider,
                    entity,
                    "Collider shadow caster requires a Collider 2D component"
                );
            }
            const bool invalid_vertices = std::any_of(
                caster.vertices.begin(), caster.vertices.end(), [](const glm::vec2& vertex) {
                    return !std::isfinite(vertex.x) || !std::isfinite(vertex.y);
                }
            );
            if (caster.affected_layers == 0 || invalid_vertices ||
                (caster.source == ShadowGeometrySource::StaticPolygon && caster.vertices.size() < 3)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::InvalidShadowCaster2D,
                    entity,
                    "Shadow Caster 2D has invalid geometry or affected layers"
                );
            }
        }

        if (entities.has_component<CanvasComponent>(entity)) {
            const auto& canvas = entities.get_component<CanvasComponent>(entity);
            if (!std::isfinite(canvas.reference_size.x) || !std::isfinite(canvas.reference_size.y) ||
                canvas.reference_size.x <= 0.0f || canvas.reference_size.y <= 0.0f) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::InvalidCanvas,
                    entity,
                    "Canvas reference size must be finite and positive"
                );
            }
        }

        if (entities.has_component<UILayoutComponent>(entity)) {
            const auto& layout = entities.get_component<UILayoutComponent>(entity);
            const auto finite = [](const glm::vec2& value) {
                return std::isfinite(value.x) && std::isfinite(value.y);
            };
            const bool invalid_anchors =
                glm::any(glm::lessThan(layout.anchor_min, glm::vec2(0.0f))) ||
                glm::any(glm::greaterThan(layout.anchor_max, glm::vec2(1.0f))) ||
                glm::any(glm::greaterThan(layout.anchor_min, layout.anchor_max));
            const bool invalid_pivot =
                glm::any(glm::lessThan(layout.pivot, glm::vec2(0.0f))) ||
                glm::any(glm::greaterThan(layout.pivot, glm::vec2(1.0f)));
            if (!finite(layout.anchor_min) || !finite(layout.anchor_max) || !finite(layout.pivot) ||
                !finite(layout.anchored_position) || !finite(layout.size_delta) ||
                invalid_anchors || invalid_pivot) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::InvalidUILayout,
                    entity,
                    "UI Layout anchors, pivot, position, or size are invalid"
                );
            }
            if (!has_canvas_ancestor(entities, entity)) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::MissingCanvasAncestor,
                    entity,
                    "UI Layout requires a Canvas on this entity or an ancestor"
                );
            }
        }

        if (entities.has_component<UITextComponent>(entity)) {
            const auto& text = entities.get_component<UITextComponent>(entity);
            const auto finite_length = [](const UILength& length) {
                return std::isfinite(length.value);
            };
            const auto percentage = [](const UILength& length) {
                return length.unit == UILengthUnit::Percent ? length.value * 0.01f : 0.0f;
            };
            const bool invalid_margins =
                !finite_length(text.margins.left) || !finite_length(text.margins.top) ||
                !finite_length(text.margins.right) || !finite_length(text.margins.bottom);
            const bool invalid_padding =
                !std::isfinite(text.fit_padding.x) || !std::isfinite(text.fit_padding.y) ||
                !std::isfinite(text.fit_padding.z) || !std::isfinite(text.fit_padding.w) ||
                glm::any(glm::lessThan(text.fit_padding, glm::vec4(0.0f)));
            const bool invalid_fit =
                (text.fit_content_width &&
                 percentage(text.margins.left) + percentage(text.margins.right) >= 1.0f) ||
                (text.fit_content_height &&
                 percentage(text.margins.top) + percentage(text.margins.bottom) >= 1.0f);
            if (!std::isfinite(text.font_size) || text.font_size <= 0.0f ||
                text.font_size > 512.0f || invalid_margins || invalid_padding || invalid_fit) {
                add_diagnostic(
                    diagnostics,
                    SceneDiagnosticSeverity::Error,
                    SceneDiagnosticCode::InvalidUIText,
                    entity,
                    "UI Text font size, margins, or fit-content settings are invalid"
                );
            }
        }

        if ((entities.has_component<UITextComponent>(entity) ||
             entities.has_component<UIImageComponent>(entity) ||
             entities.has_component<UIButtonComponent>(entity)) &&
            !entities.has_component<UILayoutComponent>(entity)) {
            add_diagnostic(
                diagnostics,
                SceneDiagnosticSeverity::Error,
                SceneDiagnosticCode::MissingUILayout,
                entity,
                "UI visual components require UI Layout on the same entity"
            );
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
