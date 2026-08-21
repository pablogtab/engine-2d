#pragma once

#include "../core/AssetHandle.hpp"
#include "../ecs/EcsTypes.hpp"
#include <string>
#include <vector>

namespace Engine {

class EntityManager;
class AssetDatabase;

enum class SceneDiagnosticSeverity {
    Warning,
    Error
};

enum class SceneDiagnosticCode {
    MissingPrimaryCamera,
    DuplicatePrimaryCamera,
    MissingHierarchyParent,
    MissingHierarchyChild,
    HierarchyLinkMismatch,
    HierarchyCycle,
    MissingTextureAsset,
    MissingMaterialAsset,
    MissingRenderEffectAsset,
    MissingPrefabAsset,
    MissingTilemapTileset,
    InvalidColliderShape,
    InvalidTilemapDimensions,
    InvalidLight2D,
    InvalidShadowCaster2D,
    MissingShadowCasterCollider,
    MissingUIImageTexture,
    MissingUIFont,
    InvalidCanvas,
    InvalidUILayout,
    InvalidUIText,
    MissingUILayout,
    MissingCanvasAncestor
};

struct SceneDiagnostic {
    SceneDiagnosticSeverity severity{SceneDiagnosticSeverity::Warning};
    SceneDiagnosticCode code{SceneDiagnosticCode::MissingPrimaryCamera};
    EntityID entity{0};
    std::string message;
    EntityID related_entity{0};
    std::string asset_path;
    AssetHandle asset_handle{};
};

class SceneValidator {
public:
    [[nodiscard]] static std::vector<SceneDiagnostic> validate(
        const EntityManager& entities,
        const AssetDatabase* asset_database = nullptr
    );
    [[nodiscard]] static bool has_errors(const std::vector<SceneDiagnostic>& diagnostics);
};

} // namespace Engine
