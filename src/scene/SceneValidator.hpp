#pragma once

#include "../ecs/EcsTypes.hpp"
#include <string>
#include <vector>

namespace Engine {

class EntityManager;

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
    HierarchyCycle
};

struct SceneDiagnostic {
    SceneDiagnosticSeverity severity{SceneDiagnosticSeverity::Warning};
    SceneDiagnosticCode code{SceneDiagnosticCode::MissingPrimaryCamera};
    EntityID entity{0};
    std::string message;
};

class SceneValidator {
public:
    [[nodiscard]] static std::vector<SceneDiagnostic> validate(const EntityManager& entities);
    [[nodiscard]] static bool has_errors(const std::vector<SceneDiagnostic>& diagnostics);
};

} // namespace Engine
