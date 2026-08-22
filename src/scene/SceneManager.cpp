#include "SceneManager.hpp"
#include "SceneSerializer.hpp"
#include "../renderer/material/MaterialAsset.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../core/AssetDatabase.hpp"
#include "../core/Log.hpp"
#include "../core/Time.hpp"
#include "../ecs/TransformSystem.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include <unordered_map>

namespace Engine {

namespace {
    const char* SCENE_STATE_PRIMARY = "build/.scene_state";
    const char* SCENE_STATE_FALLBACK = ".scene_state";

    using Json = nlohmann::json;

    std::string json_pointer_token(std::string value) {
        std::string escaped;
        escaped.reserve(value.size());
        for (const char character : value) {
            if (character == '~') escaped += "~0";
            else if (character == '/') escaped += "~1";
            else escaped += character;
        }
        return escaped;
    }

    std::string property_child(const std::string& property, const std::string& child) {
        return property.empty() ? child : property + "." + child;
    }

    void collect_json_changes(
        const Json* before,
        const Json* after,
        EntityID entity,
        const std::string& component,
        const std::string& pointer,
        const std::string& property,
        std::vector<PlayModePropertyChange>& changes
    ) {
        if (before && after && *before == *after) return;
        if (before && after && before->is_object() && after->is_object()) {
            std::set<std::string> keys;
            for (const auto& [key, value] : before->items()) {
                (void)value;
                keys.insert(key);
            }
            for (const auto& [key, value] : after->items()) {
                (void)value;
                keys.insert(key);
            }
            for (const std::string& key : keys) {
                const auto before_value = before->find(key);
                const auto after_value = after->find(key);
                collect_json_changes(
                    before_value == before->end() ? nullptr : &*before_value,
                    after_value == after->end() ? nullptr : &*after_value,
                    entity,
                    component,
                    pointer + "/" + json_pointer_token(key),
                    property_child(property, key),
                    changes
                );
            }
            return;
        }
        if (before && after && before->is_array() && after->is_array() &&
            before->size() == after->size()) {
            for (std::size_t index = 0; index < before->size(); ++index) {
                collect_json_changes(
                    &(*before)[index],
                    &(*after)[index],
                    entity,
                    component,
                    pointer + "/" + std::to_string(index),
                    property + "[" + std::to_string(index) + "]",
                    changes
                );
            }
            return;
        }

        PlayModePropertyChange change;
        change.id = changes.size();
        change.entity = entity;
        change.component = component;
        change.property = property.empty() ? "value" : property;
        change.before_value = before ? before->dump() : "<unset>";
        change.after_value = after ? after->dump() : "<unset>";
        change.json_pointer = pointer;
        change.existed_after = after != nullptr;
        changes.push_back(std::move(change));
    }

    Json* find_entity(Json& root, EntityID entity) {
        if (!root.contains("entities") || !root["entities"].is_array()) return nullptr;
        for (Json& candidate : root["entities"]) {
            if (candidate.is_object() && candidate.value("id", EntityID{0}) == entity) {
                return &candidate;
            }
        }
        return nullptr;
    }

