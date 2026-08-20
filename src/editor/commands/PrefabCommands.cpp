#include "PrefabCommands.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../scene/PrefabPropagation.hpp"
#include "../../scene/PrefabOverrides.hpp"
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

    m_is_raw_json = is_raw_json;
    if (is_raw_json) {
        m_json_content = std::move(prefab_filepath_or_json);
    } else {
        m_prefab_filepath = prefab_filepath_or_json;
        std::replace(m_prefab_filepath.begin(), m_prefab_filepath.end(), '\\', '/');
        m_json_content = PrefabSerializer::read_json_from_file(m_prefab_filepath);
        std::filesystem::path p(m_prefab_filepath);
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

        // If instantiated from file, attach PrefabLinkComponent to the root entity
        if (!m_is_raw_json && !m_prefab_filepath.empty() && m_root_entity != 0) {
            if (!m_em->has_component<PrefabLinkComponent>(m_root_entity)) {
                m_em->add_component<PrefabLinkComponent>(m_root_entity, PrefabLinkComponent{m_prefab_filepath, result.base_prefab_path});
            } else {
                auto& link = m_em->get_component<PrefabLinkComponent>(m_root_entity);
                link.prefab_asset_path = m_prefab_filepath;
                link.base_prefab_path = result.base_prefab_path;
            }
        }


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

// ──────────────────────────────────────────────────────────────────────────────
// UnpackPrefabCommand
// ──────────────────────────────────────────────────────────────────────────────
UnpackPrefabCommand::UnpackPrefabCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    EntityID root_entity,
    const EditorEntityStateStore* state_store
) : m_em(em),
    m_selection(std::move(selection)),
    m_root_entity(root_entity) {
    if (!m_em || !m_em->is_alive(m_root_entity)) {
        m_valid = false;
        return;
    }
    if (state_store && state_store->is_locked(m_root_entity)) {
        ENGINE_LOG_WARN("[PREFAB] Cannot unpack locked prefab instance #{}", m_root_entity);
        m_valid = false;
        return;
    }
    if (!m_em->has_component<PrefabLinkComponent>(m_root_entity)) {
        ENGINE_LOG_WARN("[PREFAB] Entity #{} does not have a PrefabLinkComponent", m_root_entity);
        m_valid = false;
        return;
    }

    m_stored_link = m_em->get_component<PrefabLinkComponent>(m_root_entity);
    std::filesystem::path p(m_stored_link.prefab_asset_path);
    std::string stem = p.stem().string();
    if (!stem.empty()) {
        m_description = "Unpack Prefab '" + stem + "'";
    }
    m_valid = true;
}

bool UnpackPrefabCommand::execute() {
    if (!m_valid || !m_em || !m_em->is_alive(m_root_entity)) return false;
    if (m_em->has_component<PrefabLinkComponent>(m_root_entity)) {
        m_em->remove_component<PrefabLinkComponent>(m_root_entity);
    }
    return true;
}

bool UnpackPrefabCommand::undo() {
    if (!m_valid || !m_em || !m_em->is_alive(m_root_entity)) return false;
    if (!m_em->has_component<PrefabLinkComponent>(m_root_entity)) {
        m_em->add_component<PrefabLinkComponent>(m_root_entity, m_stored_link);
    }
    return true;
}

