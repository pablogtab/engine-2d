#include "PrefabSerializer.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Log.hpp"
#include "../core/AssetManager.hpp"
#include "../core/AssetDatabase.hpp"
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Engine {

namespace {

std::string normalize_prefab_path(const std::string& path) {
    return std::filesystem::path(path).lexically_normal().generic_string();
}

std::string resolve_prefab_handle(const nlohmann::json& document, const char* key) {
    if (!document.contains(key) || !document[key].is_string()) return {};
    const auto handle = AssetHandle::from_string(document[key].get<std::string>());
    const AssetRecord* record = handle ? AssetDatabase::get().find(*handle) : nullptr;
    return record && record->status == AssetStatus::Ready && record->type == AssetType::Prefab
        ? record->source_path : std::string{};
}

std::optional<nlohmann::json> make_merge_patch(const nlohmann::json& base, const nlohmann::json& target) {
    if (base == target) return std::nullopt;
    if (!base.is_object() || !target.is_object()) return target;

    nlohmann::json patch = nlohmann::json::object();
    for (auto it = base.begin(); it != base.end(); ++it) {
        if (!target.contains(it.key())) patch[it.key()] = nullptr;
    }
    for (auto it = target.begin(); it != target.end(); ++it) {
        if (!base.contains(it.key())) {
            patch[it.key()] = it.value();
            continue;
        }
        const auto child = make_merge_patch(base.at(it.key()), it.value());
        if (child.has_value()) patch[it.key()] = *child;
    }
    return patch;
}

// JSON Merge Patch treats arrays as indivisible values. Prefab entities are an
// array, so using it directly would turn a one-property variant change into a
// snapshot of the complete prefab. Keep overrides keyed by stable local IDs
// instead, allowing future base changes to flow through untouched entities.
nlohmann::json make_variant_overrides(const nlohmann::json& base, const nlohmann::json& target) {
    nlohmann::json base_root = base;
    nlohmann::json target_root = target;
    const nlohmann::json base_entities = base_root.value("entities", nlohmann::json::array());
    const nlohmann::json target_entities = target_root.value("entities", nlohmann::json::array());
    base_root.erase("entities");
    target_root.erase("entities");
    // Provenance belongs to the variant wrapper, never to an inherited scene
    // payload.
    base_root.erase("base_prefab_path");
    target_root.erase("base_prefab_path");

    nlohmann::json result = {
        {"root_overrides", make_merge_patch(base_root, target_root).value_or(nlohmann::json::object())},
        {"entity_overrides", nlohmann::json::object()},
        {"added_entities", nlohmann::json::array()},
        {"removed_entity_ids", nlohmann::json::array()}
    };

    std::unordered_map<EntityID, nlohmann::json> base_by_id;
    for (const auto& entity : base_entities) {
        if (entity.is_object() && entity.contains("id") && entity["id"].is_number_unsigned()) {
            base_by_id.emplace(entity["id"].get<EntityID>(), entity);
        }
    }
    std::unordered_set<EntityID> target_ids;
    for (const auto& entity : target_entities) {
        if (!entity.is_object() || !entity.contains("id") || !entity["id"].is_number_unsigned()) continue;
        const EntityID id = entity["id"].get<EntityID>();
        target_ids.insert(id);
        const auto base_it = base_by_id.find(id);
        if (base_it == base_by_id.end()) {
            result["added_entities"].push_back(entity);
            continue;
        }
        nlohmann::json base_entity = base_it->second;
        nlohmann::json target_entity = entity;
        base_entity.erase("id");
        target_entity.erase("id");
        const auto patch = make_merge_patch(base_entity, target_entity);
        if (patch.has_value()) result["entity_overrides"][std::to_string(id)] = *patch;
    }
    for (const auto& [id, entity] : base_by_id) {
        if (!target_ids.contains(id)) result["removed_entity_ids"].push_back(id);
    }
    return result;
}

bool apply_variant_overrides(nlohmann::json& resolved, const nlohmann::json& overrides) {
    if (!overrides.is_object()) return false;
    if (!overrides.contains("entity_overrides")) {
        // Compatibility with the short-lived generic merge-patch variant
        // format. Newly written variants always use the entity-aware form.
        resolved.merge_patch(overrides);
        return true;
    }

    if (overrides.contains("root_overrides")) {
        if (!overrides["root_overrides"].is_object()) return false;
        resolved.merge_patch(overrides["root_overrides"]);
    }
    if (!resolved.contains("entities") || !resolved["entities"].is_array()) return false;

    std::unordered_map<EntityID, std::size_t> entity_indices;
    for (std::size_t index = 0; index < resolved["entities"].size(); ++index) {
        const auto& entity = resolved["entities"][index];
        if (entity.is_object() && entity.contains("id") && entity["id"].is_number_unsigned()) {
            entity_indices.emplace(entity["id"].get<EntityID>(), index);
        }
    }
    if (overrides.contains("removed_entity_ids")) {
        if (!overrides["removed_entity_ids"].is_array()) return false;
        std::unordered_set<EntityID> removed;
        for (const auto& id : overrides["removed_entity_ids"]) {
            if (!id.is_number_unsigned()) return false;
            removed.insert(id.get<EntityID>());
        }
        nlohmann::json retained = nlohmann::json::array();
        for (const auto& entity : resolved["entities"]) {
            if (!entity.contains("id") || !removed.contains(entity["id"].get<EntityID>())) retained.push_back(entity);
        }
        resolved["entities"] = std::move(retained);
        entity_indices.clear();
        for (std::size_t index = 0; index < resolved["entities"].size(); ++index) {
            entity_indices.emplace(resolved["entities"][index]["id"].get<EntityID>(), index);
        }
    }
    const auto& entity_overrides = overrides["entity_overrides"];
    if (!entity_overrides.is_object()) return false;
    for (auto it = entity_overrides.begin(); it != entity_overrides.end(); ++it) {
        EntityID id{};
        try { id = static_cast<EntityID>(std::stoull(it.key())); } catch (...) { return false; }
        const auto entity_it = entity_indices.find(id);
        if (entity_it == entity_indices.end() || !it.value().is_object()) return false;
        resolved["entities"][entity_it->second].merge_patch(it.value());
        resolved["entities"][entity_it->second]["id"] = id;
    }
    if (overrides.contains("added_entities")) {
        if (!overrides["added_entities"].is_array()) return false;
        for (const auto& entity : overrides["added_entities"]) {
            if (!entity.is_object() || !entity.contains("id") || !entity["id"].is_number_unsigned()) return false;
            const EntityID id = entity["id"].get<EntityID>();
            if (entity_indices.contains(id)) return false;
            resolved["entities"].push_back(entity);
            entity_indices.emplace(id, resolved["entities"].size() - 1);
        }
    }
    return true;
}

std::string resolve_prefab_json(const std::string& filepath, std::unordered_set<std::string>& visiting) {
    const std::string normalized_path = normalize_prefab_path(filepath);
    if (!visiting.insert(normalized_path).second) {
        ENGINE_LOG_ERROR("[PREFAB] Variant resolution cycle at '{}'", normalized_path);
        return {};
    }

    const std::string raw = FileSystem::read_text(normalized_path);
    if (raw.empty()) return {};
    try {
        nlohmann::json document = nlohmann::json::parse(raw);
        if (!document.contains("base_asset_handle")) {
            return document.dump(4);
        }
        const std::string base_path = normalize_prefab_path(resolve_prefab_handle(document, "base_asset_handle"));
        if (base_path.empty()) {
            ENGINE_LOG_ERROR("[PREFAB] Variant '{}' references an unavailable base prefab", normalized_path);
            return {};
        }
        const std::string base_text = resolve_prefab_json(base_path, visiting);
        if (base_text.empty()) return {};
        nlohmann::json resolved = nlohmann::json::parse(base_text);
        if (!document.contains("variant_overrides") || !document["variant_overrides"].is_object()) {
            ENGINE_LOG_ERROR("[PREFAB] Variant '{}' missing variant_overrides", normalized_path);
            return {};
        }
        if (!apply_variant_overrides(resolved, document["variant_overrides"])) {
            ENGINE_LOG_ERROR("[PREFAB] Invalid variant overrides in '{}'", normalized_path);
            return {};
        }
        // Keep provenance for instantiation metadata. EntitySubtreeArchive ignores
        // this field while reading entities, but returns it to PrefabLinkComponent.
        resolved["base_prefab_path"] = base_path;
        return resolved.dump(4);
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[PREFAB] Failed resolving '{}': {}", normalized_path, e.what());
        return {};
    }
}

} // namespace

bool PrefabSerializer::save_to_file(
    const EntityManager& em,
    EntityID root_entity,
    const std::string& filepath,
    SDL_Renderer* renderer,
    const ComponentRegistry* registry
) {
    if (filepath.empty()) {
        ENGINE_LOG_ERROR("[PREFAB] Cannot save prefab: empty filepath");
        return false;
    }

    ComponentSerializationContext context{
        .renderer = renderer,
        .asset_manager = &AssetManager::get(),
        .asset_database = &AssetDatabase::get()
    };

    std::string json_text = EntitySubtreeArchive::capture_to_json(em, root_entity, context, registry);
    if (json_text.empty()) {
        ENGINE_LOG_ERROR("[PREFAB] Failed capturing subtree for root entity #{}", root_entity);
        return false;
    }

    try {
        std::filesystem::path p(filepath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[PREFAB] Failed creating directory for '{}': {}", filepath, e.what());
        return false;
    }

    if (!FileSystem::write_text_atomically(filepath, json_text)) {
        ENGINE_LOG_ERROR("[PREFAB] Failed writing prefab JSON atomically to '{}'", filepath);
        return false;
    }
    if (!AssetDatabase::get().import_asset(filepath)) {
        ENGINE_LOG_ERROR("[PREFAB] Failed registering saved prefab '{}'", filepath);
        return false;
    }

    ENGINE_LOG_INFO("[PREFAB] Successfully saved prefab to '{}'", filepath);
    return true;
}

bool PrefabSerializer::save_variant_to_file(
    const EntityManager& em,
    EntityID root_entity,
    const std::string& filepath,
    const std::string& base_prefab_path,
    SDL_Renderer* renderer,
    const ComponentRegistry* registry
) {
    if (filepath.empty() || base_prefab_path.empty()) {
        ENGINE_LOG_ERROR("[PREFAB] Cannot save prefab variant: empty filepath or base_prefab_path");
        return false;
    }

    if (EntitySubtreeArchive::has_prefab_nesting_cycle(filepath, base_prefab_path)) {
        ENGINE_LOG_ERROR("[PREFAB] Cannot save prefab variant: cycle detected between '{}' and '{}'", filepath, base_prefab_path);
        return false;
    }


    ComponentSerializationContext context{
        .renderer = renderer,
        .asset_manager = &AssetManager::get(),
        .asset_database = &AssetDatabase::get()
    };

    const std::string resolved_base_text = read_json_from_file(base_prefab_path);
    const std::string resolved_variant_text = EntitySubtreeArchive::capture_to_json(em, root_entity, context, registry);
    if (resolved_base_text.empty() || resolved_variant_text.empty()) {
        ENGINE_LOG_ERROR("[PREFAB] Failed capturing subtree for variant root entity #{}", root_entity);
        return false;
    }

    nlohmann::json base_document;
    nlohmann::json variant_document;
    try {
        base_document = nlohmann::json::parse(resolved_base_text);
        variant_document = nlohmann::json::parse(resolved_variant_text);
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[PREFAB] Failed preparing variant '{}': {}", filepath, e.what());
        return false;
    }
    const AssetRecord* base = AssetDatabase::get().find_by_path(base_prefab_path);
    if (!base || base->status != AssetStatus::Ready || base->type != AssetType::Prefab) {
        ENGINE_LOG_ERROR("[PREFAB] Cannot save prefab variant: base '{}' is not a registered prefab", base_prefab_path);
        return false;
    }
    nlohmann::json stored_document = {
        {"prefab_version", EntitySubtreeArchive::CURRENT_PREFAB_VERSION},
        {"base_asset_handle", base->handle.to_string()},
        {"variant_overrides", make_variant_overrides(base_document, variant_document)}
    };

    try {
        std::filesystem::path p(filepath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[PREFAB] Failed creating directory for '{}': {}", filepath, e.what());
        return false;
    }

    if (!FileSystem::write_text_atomically(filepath, stored_document.dump(4))) {
        ENGINE_LOG_ERROR("[PREFAB] Failed writing variant prefab JSON atomically to '{}'", filepath);
        return false;
    }
    if (!AssetDatabase::get().import_asset(filepath)) {
        ENGINE_LOG_ERROR("[PREFAB] Failed registering saved variant '{}'", filepath);
        return false;
    }

    ENGINE_LOG_INFO("[PREFAB] Successfully saved prefab variant to '{}' (Base: '{}')", filepath, base_prefab_path);
    return true;
}

bool PrefabSerializer::is_variant_file(const std::string& filepath, std::string* out_base_path) {
    if (filepath.empty() || !std::filesystem::exists(filepath)) return false;
    std::string json_text = FileSystem::read_text(filepath);
    if (json_text.empty()) return false;
    try {
        nlohmann::json root = nlohmann::json::parse(json_text);
        if (root.contains("base_asset_handle")) {
            std::string base = resolve_prefab_handle(root, "base_asset_handle");
            if (!base.empty()) {
                if (out_base_path) *out_base_path = base;
                return true;
            }
        }
    } catch (...) {}
    return false;
}

bool PrefabSerializer::write_resolved_to_file(const std::string& filepath, const std::string& resolved_json) {
    if (filepath.empty() || resolved_json.empty()) return false;
    try {
        const nlohmann::json resolved = nlohmann::json::parse(resolved_json);
        const std::string raw_text = FileSystem::read_text(filepath);
        if (raw_text.empty()) return FileSystem::write_text_atomically(filepath, resolved.dump(4));
        const nlohmann::json raw = nlohmann::json::parse(raw_text);
        if (!raw.contains("base_asset_handle")) {
            return FileSystem::write_text_atomically(filepath, resolved.dump(4));
        }
        const std::string base_path = normalize_prefab_path(resolve_prefab_handle(raw, "base_asset_handle"));
        if (base_path.empty()) return false;
        const AssetRecord* base_record = AssetDatabase::get().find_by_path(base_path);
        if (!base_record) return false;
        const std::string base_text = read_json_from_file(base_path);
        if (base_text.empty()) return false;
        const nlohmann::json base = nlohmann::json::parse(base_text);
        const nlohmann::json variant = {
            {"prefab_version", EntitySubtreeArchive::CURRENT_PREFAB_VERSION},
            {"base_asset_handle", base_record->handle.to_string()},
            {"variant_overrides", make_variant_overrides(base, resolved)}
        };
        return FileSystem::write_text_atomically(filepath, variant.dump(4));
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[PREFAB] Failed writing resolved prefab '{}': {}", filepath, e.what());
        return false;
    }
}

std::string PrefabSerializer::read_json_from_file(const std::string& filepath) {
    std::unordered_set<std::string> visiting;
    return resolve_prefab_json(filepath, visiting);
}

} // namespace Engine