    bool erase_json_pointer(Json& value, const std::string& pointer) {
        const std::size_t separator = pointer.find_last_of('/');
        if (separator == std::string::npos) return false;
        const std::string parent_pointer = pointer.substr(0, separator);
        const std::string encoded_key = pointer.substr(separator + 1);
        std::string key;
        for (std::size_t index = 0; index < encoded_key.size(); ++index) {
            if (encoded_key[index] == '~' && index + 1 < encoded_key.size()) {
                if (encoded_key[index + 1] == '0') { key += '~'; ++index; continue; }
                if (encoded_key[index + 1] == '1') { key += '/'; ++index; continue; }
            }
            key += encoded_key[index];
        }
        Json& parent = parent_pointer.empty()
            ? value : value.at(Json::json_pointer(parent_pointer));
        if (!parent.is_object()) return false;
        return parent.erase(key) > 0;
    }
}

MaterialAssetRefreshResult SceneManager::refresh_material_assets(const AssetDatabase& database) {
    MaterialAssetRefreshResult result;
    const auto refresh = [&result, &database](const std::shared_ptr<Scene>& scene) {
        if (!scene) return;
        const auto refreshed = MaterialAsset::refresh_assigned(scene->get_entity_manager(), database);
        result.reloaded += refreshed.reloaded;
        result.unavailable += refreshed.unavailable;
        if (scene->supports_render_layer_effects()) {
            static_cast<void>(scene->refresh_render_layer_effects(database));
        }
    };
    for (const auto& scene : m_scenes) refresh(scene);
    refresh(m_transient_scene);
    refresh(m_runtime_scene);
    return result;
}

std::size_t SceneManager::preview_material_asset(
    AssetHandle handle,
    std::shared_ptr<const Material> material
) {
    std::size_t count = 0;
    const auto preview = [&](const std::shared_ptr<Scene>& scene) {
        if (scene) count += MaterialAsset::preview_assigned(
            scene->get_entity_manager(), handle, material
        );
    };
    for (const auto& scene : m_scenes) preview(scene);
    preview(m_transient_scene);
    preview(m_runtime_scene);
    return count;
}

void SceneManager::save_state() {
    if (m_active_index < 0 || static_cast<size_t>(m_active_index) >= m_scenes.size()) return;

    std::ofstream out(SCENE_STATE_PRIMARY);
    if (!out.is_open()) {
        out.open(SCENE_STATE_FALLBACK);
    }
    if (out.is_open()) {
        out << m_scenes[m_active_index]->get_name() << "\n";
    }
}

void SceneManager::restore_state() {
    std::ifstream in(SCENE_STATE_PRIMARY);
    if (!in.is_open()) {
        in.open(SCENE_STATE_FALLBACK);
    }
    if (in.is_open()) {
        std::string name;
        if (std::getline(in, name) && !name.empty()) {
            set_active_scene(name);
        }
    }
}

void SceneManager::add_scene(std::shared_ptr<Scene> scene) {
    if (!scene) return;
    
    // Check if scene with same name already exists: replace it instead of duplicating!
    for (size_t i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i] && m_scenes[i]->get_name() == scene->get_name()) {
            if (m_active_index == static_cast<int>(i)) {
                m_scenes[i]->on_detach();
            }
            m_scenes[i] = scene;
            if (m_active_index == static_cast<int>(i)) {
                m_scenes[i]->set_is_editing(is_editing());
                m_scenes[i]->on_attach();
                apply_active_view();
            }
            return;
        }
    }

    m_scenes.push_back(scene);
    
    // Automatically activate first registered scene
    if (m_active_index < 0) {
        set_active_scene(0);
    }
}

void SceneManager::set_active_scene(size_t index) {
    if (index >= m_scenes.size()) return;
    if (m_active_index == static_cast<int>(index)) return; // Prevent redundant double attach!

    m_play_mode_changes.clear();

    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        m_scenes[m_active_index]->on_detach();
    }

    m_active_index = static_cast<int>(index);
    m_scenes[m_active_index]->set_is_editing(is_editing());
    m_scenes[m_active_index]->on_attach();
    apply_active_view();

    save_state();

    if (m_on_authoring_state_invalidated) m_on_authoring_state_invalidated();

    ENGINE_LOG_INFO("Switched to Scene [{}/{}]: '{}'", m_active_index + 1, m_scenes.size(), m_scenes[m_active_index]->get_name());
}

void SceneManager::set_active_scene(const std::string& name) {
    for (size_t i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i]->get_name() == name) {
            set_active_scene(i);
            return;
        }
    }
}

void SceneManager::next_scene() {
    if (m_transient_scene) return; // blocked while in prefab stage
    if (m_scenes.empty()) return;
    size_t next_idx = (m_active_index + 1) % m_scenes.size();
    set_active_scene(next_idx);
}

void SceneManager::previous_scene() {
    if (m_transient_scene) return; // blocked while in prefab stage
    if (m_scenes.empty()) return;
    size_t prev_idx = (m_active_index - 1 + m_scenes.size()) % m_scenes.size();
    set_active_scene(prev_idx);
}

