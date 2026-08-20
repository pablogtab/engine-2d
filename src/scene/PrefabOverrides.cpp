#include "PrefabOverrides.hpp"
#include "PrefabSerializer.hpp"
#include "../core/Log.hpp"
#include "../core/AssetManager.hpp"
#include <cmath>

namespace Engine {

nlohmann::json PrefabOverrides::get_master_component_json(
    const std::string& prefab_asset_path,
    const std::string& component_key,
    EntityID source_local_id
) {
    if (prefab_asset_path.empty()) return nullptr;

    const std::string json_text = PrefabSerializer::read_json_from_file(prefab_asset_path);
    if (json_text.empty()) return nullptr;

    try {
        nlohmann::json data = nlohmann::json::parse(json_text);
        if (!data.contains("entities") || !data["entities"].is_array() || data["entities"].empty()) {
            return nullptr;
        }

        const auto& entities = data["entities"];
        const auto it = std::find_if(entities.begin(), entities.end(), [source_local_id](const nlohmann::json& e) {
            return e.value("id", 0) == source_local_id;
        });
        if (it == entities.end()) return nullptr;
        const nlohmann::json& root_obj = *it;

        if (root_obj.contains(component_key)) {
            return root_obj[component_key];
        }
    } catch (const std::exception& e) {
        ENGINE_LOG_WARN("[PREFAB] Error parsing master prefab '{}': {}", prefab_asset_path, e.what());
    }

    return nullptr;
}

bool PrefabOverrides::json_differs(
    const std::string& component_key,
    const nlohmann::json& live_json,
    const nlohmann::json& master_json,
    bool is_root_entity
) {
    if (live_json.is_null() && master_json.is_null()) return false;
    if (live_json.is_null() || master_json.is_null()) return true;

    if (component_key == "transform" && is_root_entity) {
        // For root entities in a scene, world placement position is expected to differ.
        // We compare scale, rotation, pivot, visible, and skew.
        if (live_json.contains("scale") && master_json.contains("scale")) {
            if (live_json["scale"] != master_json["scale"]) return true;
        }
        if (live_json.contains("rotation") && master_json.contains("rotation")) {
            try {
                float r1 = live_json.value("rotation", 0.0f);
                float r2 = master_json.value("rotation", 0.0f);
                if (std::abs(r1 - r2) > 0.001f) return true;
            } catch (...) {
                if (live_json["rotation"] != master_json["rotation"]) return true;
            }
        }
        if (live_json.contains("pivot") && master_json.contains("pivot")) {
            if (live_json["pivot"] != master_json["pivot"]) return true;
        }
        if (live_json.contains("visible") && master_json.contains("visible")) {
            if (live_json["visible"] != master_json["visible"]) return true;
        }
        return false;
    }


    return live_json != master_json;
}

bool PrefabOverrides::is_component_overridden(
    const EntityManager& em,
    EntityID entity,
    const std::string& component_key,
    const ComponentRegistry* registry
) {
    if (!em.is_alive(entity) || !em.has_component<PrefabLinkComponent>(entity)) {
        return false;
    }

    const auto& link = em.get_component<PrefabLinkComponent>(entity);
    nlohmann::json master_json = get_master_component_json(link.prefab_asset_path, component_key, link.source_local_id);

    const ComponentRegistry& reg = registry ? *registry : default_component_registry();
    const ComponentDescriptor* desc = reg.find_by_key(component_key);
    if (!desc) return false;


    ComponentSerializationContext ctx{
        .renderer = nullptr,
        .asset_manager = &AssetManager::get()
    };

    nlohmann::json live_json = desc->has(em, entity) ? desc->serialize(em, entity, ctx) : nlohmann::json(nullptr);
    return json_differs(component_key, live_json, master_json, link.is_root_instance);
}

std::vector<std::string> PrefabOverrides::get_overridden_components(
    const EntityManager& em,
    EntityID entity,
    const ComponentRegistry* registry
) {
    if (!em.is_alive(entity) || !em.has_component<PrefabLinkComponent>(entity)) {
        return {};
    }

    const auto& link = em.get_component<PrefabLinkComponent>(entity);
    if (link.prefab_asset_path.empty()) return {};

    const std::string json_text = PrefabSerializer::read_json_from_file(link.prefab_asset_path);
    if (json_text.empty()) return {};

    nlohmann::json root_obj = nullptr;
    try {
        nlohmann::json data = nlohmann::json::parse(json_text);
        if (data.contains("entities") && data["entities"].is_array() && !data["entities"].empty()) {
            const auto& entities = data["entities"];
            const auto it = std::find_if(entities.begin(), entities.end(), [&link](const nlohmann::json& e) {
                return e.value("id", 0) == link.source_local_id;
            });
            if (it != entities.end()) root_obj = *it;
        }
    } catch (const std::exception& e) {
        ENGINE_LOG_WARN("[PREFAB] Error parsing master prefab '{}': {}", link.prefab_asset_path, e.what());
        return {};
    }

    if (root_obj.is_null()) return {};

    const ComponentRegistry& reg = registry ? *registry : default_component_registry();
    ComponentSerializationContext ctx{
        .renderer = nullptr,
        .asset_manager = &AssetManager::get()
    };

    std::vector<std::string> overridden_keys;
    for (const auto& desc : reg.descriptors()) {
        const std::string& key = desc.serialized_key;
        if (key == "prefab_link" || key == "hierarchy") continue;

        nlohmann::json live_json = desc.has(em, entity) ? desc.serialize(em, entity, ctx) : nlohmann::json(nullptr);
        nlohmann::json master_json = root_obj.contains(key) ? root_obj[key] : nlohmann::json(nullptr);

        if (json_differs(key, live_json, master_json, link.is_root_instance)) {
            overridden_keys.push_back(key);
        }
    }

    return overridden_keys;
}

} // namespace Engine
