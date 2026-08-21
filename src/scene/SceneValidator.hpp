#pragma once

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
    MissingPrefabAsset,
    MissingTilemapTileset,
    InvalidColliderShape,
    InvalidTilemapDimensions
};

struct SceneDiagnostic {
    SceneDiagnosticSeverity severity{SceneDiagnosticSeverity::Warning};
    SceneDiagnosticCode code{SceneDiagnosticCode::MissingPrimaryCamera};
    EntityID entity{0};
    std::string message;
    EntityID related_entity{0};
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
