#include "SceneValidator.hpp"

#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"

#include <algorithm>
#include <unordered_set>

namespace Engine {
namespace {

void add_diagnostic(
    std::vector<SceneDiagnostic>& diagnostics,
    SceneDiagnosticSeverity severity,
    SceneDiagnosticCode code,
    EntityID entity,
    std::string message
) {
    diagnostics.push_back({severity, code, entity, std::move(message)});
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

} // namespace

std::vector<SceneDiagnostic> SceneValidator::validate(const EntityManager& entities) {
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
                    "Hierarchy child does not exist"
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

    return diagnostics;
}

bool SceneValidator::has_errors(const std::vector<SceneDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
        return diagnostic.severity == SceneDiagnosticSeverity::Error;
    });
}

} // namespace Engine