namespace {

std::vector<EntityID> restore_snapshots_remapped(
    EntityManager& em,
    const std::vector<EntityID>& old_ids,
    const std::vector<EntitySnapshot>& snapshots,
    SDL_Renderer* renderer
) {
    std::unordered_map<EntityID, EntityID> id_map;
    std::vector<EntityID> created_ids;
    created_ids.reserve(snapshots.size());

    for (size_t i = 0; i < snapshots.size(); ++i) {
        EntityID id = em.create_entity();
        if (i < old_ids.size()) {
            id_map[old_ids[i]] = id;
        }
        created_ids.push_back(id);
        default_component_registry().restore_entity(em, id, snapshots[i].registered_components);
    }

    for (size_t i = 0; i < snapshots.size(); ++i) {
        if (snapshots[i].has_hierarchy) {
            EntityID old_parent = snapshots[i].hierarchy.parent;
            EntityID target_parent = (id_map.find(old_parent) != id_map.end()) ? id_map[old_parent] : old_parent;
            static_cast<void>(em.set_parent(created_ids[i], target_parent));
        }
    }

    // Finalize entities (e.g. rebuild physics bodies, colliders, tilemaps, animations)
    ComponentSerializationContext ctx{
        .renderer = renderer,
        .asset_manager = &AssetManager::get()
    };
    ComponentFinalizeContext finalize_context{
        .serialization = ctx,
        .reason = ComponentFinalizeReason::SnapshotRestore
    };
    std::string finalize_error;
    if (!default_component_registry().finalize_entities(em, created_ids, finalize_context, finalize_error)) {
        ENGINE_LOG_ERROR("[PREFAB] Failed finalizing restored entities: {}", finalize_error);
    }

    TransformSystem::update(em);
    return created_ids;
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────────
// RevertPrefabCommand
// ──────────────────────────────────────────────────────────────────────────────
RevertPrefabCommand::RevertPrefabCommand(
    EntityManager* em,
    std::shared_ptr<EditorSelection> selection,
    EntityID root_entity,
    SDL_Renderer* renderer,
    const EditorEntityStateStore* state_store
) : m_em(em),
    m_selection(std::move(selection)),
    m_original_root_entity(root_entity),
    m_renderer(renderer) {
    if (!m_em || !m_em->is_alive(m_original_root_entity)) {
        m_valid = false;
        return;
    }
    if (state_store && state_store->is_locked(m_original_root_entity)) {
        ENGINE_LOG_WARN("[PREFAB] Cannot revert locked prefab instance #{}", m_original_root_entity);
        m_valid = false;
        return;
    }
    if (!m_em->has_component<PrefabLinkComponent>(m_original_root_entity)) {
        ENGINE_LOG_WARN("[PREFAB] Entity #{} does not have a PrefabLinkComponent", m_original_root_entity);
        m_valid = false;
        return;
    }

    m_prefab_filepath = m_em->get_component<PrefabLinkComponent>(m_original_root_entity).prefab_asset_path;
    m_json_content = PrefabSerializer::read_json_from_file(m_prefab_filepath);
    if (m_json_content.empty()) {
        ENGINE_LOG_ERROR("[PREFAB] RevertPrefabCommand: cannot read asset JSON from '{}'", m_prefab_filepath);
        m_valid = false;
        return;
    }

    std::filesystem::path p(m_prefab_filepath);
    std::string stem = p.stem().string();
    if (!stem.empty()) {
        m_description = "Revert Prefab Contents '" + stem + "'";
    }
    m_valid = true;
}

bool RevertPrefabCommand::execute() {
    if (!m_valid || !m_em) return false;

    if (m_selection) {
        m_previous_selection = m_selection->get_selected();
    }

    if (m_before_snapshots.empty()) {
        if (!m_em->is_alive(m_original_root_entity)) return false;

        // Collect existing instance subtree IDs in hierarchical order
        std::vector<EntityID> subtree_ids;
        std::function<void(EntityID)> collect = [&](EntityID id) {
            if (!m_em->is_alive(id)) return;
            subtree_ids.push_back(id);
            if (m_em->has_component<HierarchyComponent>(id)) {
                for (EntityID child : m_em->get_component<HierarchyComponent>(id).children) {
                    collect(child);
                }
            }
        };
        collect(m_original_root_entity);

        m_before_entity_ids = subtree_ids;
        for (EntityID id : m_before_entity_ids) {
            m_before_snapshots.push_back(capture_entity_snapshot(*m_em, id));
        }

        // Current world transform & parent
        TransformComponent desired_world;
        if (m_em->has_component<TransformComponent>(m_original_root_entity)) {
            const auto& tr = m_em->get_component<TransformComponent>(m_original_root_entity);
            desired_world.position = tr.world_position;
            desired_world.scale = tr.world_scale;
            desired_world.rotation = tr.world_rotation;
            desired_world.pivot = tr.world_pivot;
            desired_world.world_position = tr.world_position;
            desired_world.world_scale = tr.world_scale;
            desired_world.world_rotation = tr.world_rotation;
            desired_world.world_pivot = tr.world_pivot;
        }

        EntityID parent_id = 0;
        if (m_em->has_component<HierarchyComponent>(m_original_root_entity)) {
            parent_id = m_em->get_component<HierarchyComponent>(m_original_root_entity).parent;
        }

        // Destroy old subtree entities in reverse order
        for (auto it = m_before_entity_ids.rbegin(); it != m_before_entity_ids.rend(); ++it) {
            if (m_em->is_alive(*it)) {
                m_em->destroy_entity(*it);
            }
        }

        // Instantiate fresh from captured asset JSON
        ComponentSerializationContext context{
            .renderer = m_renderer,
            .asset_manager = &AssetManager::get()
        };
        auto result = EntitySubtreeArchive::instantiate_from_json(
            *m_em,
            m_json_content,
            desired_world,
            parent_id,
            context
        );

        if (!result.success) {
            ENGINE_LOG_ERROR("[PREFAB] RevertPrefabCommand failed to instantiate: {}", result.error_message);
            // Rollback by restoring before snapshots with ID remapping and component finalization
            m_before_entity_ids = restore_snapshots_remapped(*m_em, m_before_entity_ids, m_before_snapshots, m_renderer);
            m_original_root_entity = m_before_entity_ids.empty() ? 0 : m_before_entity_ids.front();
            if (m_selection && m_original_root_entity != 0) {
                m_selection->select(m_original_root_entity, false);
            }
            return false;
        }


        m_new_root_entity = result.root_entity;
        m_after_entity_ids = result.created_entities;

        // Attach PrefabLinkComponent to the new root
        if (!m_em->has_component<PrefabLinkComponent>(m_new_root_entity)) {
            m_em->add_component<PrefabLinkComponent>(m_new_root_entity, PrefabLinkComponent{m_prefab_filepath});
        }

        // Capture after snapshots for Redo cycles
        m_after_snapshots.clear();
        for (EntityID id : m_after_entity_ids) {
            m_after_snapshots.push_back(capture_entity_snapshot(*m_em, id));
        }

        TransformSystem::update(*m_em);

        if (m_selection && m_new_root_entity != 0) {
            m_selection->select(m_new_root_entity, false);
        }
        return true;
    }

    // Redo: destroy before entities and restore after snapshots with ID remapping
    for (auto it = m_before_entity_ids.rbegin(); it != m_before_entity_ids.rend(); ++it) {
        if (m_em->is_alive(*it)) {
            m_em->destroy_entity(*it);
        }
    }

    m_after_entity_ids = restore_snapshots_remapped(*m_em, m_after_entity_ids, m_after_snapshots, m_renderer);
    m_new_root_entity = m_after_entity_ids.empty() ? 0 : m_after_entity_ids.front();

    if (m_selection && m_new_root_entity != 0) {
        m_selection->select(m_new_root_entity, false);
    }
    return true;
}

bool RevertPrefabCommand::undo() {
    if (!m_valid || !m_em) return false;

    // Destroy after entities
    for (auto it = m_after_entity_ids.rbegin(); it != m_after_entity_ids.rend(); ++it) {
        if (m_em->is_alive(*it)) {
            m_em->destroy_entity(*it);
        }
    }

    // Restore before snapshots with ID remapping
    m_before_entity_ids = restore_snapshots_remapped(*m_em, m_before_entity_ids, m_before_snapshots, m_renderer);
    m_original_root_entity = m_before_entity_ids.empty() ? 0 : m_before_entity_ids.front();

    if (m_selection) {
        m_selection->set_selection(m_previous_selection);
    }
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// ApplyPrefabCommand
// ──────────────────────────────────────────────────────────────────────────────
ApplyPrefabCommand::ApplyPrefabCommand(
    EntityManager* em,
    EntityID root_entity,
    SDL_Renderer* renderer,
    const EditorEntityStateStore* state_store
) : m_em(em),
    m_root_entity(root_entity),
    m_renderer(renderer) {
    if (!m_em || !m_em->is_alive(m_root_entity)) {
        m_valid = false;
        return;
    }
    if (state_store && state_store->is_locked(m_root_entity)) {
        ENGINE_LOG_WARN("[PREFAB] Cannot apply locked prefab instance #{}", m_root_entity);
        m_valid = false;
        return;
    }
    if (!m_em->has_component<PrefabLinkComponent>(m_root_entity)) {
        ENGINE_LOG_WARN("[PREFAB] Entity #{} does not have a PrefabLinkComponent", m_root_entity);
        m_valid = false;
        return;
    }

    m_prefab_filepath = m_em->get_component<PrefabLinkComponent>(m_root_entity).prefab_asset_path;
    if (m_prefab_filepath.empty()) {
        ENGINE_LOG_ERROR("[PREFAB] ApplyPrefabCommand: empty prefab asset path on entity #{}", m_root_entity);
        m_valid = false;
        return;
    }

    m_previous_file_content = PrefabSerializer::read_json_from_file(m_prefab_filepath);
    std::filesystem::path p(m_prefab_filepath);
    std::string stem = p.stem().string();
    if (!stem.empty()) {
        m_description = "Apply Prefab '" + stem + "'";
    }
    m_valid = true;
}

bool ApplyPrefabCommand::execute() {
    if (!m_valid || !m_em) return false;

    if (m_first_execute) {
        if (!m_em->is_alive(m_root_entity)) return false;

        ComponentSerializationContext context{
            .renderer = m_renderer,
            .asset_manager = &AssetManager::get()
        };
        m_new_file_content = EntitySubtreeArchive::capture_to_json(*m_em, m_root_entity, context);
        if (m_new_file_content.empty()) {
            ENGINE_LOG_ERROR("[PREFAB] ApplyPrefabCommand failed capturing subtree for entity #{}", m_root_entity);
            return false;
        }

        try {
            std::filesystem::path p(m_prefab_filepath);
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path());
            }
        } catch (const std::exception& e) {
            ENGINE_LOG_ERROR("[PREFAB] ApplyPrefabCommand failed creating directory for '{}': {}", m_prefab_filepath, e.what());
            return false;
        }

        if (!FileSystem::write_text_atomically(m_prefab_filepath, m_new_file_content)) {
            ENGINE_LOG_ERROR("[PREFAB] ApplyPrefabCommand failed writing '{}' atomically", m_prefab_filepath);
            return false;
        }

        ENGINE_LOG_INFO("[PREFAB] Successfully applied overrides to '{}'", m_prefab_filepath);
        m_first_execute = false;

        // Propagate to other scene instances linking to the same prefab asset
        PrefabPropagation::propagate_to_instances(*m_em, m_prefab_filepath, m_root_entity, m_renderer);
        return true;
    }

    // Redo: rewrite captured new JSON text
    if (!FileSystem::write_text_atomically(m_prefab_filepath, m_new_file_content)) {
        ENGINE_LOG_ERROR("[PREFAB] ApplyPrefabCommand redo failed writing '{}'", m_prefab_filepath);
        return false;
    }
    ENGINE_LOG_INFO("[PREFAB] Redone applying overrides to '{}'", m_prefab_filepath);

    // Propagate redone asset to all instances
    PrefabPropagation::propagate_to_instances(*m_em, m_prefab_filepath, 0, m_renderer);
    return true;
}

bool ApplyPrefabCommand::undo() {
    if (!m_valid || !m_em) return false;

    if (!FileSystem::write_text_atomically(m_prefab_filepath, m_previous_file_content)) {
        ENGINE_LOG_ERROR("[PREFAB] ApplyPrefabCommand undo failed restoring '{}'", m_prefab_filepath);
        return false;
    }
    ENGINE_LOG_INFO("[PREFAB] Undone applying overrides, restored '{}'", m_prefab_filepath);

    // Propagate restored asset to all instances
    PrefabPropagation::propagate_to_instances(*m_em, m_prefab_filepath, 0, m_renderer);
    return true;
}

RevertComponentPrefabCommand::RevertComponentPrefabCommand(
    EntityManager* em,
    EntityID entity,
    std::string component_key,
    SDL_Renderer* renderer,
    const EditorEntityStateStore* state_store
) : m_em(em),
    m_entity(entity),
    m_component_key(std::move(component_key)),
    m_renderer(renderer)
{
    if (!m_em || !m_em->is_alive(m_entity) || (state_store && state_store->is_locked(m_entity))) {
        m_valid = false;
        return;
    }
    if (!m_em->has_component<PrefabLinkComponent>(m_entity)) {
        m_valid = false;
        return;
    }
    m_prefab_filepath = m_em->get_component<PrefabLinkComponent>(m_entity).prefab_asset_path;
    m_description = "Revert Component '" + m_component_key + "' to Prefab";
}

bool RevertComponentPrefabCommand::execute() {
    if (!m_valid || !m_em || !m_em->is_alive(m_entity)) return false;

    const ComponentRegistry& reg = default_component_registry();
    const ComponentDescriptor* desc = reg.find_by_key(m_component_key);
    if (!desc) return false;

    if (m_first_execute) {
        // Capture initial state
        m_had_before = desc->has(*m_em, m_entity);
        if (m_had_before) {
            desc->capture(*m_em, m_entity, m_before_snapshot);
        }

        nlohmann::json master_comp_json = PrefabOverrides::get_master_component_json(m_prefab_filepath, m_component_key);
        if (master_comp_json.is_null()) {
            // Master didn't have component
            if (m_had_before) {
                desc->remove(*m_em, m_entity);
            }
            m_had_after = false;
        } else {
            if (m_component_key == "transform") {
                if (!m_em->has_component<TransformComponent>(m_entity)) {
                    m_em->add_component<TransformComponent>(m_entity, TransformComponent{});
                }
                auto& t = m_em->get_component<TransformComponent>(m_entity);
                if (master_comp_json.contains("scale") && master_comp_json["scale"].is_array() && master_comp_json["scale"].size() >= 2) {
                    t.scale = glm::vec2(master_comp_json["scale"][0].get<float>(), master_comp_json["scale"][1].get<float>());
                }
                if (master_comp_json.contains("rotation")) {
                    t.rotation = master_comp_json["rotation"].get<float>();
                }
                if (master_comp_json.contains("pivot") && master_comp_json["pivot"].is_array() && master_comp_json["pivot"].size() >= 2) {
                    t.pivot = glm::vec2(master_comp_json["pivot"][0].get<float>(), master_comp_json["pivot"][1].get<float>());
                }
                if (master_comp_json.contains("visible")) {
                    t.visible = master_comp_json["visible"].get<bool>();
                }
            } else {
                ComponentSerializationContext ctx{.renderer = m_renderer, .asset_manager = &AssetManager::get()};
                std::string err;
                if (!desc->deserialize(*m_em, m_entity, master_comp_json, ctx, err)) {
                    ENGINE_LOG_ERROR("[PREFAB] RevertComponentPrefabCommand failed deserializing '{}': {}", m_component_key, err);
                    return false;
                }
            }
            m_had_after = true;
            desc->capture(*m_em, m_entity, m_after_snapshot);
        }

        ComponentFinalizeContext finalize_ctx{
            .serialization = {.renderer = m_renderer, .asset_manager = &AssetManager::get()},
            .reason = ComponentFinalizeReason::SnapshotRestore
        };
        std::string err;
        static_cast<void>(reg.finalize_entity(*m_em, m_entity, finalize_ctx, err));
        TransformSystem::update(*m_em);

        m_first_execute = false;
        return true;
    }

    // Redo
    if (m_had_after) {
        desc->restore(*m_em, m_entity, m_after_snapshot);
    } else if (desc->has(*m_em, m_entity)) {
        desc->remove(*m_em, m_entity);
    }
    ComponentFinalizeContext finalize_ctx{
        .serialization = {.renderer = m_renderer, .asset_manager = &AssetManager::get()},
        .reason = ComponentFinalizeReason::SnapshotRestore
    };
    std::string err;
    static_cast<void>(reg.finalize_entity(*m_em, m_entity, finalize_ctx, err));
    TransformSystem::update(*m_em);
    return true;
}

bool RevertComponentPrefabCommand::undo() {
    if (!m_valid || !m_em || !m_em->is_alive(m_entity)) return false;

    const ComponentRegistry& reg = default_component_registry();
    const ComponentDescriptor* desc = reg.find_by_key(m_component_key);
    if (!desc) return false;

    if (m_had_before) {
        desc->restore(*m_em, m_entity, m_before_snapshot);
    } else if (desc->has(*m_em, m_entity)) {
        desc->remove(*m_em, m_entity);
    }
    ComponentFinalizeContext finalize_ctx{
        .serialization = {.renderer = m_renderer, .asset_manager = &AssetManager::get()},
        .reason = ComponentFinalizeReason::SnapshotRestore
    };
    std::string err;
    static_cast<void>(reg.finalize_entity(*m_em, m_entity, finalize_ctx, err));
    TransformSystem::update(*m_em);
    return true;
}


ApplyComponentPrefabCommand::ApplyComponentPrefabCommand(
    EntityManager* em,
    EntityID root_entity,
    std::string component_key,
    SDL_Renderer* renderer,
    const EditorEntityStateStore* state_store
) : m_em(em),
    m_root_entity(root_entity),
    m_component_key(std::move(component_key)),
    m_renderer(renderer)
{
    if (!m_em || !m_em->is_alive(m_root_entity) || (state_store && state_store->is_locked(m_root_entity))) {
        m_valid = false;
        return;
    }
    if (!m_em->has_component<PrefabLinkComponent>(m_root_entity)) {
        m_valid = false;
        return;
    }
    m_prefab_filepath = m_em->get_component<PrefabLinkComponent>(m_root_entity).prefab_asset_path;
    m_previous_file_content = PrefabSerializer::read_json_from_file(m_prefab_filepath);
    if (m_previous_file_content.empty()) {
        m_valid = false;
        return;
    }
    m_description = "Apply Component '" + m_component_key + "' to Prefab Asset";
}

bool ApplyComponentPrefabCommand::execute() {
    if (!m_valid || !m_em || !m_em->is_alive(m_root_entity)) return false;

    if (m_first_execute) {
        try {
            nlohmann::json data = nlohmann::json::parse(m_previous_file_content);
            if (!data.contains("entities") || !data["entities"].is_array() || data["entities"].empty()) {
                return false;
            }
            auto& entities = data["entities"];
            auto it = std::find_if(entities.begin(), entities.end(), [](const nlohmann::json& e) {
                return e.value("id", 0) == 1;
            });
            auto& root_obj = (it != entities.end()) ? *it : entities[0];

            const ComponentRegistry& reg = default_component_registry();
            const ComponentDescriptor* desc = reg.find_by_key(m_component_key);
            if (!desc) return false;

            ComponentSerializationContext ctx{.renderer = m_renderer, .asset_manager = &AssetManager::get()};
            if (desc->has(*m_em, m_root_entity)) {
                nlohmann::json comp_json = desc->serialize(*m_em, m_root_entity, ctx);
                if (m_component_key == "transform") {
                    if (root_obj.contains("transform") && root_obj["transform"].contains("position")) {
                        comp_json["position"] = root_obj["transform"]["position"];
                    }
                }
                root_obj[m_component_key] = comp_json;
            } else {
                root_obj.erase(m_component_key);
            }

            m_new_file_content = data.dump(4);
        } catch (const std::exception& e) {
            ENGINE_LOG_ERROR("[PREFAB] ApplyComponentPrefabCommand error updating JSON: {}", e.what());
            return false;
        }

        if (!FileSystem::write_text_atomically(m_prefab_filepath, m_new_file_content)) {
            ENGINE_LOG_ERROR("[PREFAB] ApplyComponentPrefabCommand failed writing '{}'", m_prefab_filepath);
            return false;
        }

        m_first_execute = false;
        PrefabPropagation::propagate_to_instances(*m_em, m_prefab_filepath, m_root_entity, m_renderer);
        return true;
    }

    // Redo
    if (!FileSystem::write_text_atomically(m_prefab_filepath, m_new_file_content)) {
        return false;
    }
    PrefabPropagation::propagate_to_instances(*m_em, m_prefab_filepath, 0, m_renderer);
    return true;
}

bool ApplyComponentPrefabCommand::undo() {
    if (!m_valid || !m_em) return false;

    if (!FileSystem::write_text_atomically(m_prefab_filepath, m_previous_file_content)) {
        return false;
    }
    PrefabPropagation::propagate_to_instances(*m_em, m_prefab_filepath, 0, m_renderer);
    return true;
}

CreatePrefabVariantCommand::CreatePrefabVariantCommand(
    EntityManager* em,
    EntityID root_entity,
    std::string variant_filepath,
    SDL_Renderer* renderer,
    const EditorEntityStateStore* state_store
)
    : m_em(em),
      m_root_entity(root_entity),
      m_variant_filepath(std::move(variant_filepath)),
      m_renderer(renderer)
{
    (void)state_store;
    if (!m_em || !m_em->is_alive(m_root_entity) || m_variant_filepath.empty()) {
        m_valid = false;
        return;
    }

    if (m_em->has_component<PrefabLinkComponent>(m_root_entity)) {
        m_previous_link = m_em->get_component<PrefabLinkComponent>(m_root_entity);
        m_had_previous_link = true;
        m_base_prefab_path = m_previous_link.prefab_asset_path;
    } else {
        m_valid = false;
        return;
    }

    if (m_base_prefab_path.empty() || m_base_prefab_path == m_variant_filepath) {
        m_valid = false;
        return;
    }

    if (EntitySubtreeArchive::has_prefab_nesting_cycle(m_base_prefab_path, m_variant_filepath)) {
        ENGINE_LOG_ERROR("[PREFAB] CreatePrefabVariantCommand: nesting cycle between '{}' and '{}'", m_variant_filepath, m_base_prefab_path);
        m_valid = false;
        return;
    }

    m_description = "Create Prefab Variant: " + m_variant_filepath;
}

bool CreatePrefabVariantCommand::execute() {
    if (!m_valid || !m_em || !m_em->is_alive(m_root_entity)) return false;

    if (m_first_execute) {
        if (!PrefabSerializer::save_variant_to_file(*m_em, m_root_entity, m_variant_filepath, m_base_prefab_path, m_renderer)) {
            ENGINE_LOG_ERROR("[PREFAB] CreatePrefabVariantCommand failed saving variant to '{}'", m_variant_filepath);
            return false;
        }
        m_first_execute = false;
    }

    auto& link = m_em->get_component<PrefabLinkComponent>(m_root_entity);
    link.prefab_asset_path = m_variant_filepath;
    link.base_prefab_path = m_base_prefab_path;
    return true;
}

bool CreatePrefabVariantCommand::undo() {
    if (!m_valid || !m_em || !m_em->is_alive(m_root_entity)) return false;

    if (m_had_previous_link) {
        if (!m_em->has_component<PrefabLinkComponent>(m_root_entity)) {
            m_em->add_component<PrefabLinkComponent>(m_root_entity, m_previous_link);
        } else {
            m_em->get_component<PrefabLinkComponent>(m_root_entity) = m_previous_link;
        }
    } else if (m_em->has_component<PrefabLinkComponent>(m_root_entity)) {
        m_em->remove_component<PrefabLinkComponent>(m_root_entity);
    }
    return true;
}

} // namespace Engine