void SceneManager::start_play() {
    if (m_transient_scene) {
        ENGINE_LOG_WARN("[SCENE PLAY] Cannot enter Play mode while Prefab Stage is active.");
        return;
    }
    if (m_play_state == ScenePlayState::Pause) {
        resume_play();
        return;
    }

    Scene* active_scene = get_active_scene();
    if (!active_scene) return;

    m_play_mode_changes.clear();
    m_runtime_scene.reset();
    m_pending_runtime_scene.reset();

    // 1. Take in-memory snapshot of authoring scene state
    SceneSerializer serializer(active_scene, m_renderer);
    m_snapshot_json = serializer.serialize_to_string();

    // 2. Attach runtime physics simulation
    active_scene->on_detach();
    active_scene->set_is_editing(false);
    active_scene->on_attach();
    const auto& game = m_game_view;
    active_scene->on_viewport_resize(RenderViewKind::Game, game);

    m_play_state = ScenePlayState::Play;
    Time::set_time_scale(1.0f);
    start_scripts(*active_scene);
}

void SceneManager::start_scripts(Scene& scene) {
    ScriptRuntimeServices services = m_script_services;
    services.request_scene_load = [this](const std::string& path, std::string& error) {
        return request_runtime_scene_load(path, error);
    };
    services.current_scene_name = [this]() {
        const Scene* scene = get_active_scene();
        return scene ? scene->get_name() : std::string{};
    };

    const ScriptRuntimeStartResult scripts = m_script_runtime.start(
        scene.get_entity_manager(),
        m_asset_database ? *m_asset_database : AssetDatabase::get(),
        &scene.get_event_dispatcher(),
        std::move(services)
    );
    ENGINE_LOG_INFO(
        "[SCENE PLAY] Started Play simulation: {} scripts running, {} failed",
        scripts.started,
        scripts.failed
    );
}

bool SceneManager::request_runtime_scene_load(const std::string& path, std::string& error) {
    if (m_play_state == ScenePlayState::Edit) {
        error = "Scenes can only be loaded during Play mode";
        return false;
    }
    if (!m_runtime_scene_loader) {
        error = "Runtime scene loader is unavailable";
        return false;
    }

    std::shared_ptr<Scene> scene = m_runtime_scene_loader(path, error);
    if (!scene) {
        if (error.empty()) error = "Scene could not be loaded";
        return false;
    }
    m_pending_runtime_scene = std::move(scene);
    return true;
}

void SceneManager::activate_pending_runtime_scene() {
    if (!m_pending_runtime_scene || m_play_state == ScenePlayState::Edit) return;

    m_script_runtime.shutdown();
    if (Scene* current = get_active_scene()) current->on_detach();

    m_runtime_scene = std::move(m_pending_runtime_scene);
    m_runtime_scene->set_is_editing(false);
    m_runtime_scene->on_attach();
    m_runtime_scene->on_viewport_resize(RenderViewKind::Game, m_game_view);
    start_scripts(*m_runtime_scene);
    ENGINE_LOG_INFO("[SCENE PLAY] Loaded runtime scene '{}'.", m_runtime_scene->get_name());
}

void SceneManager::pause_play() {
    if (m_play_state != ScenePlayState::Play) return;

    m_play_state = ScenePlayState::Pause;
    Time::set_time_scale(0.0f);
    ENGINE_LOG_INFO("[SCENE PLAY] Simulation Paused.");
}

void SceneManager::resume_play() {
    if (m_play_state != ScenePlayState::Pause) return;

    m_play_state = ScenePlayState::Play;
    Time::set_time_scale(1.0f);
    ENGINE_LOG_INFO("[SCENE PLAY] Simulation Resumed.");
}

