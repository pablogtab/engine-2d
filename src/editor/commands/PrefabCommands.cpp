#include "PrefabCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"
#include "../../ecs/TransformSystem.hpp"
#include <filesystem>

namespace Engine {

InstantiatePrefabCommand::InstantiatePrefabCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    std::string prefab_filepath_or_json,
    bool is_raw_json,
    std::optional<TransformComponent> desired_world_transform,
    EntityID target_parent,
    SDL_Renderer* renderer,
    const EditorEntityStateStore* state_store
) : m_em(em),
    m_selection(std::move(selection)),
    m_desired_world_transform(desired_world_transform),
    m_target_parent(target_parent),
    m_renderer(renderer) {
    if (!m_em) {
        m_valid = false;
        return;
    }

    if (m_target_parent != 0 && state_store && state_store->is_locked(m_target_parent)) {
        ENGINE_LOG_WARN("[PREFAB] Cannot instantiate prefab under locked parent #{}", m_target_parent);
        m_valid = false;
        return;
    }

    if (is_raw_json) {
        m_json_content = std::move(prefab_filepath_or_json);
    } else {
        m_json_content = PrefabSerializer::read_json_from_file(prefab_filepath_or_json);
        std::filesystem::path p(prefab_filepath_or_json);
        std::string stem = p.stem().string();
        if (!stem.empty()) {
            m_description = "Instantiate Prefab '" + stem + "'";
        }
    }

    if (m_json_content.empty()) {
        ENGINE_LOG_ERROR("[PREFAB] InstantiatePrefabCommand: empty JSON content");
        m_valid = false;
        return;
    }

    m_valid = true;
}

bool InstantiatePrefabCommand::execute() {
    if (!m_valid || !m_em) return false;

    if (m_selection) {
        m_previous_selection = m_selection->get_selected();
    }

    // If this is the first execution, instantiate from JSON and capture snapshots
    if (m_snapshots.empty()) {
        ComponentSerializationContext context{
            .renderer = m_renderer,
            .asset_manager = &AssetManager::get()
        };

        auto result = EntitySubtreeArchive::instantiate_from_json(
            *m_em,
            m_json_content,
            m_desired_world_transform,
            m_target_parent,
            context
        );

        if (!result.success) {
            ENGINE_LOG_ERROR("[PREFAB] InstantiatePrefabCommand failed: {}", result.error_message);
            return false;
        }

        m_root_entity = result.root_entity;
        m_created_entity_ids = result.created_entities;

        // Capture in-memory snapshots for subsequent Redo cycles without disk access
        m_snapshots.clear();
        for (EntityID id : m_created_entity_ids) {
            m_snapshots.push_back(capture_entity_snapshot(*m_em, id));
        }

        if (m_selection && m_root_entity != 0) {
            m_selection->select(m_root_entity, false);
        }
        return true;
    }

    // Subsequent execution (Redo): restore from captured snapshots with atomic rollback
    std::vector<EntityID> restored_ids;
    bool redo_success = true;

    for (const auto& snap : m_snapshots) {
        if (!restore_entity_snapshot(*m_em, snap)) {
            ENGINE_LOG_ERROR("[PREFAB] InstantiatePrefabCommand redo failed restoring entity #{}", snap.id);
            redo_success = false;
            break;
        }
        restored_ids.push_back(snap.id);
    }

    if (!redo_success) {
        for (EntityID id : restored_ids) {
            if (m_em->is_alive(id)) m_em->destroy_entity(id);
        }
        TransformSystem::update(*m_em);
        return false;
    }

    // Re-establish parent links for the restored subtree
    if (m_target_parent != 0) {
        if (!m_em->is_alive(m_target_parent) || !m_em->has_component<TransformComponent>(m_target_parent)) {
            ENGINE_LOG_ERROR("[PREFAB] InstantiatePrefabCommand redo failed: target parent #{} is not alive or valid", m_target_parent);
            redo_success = false;
        } else {
            auto res = m_em->set_parent(m_root_entity, m_target_parent);
            if (!res.succeeded()) {
                ENGINE_LOG_ERROR("[PREFAB] InstantiatePrefabCommand redo failed setting parent for root #{}: {}", m_root_entity, res.message());
                redo_success = false;
            }
        }
    }

    if (redo_success) {
        for (const auto& snap : m_snapshots) {
            if (snap.id != m_root_entity && snap.has_hierarchy && snap.hierarchy.parent != 0) {
                if (!m_em->is_alive(snap.hierarchy.parent)) {
                    ENGINE_LOG_ERROR("[PREFAB] InstantiatePrefabCommand redo failed: parent #{} is not alive", snap.hierarchy.parent);
                    redo_success = false;
                    break;
                }
                auto res = m_em->set_parent(snap.id, snap.hierarchy.parent);
                if (!res.succeeded()) {
                    ENGINE_LOG_ERROR("[PREFAB] InstantiatePrefabCommand redo failed setting parent for child #{}: {}", snap.id, res.message());
                    redo_success = false;
                    break;
                }
            }
        }
    }

    if (!redo_success) {
        for (EntityID id : restored_ids) {
            if (m_em->is_alive(id)) m_em->destroy_entity(id);
        }
        TransformSystem::update(*m_em);
        return false;
    }

    // Finalize entities (e.g. rebuild physics bodies, colliders, tilemaps, animations)
    ComponentSerializationContext ctx{
        .renderer = m_renderer,
        .asset_manager = &AssetManager::get()
    };
    ComponentFinalizeContext finalize_context{
        .serialization = ctx,
        .reason = ComponentFinalizeReason::SnapshotRestore
    };

    std::string finalize_error;
    if (!default_component_registry().finalize_entities(*m_em, restored_ids, finalize_context, finalize_error)) {
        ENGINE_LOG_ERROR("[PREFAB] InstantiatePrefabCommand redo failed finalizing entities: {}", finalize_error);
        for (EntityID id : restored_ids) {
            if (m_em->is_alive(id)) m_em->destroy_entity(id);
        }
        TransformSystem::update(*m_em);
        return false;
    }

    TransformSystem::update(*m_em);

    if (m_selection && m_root_entity != 0) {
        m_selection->select(m_root_entity, false);
    }
    return true;
}

bool InstantiatePrefabCommand::undo() {
    if (!m_valid || !m_em) return false;

    // Destroy created entities in reverse order
    for (auto it = m_created_entity_ids.rbegin(); it != m_created_entity_ids.rend(); ++it) {
        if (m_em->is_alive(*it)) {
            m_em->destroy_entity(*it);
        }
    }

    TransformSystem::update(*m_em);

    if (m_selection) {
        m_selection->set_selection(m_previous_selection);
    }
    return true;
}

} // namespace Engine