void SceneManager::stop_play() {
    if (m_play_state == ScenePlayState::Edit) return;

    std::string runtime_json;
    if (!m_runtime_scene) {
        if (Scene* runtime_authoring_scene = get_active_scene()) {
            SceneSerializer runtime_serializer(runtime_authoring_scene, m_renderer);
            runtime_json = runtime_serializer.serialize_to_string();
        }
    }

    m_script_runtime.shutdown();
    m_pending_runtime_scene.reset();
    const bool had_runtime_scene = m_runtime_scene != nullptr;
    if (had_runtime_scene) {
        m_runtime_scene->on_detach();
        m_runtime_scene.reset();
    }

    Scene* active_scene = nullptr;
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        active_scene = m_scenes[m_active_index].get();
    }
    if (active_scene) {
        if (!had_runtime_scene) active_scene->on_detach();
        active_scene->set_is_editing(true);

        // Restore pristine authoring state from memory snapshot
        if (!m_snapshot_json.empty()) {
            SceneSerializer serializer(active_scene, m_renderer);
            serializer.deserialize_from_string(m_snapshot_json);
            if (m_on_authoring_state_invalidated) m_on_authoring_state_invalidated();
        }

        TransformSystem::update(active_scene->get_entity_manager());
        const auto& editor = m_editor_view;
        active_scene->on_viewport_resize(RenderViewKind::Editor, editor);
    }

    collect_play_mode_changes(runtime_json);

    m_play_state = ScenePlayState::Edit;
    Time::set_time_scale(0.0f);
    ENGINE_LOG_INFO("[SCENE PLAY] Stopped Simulation: Authoring snapshot restored, switched back to Editor Camera.");
}

void SceneManager::collect_play_mode_changes(const std::string& runtime_json) {
    m_play_mode_changes.clear();
    if (m_snapshot_json.empty() || runtime_json.empty()) return;

    const Json authored = Json::parse(m_snapshot_json, nullptr, false);
    const Json runtime = Json::parse(runtime_json, nullptr, false);
    if (authored.is_discarded() || runtime.is_discarded() ||
        !authored.contains("entities") || !authored["entities"].is_array() ||
        !runtime.contains("entities") || !runtime["entities"].is_array()) return;

    std::unordered_map<EntityID, const Json*> runtime_entities;
    for (const Json& entity : runtime["entities"]) {
        if (!entity.is_object()) continue;
        const EntityID id = entity.value("id", EntityID{0});
        if (id != 0) runtime_entities.insert_or_assign(id, &entity);
    }

    for (const Json& entity : authored["entities"]) {
        if (!entity.is_object()) continue;
        const EntityID id = entity.value("id", EntityID{0});
        const auto runtime_entity = runtime_entities.find(id);
        if (id == 0 || runtime_entity == runtime_entities.end()) continue;
        for (const auto& [component, authored_value] : entity.items()) {
            if (component == "id") continue;
            const auto runtime_value = runtime_entity->second->find(component);
            if (runtime_value == runtime_entity->second->end()) continue;
            collect_json_changes(
                &authored_value,
                &*runtime_value,
                id,
                component,
                "/" + json_pointer_token(component),
                {},
                m_play_mode_changes
            );
        }
    }

    if (!m_play_mode_changes.empty()) {
        ENGINE_LOG_INFO(
            "[SCENE PLAY] Captured {} runtime property changes for optional apply-back",
            m_play_mode_changes.size()
        );
    }
}

void SceneManager::select_play_mode_change(std::size_t id, bool selected) {
    const auto found = std::find_if(
        m_play_mode_changes.begin(),
        m_play_mode_changes.end(),
        [id](const PlayModePropertyChange& change) { return change.id == id; }
    );
    if (found != m_play_mode_changes.end()) found->selected = selected;
}

void SceneManager::select_all_play_mode_changes(bool selected) {
    for (PlayModePropertyChange& change : m_play_mode_changes) change.selected = selected;
}

std::optional<PlayModeApplySnapshot> SceneManager::build_play_mode_apply_snapshot() {
    if (m_transient_scene) return std::nullopt;
    Scene* scene = get_active_scene();
    if (!scene || !is_editing()) return std::nullopt;

    SceneSerializer serializer(scene, m_renderer);
    const std::string before_json = serializer.serialize_to_string();
    Json after = Json::parse(before_json, nullptr, false);
    if (before_json.empty() || after.is_discarded()) return std::nullopt;

    std::size_t property_count = 0;
    for (const PlayModePropertyChange& change : m_play_mode_changes) {
        if (!change.selected) continue;
        Json* entity = find_entity(after, change.entity);
        if (!entity || !entity->contains(change.component)) continue;
        const std::string pointer = change.json_pointer;
        try {
            if (change.existed_after) {
                Json value = Json::parse(change.after_value, nullptr, false);
                if (value.is_discarded()) continue;
                (*entity)[Json::json_pointer(pointer)] = std::move(value);
            } else if (!erase_json_pointer(*entity, pointer)) {
                continue;
            }
            ++property_count;
        } catch (const std::exception&) {
            continue;
        }
    }
    if (property_count == 0) return std::nullopt;
    return PlayModeApplySnapshot{before_json, after.dump(4), property_count};
}

void SceneManager::update(float delta_time) {
    Scene* active = get_active_scene();
    if (active) {
        if (m_play_state == ScenePlayState::Play) {
            static_cast<void>(m_script_runtime.reload_changed_scripts());
            m_script_runtime.update(delta_time);
        }
        active->on_update(delta_time);
        if (m_play_state == ScenePlayState::Play) {
            m_script_runtime.dispatch_events();
            activate_pending_runtime_scene();
        }
    }
}

void SceneManager::render(Renderer2D& renderer) {
    renderer.clear_all_render_layer_effects();
    renderer.clear_all_entity_effect_profiles();
    Scene* active = get_active_scene();
    if (active) {
        active->on_render(renderer);
    }
}

void SceneManager::configure_view(RenderViewKind view, const RenderViewConfiguration& configuration) {
    RenderViewConfiguration& metrics = view == RenderViewKind::Editor ? m_editor_view : m_game_view;
    RenderViewConfiguration normalized = configuration;
    normalized.output_width = std::max(normalized.output_width, 1);
    normalized.output_height = std::max(normalized.output_height, 1);
    normalized.reference_width = std::max(normalized.reference_width, 1);
    normalized.reference_height = std::max(normalized.reference_height, 1);
    if (metrics == normalized) return;
    metrics = normalized;

    const RenderViewKind active_view = is_editing() ? RenderViewKind::Editor : RenderViewKind::Game;
    if (view == active_view) {
        Scene* active = get_active_scene();
        if (active) {
            active->on_viewport_resize(view, metrics);
        }
    }
}

void SceneManager::invalidate_render_resources() {
    for (const auto& scene : m_scenes) {
        if (scene) scene->on_render_resources_invalidated();
    }
    if (m_transient_scene) m_transient_scene->on_render_resources_invalidated();
    if (m_runtime_scene) m_runtime_scene->on_render_resources_invalidated();
}

void SceneManager::apply_active_view() {
    Scene* active = get_active_scene();
    if (!active) return;
    const RenderViewKind view = is_editing() ? RenderViewKind::Editor : RenderViewKind::Game;
    const RenderViewConfiguration& metrics = view == RenderViewKind::Editor ? m_editor_view : m_game_view;
    active->on_viewport_resize(view, metrics);
}


Scene* SceneManager::get_active_scene() const {
    if (m_transient_scene) return m_transient_scene.get();
    if (m_runtime_scene && m_play_state != ScenePlayState::Edit) return m_runtime_scene.get();
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        return m_scenes[m_active_index].get();
    }
    return nullptr;
}

std::shared_ptr<Scene> SceneManager::get_active_scene_shared_ptr() const {
    if (m_transient_scene) return m_transient_scene;
    if (m_runtime_scene && m_play_state != ScenePlayState::Edit) return m_runtime_scene;
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        return m_scenes[m_active_index];
    }
    return nullptr;
}

void SceneManager::enter_transient_scene(std::shared_ptr<Scene> stage) {
    if (!stage) return;
    if (m_transient_scene) {
        ENGINE_LOG_WARN("[SCENE] enter_transient_scene called while one is already active — ignoring.");
        return;
    }
    m_transient_scene = std::move(stage);
    m_transient_scene->set_is_editing(true);
    m_transient_scene->on_attach();
    m_transient_scene->on_viewport_resize(RenderViewKind::Editor, m_editor_view);
    ENGINE_LOG_INFO("[SCENE] Entered transient scene: '{}'", m_transient_scene->get_name());
}

void SceneManager::exit_transient_scene() {
    if (!m_transient_scene) return;
    m_transient_scene->on_detach();
    ENGINE_LOG_INFO("[SCENE] Exited transient scene: '{}'", m_transient_scene->get_name());
    m_transient_scene.reset();
    // Re-apply the underlying authoring scene view so it renders correctly.
    apply_active_view();
}

} // namespace Engine
