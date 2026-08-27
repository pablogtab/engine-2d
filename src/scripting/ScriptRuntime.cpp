#include "ScriptRuntime.hpp"

#include "../core/AssetDatabase.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Input.hpp"
#include "../core/Log.hpp"
#include "../core/PlayerPrefs.hpp"
#include "../core/Time.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/AnimationComponents.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include "../ecs/components/ParticleComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../ecs/components/UIComponents.hpp"
#include "../tilemap/Tilemap.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../particles/ParticleEffectAsset.hpp"
#include "../renderer/SpriteAnimationSystem.hpp"
#include "../events/Event.hpp"
#include "../events/EventDispatcher.hpp"

#include "../enginescript/editor/EngineScriptWorkspace.hpp"
#include "../enginescript/async/AsyncScheduler.hpp"
#include "../enginescript/module/GameModule.hpp"
#include "../enginescript/module/NativeModuleBuilder.hpp"
#include "../enginescript/runtime/Collections.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <unordered_map>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <variant>
#include <nlohmann/json.hpp>

namespace Engine {

struct ScriptRuntimeBinding {
    ScriptRuntime* runtime{nullptr};
    EntityManager* entity_manager{nullptr};
    EventDispatcher* event_dispatcher{nullptr};
    const AssetDatabase* asset_database{nullptr};
    EntityID entity{0};
    ScriptSlotID slot_id{0};
    const ScriptRuntimeServices* services{nullptr};
    EngineScript::AsyncScheduler* async_scheduler{nullptr};
    EngineScript::TypedEventBus* typed_event_bus{nullptr};
    std::thread::id main_thread_id;
    void* native_component_context{nullptr};
    EngineScript::ComponentReference (*find_native_component)(void*, EntityID, const char*, std::size_t){nullptr};
    std::size_t (*count_native_components)(void*, EntityID, const char*){nullptr};
};

void require_main_thread(const ScriptRuntimeBinding& binding) {
    if (binding.main_thread_id != std::thread::id{} && std::this_thread::get_id() != binding.main_thread_id) {
        throw EngineScript::ScriptRuntimeError("EngineScript ECS access is restricted to the main thread");
    }
}

void bind_dynamic_component_access(EngineScript::Entity& entity, const ScriptRuntimeBinding& binding) {
    entity.component_context = const_cast<ScriptRuntimeBinding*>(&binding);
    entity.find_component_callback = [](void* context, uint32_t entity_id, const char* type_name, std::size_t index) {
        auto* binding = static_cast<ScriptRuntimeBinding*>(context);
        if (!binding) return EngineScript::ComponentReference{};
        require_main_thread(*binding);
        if (binding->find_native_component && binding->native_component_context && type_name) {
            EngineScript::ComponentReference native = binding->find_native_component(
                binding->native_component_context, entity_id, type_name, index
            );
            if (native.resolve_callback) return native;
        }
        auto* runtime = binding->runtime;
        return runtime && type_name
            ? runtime->find_behaviour_component(entity_id, type_name, index)
            : EngineScript::ComponentReference{};
    };
    entity.count_components_callback = [](void* context, uint32_t entity_id, const char* type_name) {
        const auto* binding = static_cast<const ScriptRuntimeBinding*>(context);
        if (!binding) return std::size_t{0};
        require_main_thread(*binding);
        if (binding->count_native_components && binding->native_component_context && type_name) {
            const std::size_t native = binding->count_native_components(
                binding->native_component_context, entity_id, type_name
            );
            if (native != 0) return native;
        }
        const auto* runtime = binding->runtime;
        return runtime && type_name
            ? runtime->count_behaviour_components(entity_id, type_name)
            : std::size_t{0};
    };
    entity.add_component_callback = [](void* context, uint32_t entity_id, const char* type_name) {
        auto* binding = static_cast<ScriptRuntimeBinding*>(context);
        if (!binding) return EngineScript::ComponentReference{};
        require_main_thread(*binding);
        auto* runtime = binding->runtime;
        return runtime && type_name
            ? runtime->add_behaviour_component(entity_id, type_name)
            : EngineScript::ComponentReference{};
    };
}

class NativeBehaviourHost {
public:
    explicit NativeBehaviourHost(ScriptRuntimeBinding& binding)
        : m_binding(binding), m_input(binding), m_log(), m_audio(binding), m_scene(binding),
          m_prefabs(binding), m_events(binding.typed_event_bus, binding.slot_id), m_prefs(), m_assets(binding),
          m_particles(binding), m_world_text(binding), m_ui(binding), m_tilemap(binding), m_display(binding),
          m_physics(binding), m_animator(binding), m_jobs(binding.async_scheduler, binding.slot_id),
          m_task_scope(binding.async_scheduler, binding.slot_id) {
        m_binding.native_component_context = &m_entities;
        m_binding.find_native_component = [](void* context, EntityID entity, const char* type, std::size_t index) {
            return static_cast<EntityService*>(context)->component_reference(entity, type, index);
        };
        m_binding.count_native_components = [](void* context, EntityID entity, const char* type) {
            return static_cast<EntityService*>(context)->component_count(entity, type);
        };
    }

    bool prepare(const EngineScript::BehaviourTypeRegistration& registration, std::string& error) {
        m_component_references.reserve(registration.constructor_dependencies.size());
        m_arguments.reserve(registration.constructor_dependencies.size());
        for (const auto& [type, kind] : registration.constructor_dependencies) {
            if (type == "Input") m_arguments.push_back(&m_input);
            else if (type == "Log") m_arguments.push_back(&m_log);
            else if (type == "Audio") m_arguments.push_back(&m_audio);
            else if (type == "Scene") m_arguments.push_back(&m_scene);
            else if (type == "Prefabs") m_arguments.push_back(&m_prefabs);
            else if (type == "Events") m_arguments.push_back(&m_events);
            else if (type == "Jobs") m_arguments.push_back(&m_jobs);
            else if (type == "TaskScope") m_arguments.push_back(&m_task_scope);
            else if (type == "PlayerPrefs") m_arguments.push_back(&m_prefs);
            else if (type == "Assets") m_arguments.push_back(&m_assets);
            else if (type == "Particles") m_arguments.push_back(&m_particles);
            else if (type == "WorldText") m_arguments.push_back(&m_world_text);
            else if (type == "UI") m_arguments.push_back(&m_ui);
            else if (type == "Tilemap") m_arguments.push_back(&m_tilemap);
            else if (type == "RigidBody2D") m_arguments.push_back(&m_rigid_body);
            else if (type == "SpriteRenderer2D") m_arguments.push_back(&m_sprite);
            else if (type == "Animator2D") m_arguments.push_back(&m_animator);
            else if (type == "Transform2D") m_arguments.push_back(&m_transform);
            else if (type == "Time") m_arguments.push_back(&m_time);
            else if (type == "Entities") m_arguments.push_back(&m_entities);
            else if (type == "Display") m_arguments.push_back(&m_display);
            else if (type == "Physics") m_arguments.push_back(&m_physics);
            else if (type == "ParticleEmitter2D") {
                EngineScript::Entity entity;
                entity.id = m_binding.entity;
                auto* emitter = m_entities.particleEmitter(entity);
                if (!emitter && kind != EngineScript::NativeDependencyKind::OptionalComponent) {
                    error = "missing required ParticleEmitter2D component";
                    return false;
                }
                m_arguments.push_back(emitter);
            }
            else if (kind == EngineScript::NativeDependencyKind::RequiredComponent ||
                     kind == EngineScript::NativeDependencyKind::OptionalComponent) {
                EngineScript::ComponentReference reference = m_binding.runtime
                    ? m_binding.runtime->find_behaviour_component(m_binding.entity, type, 0)
                    : EngineScript::ComponentReference{};
                if (!reference.resolve_callback && kind == EngineScript::NativeDependencyKind::RequiredComponent) {
                    error = "missing required " + type + " behaviour";
                    return false;
                }
                m_component_references.push_back(std::move(reference));
                m_arguments.push_back(&m_component_references.back());
            }
            else { error = "unsupported EngineScript dependency: " + type; return false; }
        }
        return true;
    }

    void sync_before() {
        if (!m_binding.entity_manager) return;
        m_entities.begin_invocation();
        EntityManager& entities = *m_binding.entity_manager;
        if (entities.has_component<RigidBodyComponent>(m_binding.entity)) {
            m_rigid_body.velocity = entities.get_component<RigidBodyComponent>(m_binding.entity).velocity;
            m_rigid_body.isGrounded = PhysicsSystem::is_grounded(entities, m_binding.entity);
            bind_rigid_body(m_rigid_body, m_binding.entity);
        }
        if (entities.has_component<SpriteComponent>(m_binding.entity)) {
            m_sprite.flipX = entities.get_component<SpriteComponent>(m_binding.entity).flip_x;
        }
        if (entities.has_component<TransformComponent>(m_binding.entity)) {
            const TransformComponent& transform = entities.get_component<TransformComponent>(m_binding.entity);
            m_transform.position = glm::vec2(transform.position);
            m_transform.rotation = transform.rotation;
            m_transform.scale = transform.scale;
        }
        m_time.deltaTime = Time::get_delta_time();
        m_time.realDeltaTime = Time::get_real_delta_time();
        m_time.elapsed = Time::get_time();
        m_time.realElapsed = Time::get_real_time();
        m_time.frameCount = Time::get_frame_count();
        m_display.sync();
    }

    void sync_after() {
        if (!m_binding.entity_manager) return;
        EntityManager& entities = *m_binding.entity_manager;
        if (entities.has_component<RigidBodyComponent>(m_binding.entity)) {
            PhysicsSystem::set_velocity(entities, m_binding.entity, m_rigid_body.velocity);
        }
        if (entities.has_component<SpriteComponent>(m_binding.entity)) {
            entities.get_component<SpriteComponent>(m_binding.entity).flip_x = m_sprite.flipX;
        }
        if (entities.has_component<TransformComponent>(m_binding.entity)) {
            TransformComponent& transform = entities.get_component<TransformComponent>(m_binding.entity);
            const bool position_changed = glm::vec2(transform.position) != m_transform.position;
            transform.position = glm::vec3(m_transform.position, transform.position.z);
            transform.rotation = m_transform.rotation;
            transform.scale = m_transform.scale;
            if (position_changed && entities.has_component<RigidBodyComponent>(m_binding.entity)) {
                PhysicsSystem::set_position(entities, m_binding.entity, m_transform.position);
            }
        }
        m_entities.sync_after();
    }

    void** arguments() { return m_arguments.empty() ? nullptr : m_arguments.data(); }
    [[nodiscard]] EngineScript::Entities* entities() { return &m_entities; }
private:
    void bind_rigid_body(EngineScript::RigidBody2D& body, EntityID entity) {
        body.runtime_context = &m_binding;
        body.entity_id = entity;
        body.ignore_one_way_callback = [](void* context, uint32_t id, bool ignore) {
            auto* binding = static_cast<ScriptRuntimeBinding*>(context);
            if (!binding) return;
            require_main_thread(*binding);
            auto* entities = binding->entity_manager;
            if (!entities || !entities->is_alive(id) ||
                !entities->has_component<RigidBodyComponent>(id)) return;
            PhysicsSystem::set_ignore_one_way(id, ignore);
        };
    }

    std::vector<EngineScript::ComponentReference> m_component_references;

    class Input final : public EngineScript::Input {
    public:
        explicit Input(const ScriptRuntimeBinding&) {}
        float axis(const std::string& name) const override { return Engine::Input::get_axis(name); }
        bool down(const std::string& action) const override { return Engine::Input::is_action_pressed(action); }
        bool pressed(const std::string& action) const override { return Engine::Input::is_action_just_pressed(action); }
        bool released(const std::string& action) const override { return Engine::Input::is_action_just_released(action); }
    };
    class Log final : public EngineScript::Log {
    public:
        void write(const std::string& message) const override { ENGINE_LOG_INFO("[SCRIPT] {}", message); }
    };
    class Audio final : public EngineScript::Audio {
    public:
        explicit Audio(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        bool playSound(const std::string& path, float volume, float pitch) override { std::string error; return m_binding.services && m_binding.services->play_sound && m_binding.services->play_sound(path, volume, pitch, error); }
        bool playMusic(const std::string& path, float volume, bool loop) override { std::string error; return m_binding.services && m_binding.services->play_music && m_binding.services->play_music(path, volume, loop, error); }
        void stopMusic() override { if (m_binding.services && m_binding.services->stop_music) m_binding.services->stop_music(); }
        void pauseMusic() override { if (m_binding.services && m_binding.services->pause_music) m_binding.services->pause_music(); }
        void resumeMusic() override { if (m_binding.services && m_binding.services->resume_music) m_binding.services->resume_music(); }
        void setMasterVolume(float value) override { if (m_binding.services && m_binding.services->set_master_volume) m_binding.services->set_master_volume(value); }
        void setMusicVolume(float value) override { if (m_binding.services && m_binding.services->set_music_volume) m_binding.services->set_music_volume(value); }
        void setSfxVolume(float value) override { if (m_binding.services && m_binding.services->set_sfx_volume) m_binding.services->set_sfx_volume(value); }
        float masterVolume() const override { return m_binding.services && m_binding.services->get_master_volume ? m_binding.services->get_master_volume() : 1.0f; }
        float musicVolume() const override { return m_binding.services && m_binding.services->get_music_volume ? m_binding.services->get_music_volume() : 1.0f; }
        float sfxVolume() const override { return m_binding.services && m_binding.services->get_sfx_volume ? m_binding.services->get_sfx_volume() : 1.0f; }
    private: const ScriptRuntimeBinding& m_binding; };
    class Scene final : public EngineScript::Scene {
    public:
        explicit Scene(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        bool load(const std::string& path) override { std::string error; return m_binding.services && m_binding.services->request_scene_load && m_binding.services->request_scene_load(path, error); }
        std::string currentName() const override { return m_binding.services && m_binding.services->current_scene_name ? m_binding.services->current_scene_name() : std::string{}; }
        bool isLoading() const override { return m_binding.services && m_binding.services->is_scene_loading && m_binding.services->is_scene_loading(); }
        std::string lastError() const override { return m_binding.services && m_binding.services->scene_load_error ? m_binding.services->scene_load_error() : std::string{}; }
    private: const ScriptRuntimeBinding& m_binding; };
    class PlayerPrefs final : public EngineScript::PlayerPrefs {
    public:
        bool has(const std::string& key) const override { return Engine::PlayerPrefs::has_key(key); }
        int getInt(const std::string& key, int fallback) const override { return Engine::PlayerPrefs::get_int(key, fallback); }
        float getFloat(const std::string& key, float fallback) const override { return Engine::PlayerPrefs::get_float(key, fallback); }
        bool getBool(const std::string& key, bool fallback) const override { return Engine::PlayerPrefs::get_bool(key, fallback); }
        std::string getString(const std::string& key, const std::string& fallback) const override { return Engine::PlayerPrefs::get_string(key, fallback); }
        glm::vec2 getVec2(const std::string& key, const glm::vec2& fallback) const override { return Engine::PlayerPrefs::get_vec2(key, fallback); }
        glm::vec3 getVec3(const std::string& key, const glm::vec3& fallback) const override { return Engine::PlayerPrefs::get_vec3(key, fallback); }
        void setInt(const std::string& key, int value) override { Engine::PlayerPrefs::set_int(key, value); }
        void setFloat(const std::string& key, float value) override { Engine::PlayerPrefs::set_float(key, value); }
        void setBool(const std::string& key, bool value) override { Engine::PlayerPrefs::set_bool(key, value); }
        void setString(const std::string& key, const std::string& value) override { Engine::PlayerPrefs::set_string(key, value); }
        void setVec2(const std::string& key, const glm::vec2& value) override { Engine::PlayerPrefs::set_vec2(key, value); }
        void setVec3(const std::string& key, const glm::vec3& value) override { Engine::PlayerPrefs::set_vec3(key, value); }
        void remove(const std::string& key) override { Engine::PlayerPrefs::delete_key(key); }
        void clear() override { Engine::PlayerPrefs::delete_all(); }
        bool save() override { return Engine::PlayerPrefs::save(); }
    };
    class Prefabs final : public EngineScript::Prefabs {
    public:
        explicit Prefabs(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        EngineScript::Entity create(const EngineScript::Prefab& prefab) override {
            if (!m_binding.services || !m_binding.services->create_prefab || prefab.handle == 0) return {};
            EngineScript::Entity result; result.id = m_binding.services->create_prefab(AssetHandle{prefab.handle});
            if (result.id != 0) bind_dynamic_component_access(result, m_binding); return result;
        }
        bool spawn(const EngineScript::Entity& instance, const glm::vec2& position) override {
            return m_binding.services && m_binding.services->spawn_prefab && m_binding.services->spawn_prefab(instance.id, position);
        }
        EngineScript::Entity instantiate(const EngineScript::Prefab& prefab, const glm::vec2& position) override {
            if (!m_binding.services || !m_binding.services->instantiate_prefab || prefab.handle == 0) return {};
            EngineScript::Entity result;
            result.id = m_binding.services->instantiate_prefab(AssetHandle{prefab.handle}, position);
            if (result.id != 0) bind_dynamic_component_access(result, m_binding);
            return result;
        }
    private: const ScriptRuntimeBinding& m_binding;
    };
    class Assets final : public EngineScript::Assets {
    public:
        explicit Assets(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        EngineScript::Asset* find(const std::string& path) override {
            const AssetRecord* record = record_for(path);
            if (!record) return nullptr;
            m_asset.handle = record->handle.value;
            m_asset.path = record->source_path;
            m_asset.ready = record->status == AssetStatus::Ready;
            return &m_asset;
        }
        bool exists(const std::string& path) const override { return record_for(path) != nullptr; }
        EngineScript::Task<EngineScript::Result<EngineScript::Asset*, std::string>> load_resource(
            const std::string& path,
            EngineScript::ResourceType type
        ) override {
            const AssetRecord* record = record_for(path);
            if (!record) {
                co_return EngineScript::Result<EngineScript::Asset*, std::string>::failure(
                    "asset does not exist: " + path
                );
            }
            if (record->status != AssetStatus::Ready) {
                co_return EngineScript::Result<EngineScript::Asset*, std::string>::failure(
                    "asset is not ready: " + path
                );
            }
            if (!matches(type, record->type)) {
                co_return EngineScript::Result<EngineScript::Asset*, std::string>::failure(
                    "asset type does not match the requested resource type: " + path
                );
            }
            if (!m_binding.async_scheduler || !m_binding.services || !m_binding.services->preload_asset) {
                co_return EngineScript::Result<EngineScript::Asset*, std::string>::failure(
                    "asset loading service is unavailable"
                );
            }
            const AssetHandle handle = record->handle;
            const std::string source_path = record->source_path;
            co_await m_binding.async_scheduler->next_frame(m_binding.entity);
            std::string error;
            if (!m_binding.services->preload_asset(handle, error)) {
                co_return EngineScript::Result<EngineScript::Asset*, std::string>::failure(
                    error.empty() ? "failed to load asset: " + path : std::move(error)
                );
            }
            m_asset = EngineScript::Asset{handle.value, source_path, AssetDatabase::type_name(record->type), "ready", true};
            co_return EngineScript::Result<EngineScript::Asset*, std::string>::success(&m_asset);
        }
    private:
        static bool matches(EngineScript::ResourceType requested, AssetType actual) {
            switch (requested) {
                case EngineScript::ResourceType::Texture: return actual == AssetType::Texture;
                case EngineScript::ResourceType::AudioClip: return actual == AssetType::Audio;
                case EngineScript::ResourceType::Font: return actual == AssetType::Font;
                case EngineScript::ResourceType::DataAsset: return actual == AssetType::Data;
                case EngineScript::ResourceType::ParticleEffect: return actual == AssetType::ParticleEffect;
            }
            return false;
        }
        const AssetRecord* record_for(const std::string& path) const {
            if (!m_binding.asset_database) return nullptr;
            return m_binding.asset_database->find_by_path(path);
        }
        const ScriptRuntimeBinding& m_binding;
        EngineScript::Asset m_asset;
    };
    class Particles final : public EngineScript::Particles {
    public:
        explicit Particles(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        bool emit(const EngineScript::Asset* effect, const glm::vec2& position, int count) override {
            return effect && effect->ready && effect->handle != 0 && m_binding.services &&
                m_binding.services->emit_particles &&
                m_binding.services->emit_particles(AssetHandle{effect->handle}, position, count);
        }
        bool emitWithOptions(
            const EngineScript::Asset* effect,
            const glm::vec2& position,
            const EngineScript::ParticleEmitOptions& options
        ) override {
            return effect && effect->ready && effect->handle != 0 && m_binding.services &&
                m_binding.services->emit_particles_with_options &&
                m_binding.services->emit_particles_with_options(
                    AssetHandle{effect->handle}, position, options
                );
        }
    private:
        const ScriptRuntimeBinding& m_binding;
    };
    class WorldText final : public EngineScript::WorldText {
    public:
        explicit WorldText(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        bool show(
            const std::string& text,
            const glm::vec2& position,
            const EngineScript::FloatingTextOptions& options
        ) override {
            return m_binding.services && m_binding.services->show_world_text &&
                m_binding.services->show_world_text(text, position, options);
        }
    private:
        const ScriptRuntimeBinding& m_binding;
    };
    class UI final : public EngineScript::UI {
    public:
        explicit UI(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        bool setText(const EngineScript::Entity& entity, const std::string& text) override {
            if (!alive(entity.id) || !m_binding.entity_manager->has_component<UITextComponent>(entity.id)) return false;
            m_binding.entity_manager->get_component<UITextComponent>(entity.id).text = text;
            return true;
        }
        bool setVisible(const EngineScript::Entity& entity, bool visible) override {
            if (!alive(entity.id)) return false;
            bool changed = false;
            if (m_binding.entity_manager->has_component<UITextComponent>(entity.id)) {
                m_binding.entity_manager->get_component<UITextComponent>(entity.id).enabled = visible;
                changed = true;
            }
            if (m_binding.entity_manager->has_component<UIImageComponent>(entity.id)) {
                m_binding.entity_manager->get_component<UIImageComponent>(entity.id).enabled = visible;
                changed = true;
            }
            if (m_binding.entity_manager->has_component<UIButtonComponent>(entity.id)) {
                m_binding.entity_manager->get_component<UIButtonComponent>(entity.id).enabled = visible;
                changed = true;
            }
            return changed;
        }
    private:
        bool alive(EntityID id) const {
            require_main_thread(m_binding);
            return m_binding.entity_manager && m_binding.entity_manager->is_alive(id);
        }
        const ScriptRuntimeBinding& m_binding;
    };
    class Tilemap final : public EngineScript::Tilemap {
    public:
        explicit Tilemap(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        int getTile(const EngineScript::Entity& entity, int x, int y) const override {
            const TilemapComponent* tilemap = component(entity.id);
            return tilemap ? static_cast<int>(tilemap->get_tile(x, y)) : 0;
        }
        bool setTile(const EngineScript::Entity& entity, int x, int y, int tile_id) override {
            if (tile_id < 0 || tile_id > std::numeric_limits<uint16_t>::max()) return false;
            TilemapComponent* tilemap = component(entity.id);
            if (!tilemap) return false;
            tilemap->set_tile(x, y, static_cast<uint16_t>(tile_id));
            return true;
        }
    private:
        TilemapComponent* component(EntityID id) const {
            require_main_thread(m_binding);
            if (!m_binding.entity_manager || !m_binding.entity_manager->is_alive(id) ||
                !m_binding.entity_manager->has_component<TilemapComponent>(id)) return nullptr;
            return &m_binding.entity_manager->get_component<TilemapComponent>(id);
        }
        const ScriptRuntimeBinding& m_binding;
    };
    class Display final : public EngineScript::Display {
    public:
        explicit Display(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        void sync() {
            if (!m_binding.services || !m_binding.services->query_display) return;
            const ScriptDisplayInfo info = m_binding.services->query_display();
            width = info.reference_width;
            height = info.reference_height;
            pixelWidth = info.pixel_width;
            pixelHeight = info.pixel_height;
            dpiScale = info.dpi_scale;
            isFullscreen = info.window_mode != WindowMode::Windowed;
        }
        bool setVsync(bool enabled) override {
            ScriptDisplayRequest request;
            request.vsync = enabled;
            return apply(request);
        }
        bool setFullscreen(bool enabled) override {
            ScriptDisplayRequest request;
            request.window_mode = enabled ? WindowMode::BorderlessFullscreen : WindowMode::Windowed;
            return apply(request);
        }
    private:
        bool apply(const ScriptDisplayRequest& request) {
            std::string error;
            const bool applied = m_binding.services && m_binding.services->apply_display &&
                m_binding.services->apply_display(request, error);
            if (applied) sync();
            return applied;
        }
        const ScriptRuntimeBinding& m_binding;
    };
    class Physics final : public EngineScript::Physics {
    public:
        explicit Physics(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        EngineScript::RaycastHit* raycast(const glm::vec2& origin, const glm::vec2& destination, uint32_t mask) override {
            require_main_thread(m_binding);
            const RaycastHit2D hit = PhysicsSystem::raycast(origin, destination, static_cast<uint16_t>(mask));
            if (!hit.hit) return nullptr;
            m_hit.hit = true;
            m_hit.entity = make_entity(hit.entity_id);
            m_hit.point = hit.point;
            m_hit.normal = hit.normal;
            m_hit.distance = hit.distance;
            return &m_hit;
        }
    private:
        EngineScript::Entity make_entity(EntityID id) const {
            EngineScript::Entity entity;
            entity.id = id;
            if (!m_binding.entity_manager || !m_binding.entity_manager->is_alive(id)) return entity;
            if (m_binding.entity_manager->has_component<TagComponent>(id)) {
                entity.name = m_binding.entity_manager->get_component<TagComponent>(id).tag;
                entity.tag = entity.name;
            }
            entity.runtime_context = const_cast<ScriptRuntimeBinding*>(&m_binding);
            entity.destroy_callback = [](void* context, uint32_t entity_id) {
                auto* binding = static_cast<ScriptRuntimeBinding*>(context);
                if (!binding) return;
                require_main_thread(*binding);
                auto* entities = binding->entity_manager;
                if (entities && entities->is_alive(entity_id)) entities->destroy_entity_subtree(entity_id);
            };
            bind_dynamic_component_access(entity, m_binding);
            return entity;
        }
        const ScriptRuntimeBinding& m_binding;
        EngineScript::RaycastHit m_hit;
    };
    class Animator final : public EngineScript::Animator2D {
    public:
        explicit Animator(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        void play(const std::string& clip) override {
            require_main_thread(m_binding);
            if (m_binding.entity_manager) SpriteAnimationSystem::play_clip(*m_binding.entity_manager, m_binding.entity, clip);
        }
    private: const ScriptRuntimeBinding& m_binding; };
    class EntityService final : public EngineScript::Entities {
    public:
        explicit EntityService(const ScriptRuntimeBinding& binding) : m_binding(binding) {}

        EngineScript::ComponentReference component_reference(
            EntityID entity,
            const char* type,
            std::size_t index
        ) {
            if (!type || index != 0 || component_count(entity, type) == 0) return {};
            EngineScript::ComponentReference reference;
            reference.runtime_context = this;
            reference.entity_id = entity;
            reference.type_name = type;
            reference.resolve_callback = [](void* context, uint32_t id, uint64_t, uint32_t, const char* requested) -> void* {
                auto* service = static_cast<EntityService*>(context);
                if (!service || !requested) return nullptr;
                EngineScript::Entity target;
                target.id = id;
                const std::string_view name{requested};
                if (name == "Transform2D") return service->transform(target);
                if (name == "RigidBody2D") return service->rigidBody(target);
                if (name == "SpriteRenderer2D") return service->spriteRenderer(target);
                if (name == "ParticleEmitter2D") return service->particleEmitter(target);
                if (name == "Joint2D") return service->joint(target);
                if (name == "Animator2D") {
                    service->m_entity_animator.bind(id);
                    return &service->m_entity_animator;
                }
                return nullptr;
            };
            return reference;
        }

        std::size_t component_count(EntityID entity, const char* type) const {
            if (!type) return 0;
            const std::string_view name{type};
            if (name == "Transform2D") return alive_with<TransformComponent>(entity) ? 1 : 0;
            if (name == "RigidBody2D") return alive_with<RigidBodyComponent>(entity) ? 1 : 0;
            if (name == "SpriteRenderer2D") return alive_with<SpriteComponent>(entity) ? 1 : 0;
            if (name == "ParticleEmitter2D") return alive_with<ParticleEmitter2DComponent>(entity) ? 1 : 0;
            if (name == "Joint2D") return alive_with<JointComponent>(entity) ? 1 : 0;
            if (name == "Animator2D") return alive_with<AnimationComponent>(entity) ? 1 : 0;
            return 0;
        }
        EngineScript::Entity create(const std::string& name) override {
            require_main_thread(m_binding);
            if (!m_binding.entity_manager) return {};
            EntityManager& entities = *m_binding.entity_manager;
            const EntityID id = entities.create_entity();
            entities.add_component<TagComponent>(id, TagComponent{name});
            entities.add_component<TransformComponent>(id, TransformComponent{});
            return make_entity(id);
        }
        bool destroy(const EngineScript::Entity& entity) override {
            require_main_thread(m_binding);
            if (!m_binding.entity_manager || !m_binding.entity_manager->is_alive(entity.id)) return false;
            m_binding.entity_manager->destroy_entity_subtree(entity.id);
            return true;
        }
        bool setTag(const EngineScript::Entity& entity, const std::string& tag) override {
            require_main_thread(m_binding);
            if (!alive_with<TagComponent>(entity.id)) return false;
            m_binding.entity_manager->get_component<TagComponent>(entity.id).tag = tag;
            return true;
        }
        EngineScript::Entity parent(const EngineScript::Entity& entity) const override {
            require_main_thread(m_binding);
            if (!alive_with<HierarchyComponent>(entity.id)) return {};
            return make_entity(m_binding.entity_manager->get_component<HierarchyComponent>(entity.id).parent);
        }
        bool setParent(const EngineScript::Entity& child, const EngineScript::Entity& parent) override {
            require_main_thread(m_binding);
            if (!m_binding.entity_manager) return false;
            return m_binding.entity_manager->set_parent(child.id, parent.id).changed();
        }
        bool clearParent(const EngineScript::Entity& child) override {
            require_main_thread(m_binding);
            if (!m_binding.entity_manager) return false;
            return m_binding.entity_manager->remove_parent(child.id).changed();
        }
        EngineScript::Entity findByName(const std::string& name) const override { return find(name); }
        EngineScript::Entity findByTag(const std::string& tag) const override { return find(tag); }
        bool exists(const EngineScript::Entity& entity) const override {
            require_main_thread(m_binding);
            return m_binding.entity_manager && m_binding.entity_manager->is_alive(entity.id);
        }
        std::vector<EngineScript::Entity> findAllByTag(const std::string& tag) const override {
            return find_all([&tag](const TagComponent& value) { return value.tag == tag; });
    }
    EngineScript::Entity
    findByGameplayTag(const std::string &tag) const override {
      if (!m_binding.entity_manager || !is_valid_gameplay_tag(tag))
        return {};
      EntityID result = 0;
      m_binding.entity_manager->for_each<GameplayTagsComponent>(
          [&](EntityID id, const GameplayTagsComponent &tags) {
            if (result == 0 && tags.contains(tag))
              result = id;
          });
      return make_entity(result);
    }
    std::vector<EngineScript::Entity>
    findAllByGameplayTag(const std::string &tag) const override {
      std::vector<EngineScript::Entity> results;
      if (!m_binding.entity_manager || !is_valid_gameplay_tag(tag))
        return results;
      m_binding.entity_manager->for_each<GameplayTagsComponent>(
          [&](EntityID id, const GameplayTagsComponent &tags) {
            if (tags.contains(tag))
              results.push_back(make_entity(id));
          });
      return results;
    }
    bool hasGameplayTag(const EngineScript::Entity &entity,
                        const std::string &tag) const override {
      return alive_with<GameplayTagsComponent>(entity.id) &&
             is_valid_gameplay_tag(tag) &&
             m_binding.entity_manager
                 ->get_component<GameplayTagsComponent>(entity.id)
                 .contains(tag);
    }
    bool addGameplayTag(const EngineScript::Entity &entity,
                        const std::string &tag) override {
      require_main_thread(m_binding);
      if (!m_binding.entity_manager ||
          !m_binding.entity_manager->is_alive(entity.id) ||
          !is_valid_gameplay_tag(tag))
        return false;
      if (!m_binding.entity_manager->has_component<GameplayTagsComponent>(
              entity.id)) {
        m_binding.entity_manager->add_component<GameplayTagsComponent>(
            entity.id, GameplayTagsComponent{});
      }
      auto &tags = m_binding.entity_manager
                       ->get_component<GameplayTagsComponent>(entity.id)
                       .tags;
      if (tags.size() >= 32 ||
          std::find(tags.begin(), tags.end(), tag) != tags.end())
        return false;
      tags.push_back(tag);
      std::sort(tags.begin(), tags.end());
      return true;
    }
    bool removeGameplayTag(const EngineScript::Entity &entity,
                           const std::string &tag) override {
      if (!alive_with<GameplayTagsComponent>(entity.id))
        return false;
      auto &tags = m_binding.entity_manager
                       ->get_component<GameplayTagsComponent>(entity.id)
                       .tags;
      const auto found = std::find(tags.begin(), tags.end(), tag);
      if (found == tags.end())
        return false;
      tags.erase(found);
      return true;
    }
    std::vector<std::string>
    gameplayTags(const EngineScript::Entity &entity) const override {
      if (!alive_with<GameplayTagsComponent>(entity.id))
        return {};
      return m_binding.entity_manager
          ->get_component<GameplayTagsComponent>(entity.id)
          .tags;
        }
        std::vector<EngineScript::Entity> findAllWithTransform() const override {
            require_main_thread(m_binding);
            std::vector<EngineScript::Entity> results;
            if (!m_binding.entity_manager) return results;
            m_binding.entity_manager->for_each<TransformComponent>([&](EntityID id, const TransformComponent&) {
                results.push_back(make_entity(id));
            });
            return results;
        }
        bool hasTransform(const EngineScript::Entity& entity) const override {
            require_main_thread(m_binding);
            return m_binding.entity_manager && m_binding.entity_manager->is_alive(entity.id) &&
                m_binding.entity_manager->has_component<TransformComponent>(entity.id);
        }
        EngineScript::Transform2D* transform(const EngineScript::Entity& entity) override {
            if (!hasTransform(entity)) return nullptr;
            const TransformComponent& source = m_binding.entity_manager->get_component<TransformComponent>(entity.id);
            m_transform.id = entity.id;
            m_transform.value.position = glm::vec2(source.position);
            m_transform.value.rotation = source.rotation;
            m_transform.value.scale = source.scale;
            return &m_transform.value;
        }
        bool hasRigidBody(const EngineScript::Entity& entity) const override {
            return alive_with<RigidBodyComponent>(entity.id);
        }
        EngineScript::RigidBody2D* rigidBody(const EngineScript::Entity& entity) override {
            if (!hasRigidBody(entity)) return nullptr;
            const RigidBodyComponent& source = m_binding.entity_manager->get_component<RigidBodyComponent>(entity.id);
            m_rigid_body.id = entity.id;
            m_rigid_body.value.velocity = source.velocity;
            m_rigid_body.value.isGrounded = PhysicsSystem::is_grounded(*m_binding.entity_manager, entity.id);
            m_rigid_body.value.runtime_context = const_cast<ScriptRuntimeBinding*>(&m_binding);
            m_rigid_body.value.entity_id = entity.id;
            m_rigid_body.value.ignore_one_way_callback = [](void* context, uint32_t id, bool ignore) {
                auto* binding = static_cast<ScriptRuntimeBinding*>(context);
                if (!binding) return;
                require_main_thread(*binding);
                auto* entities = binding->entity_manager;
                if (!entities || !entities->is_alive(id) ||
                    !entities->has_component<RigidBodyComponent>(id)) return;
                PhysicsSystem::set_ignore_one_way(id, ignore);
            };
            return &m_rigid_body.value;
        }
        bool hasSpriteRenderer(const EngineScript::Entity& entity) const override {
            return alive_with<SpriteComponent>(entity.id);
        }
        EngineScript::SpriteRenderer2D* spriteRenderer(const EngineScript::Entity& entity) override {
            if (!hasSpriteRenderer(entity)) return nullptr;
            m_sprite_renderer.id = entity.id;
            m_sprite_renderer.value.flipX = m_binding.entity_manager->get_component<SpriteComponent>(entity.id).flip_x;
            return &m_sprite_renderer.value;
        }
        bool hasParticleEmitter(const EngineScript::Entity& entity) const override { return alive_with<ParticleEmitter2DComponent>(entity.id); }
        EngineScript::ParticleEmitter2D* particleEmitter(const EngineScript::Entity& entity) override {
            if (!hasParticleEmitter(entity)) return nullptr;
            const ParticleEmitter2DComponent& source = m_binding.entity_manager->get_component<ParticleEmitter2DComponent>(entity.id);
            m_particle.id = entity.id;
            float asset_rate = 0.0f;
            int asset_burst = 0;
            if (m_binding.asset_database && source.effect_handle.is_valid()) {
                if (const auto asset = ParticleEffectAsset::load_asset(source.effect_handle, *m_binding.asset_database)) {
                    asset_rate = asset->emission.continuous_rate;
                    asset_burst = asset->emission.burst_count;
                }
            }
            m_particle.value.emitRate = source.emit_rate_override.value_or(asset_rate);
            m_particle.value.burstCount = source.burst_count_override.value_or(asset_burst);
            m_particle.value.active = source.active;
            m_particle.initial_emit_rate = m_particle.value.emitRate;
            m_particle.initial_burst_count = m_particle.value.burstCount;
            m_particle.initial_active = m_particle.value.active;
            m_particle.value.isPlaying = source.is_playing;
            m_particle.value.effectHandle = source.effect_handle.value;
            m_particle.value.runtime_context = const_cast<ScriptRuntimeBinding*>(&m_binding);
            m_particle.value.entity_id = entity.id;
            m_particle.value.play_callback = [](void* context, uint32_t id) {
                const auto* binding = static_cast<ScriptRuntimeBinding*>(context);
                const auto* services = binding ? binding->services : nullptr;
                return services && services->play_particle_emitter && services->play_particle_emitter(id);
            };
            m_particle.value.stop_callback = [](void* context, uint32_t id, bool clear_particles) {
                const auto* binding = static_cast<ScriptRuntimeBinding*>(context);
                const auto* services = binding ? binding->services : nullptr;
                return services && services->stop_particle_emitter &&
                    services->stop_particle_emitter(id, clear_particles);
            };
            m_particle.value.burst_callback = [](void* context, uint32_t id, int count) {
                const auto* binding = static_cast<ScriptRuntimeBinding*>(context);
                const auto* services = binding ? binding->services : nullptr;
                return services && services->burst_particle_emitter &&
                    services->burst_particle_emitter(id, count);
            };
            m_particle.value.set_effect_callback = [](void* context, uint32_t id, const EngineScript::Asset* effect) {
                auto* binding = static_cast<ScriptRuntimeBinding*>(context);
                if (!binding || !binding->entity_manager || !binding->asset_database ||
                    !effect || !effect->ready || effect->handle == 0 ||
                    !binding->entity_manager->is_alive(id) ||
                    !binding->entity_manager->has_component<ParticleEmitter2DComponent>(id)) return false;
                const AssetRecord* record = binding->asset_database->find(AssetHandle{effect->handle});
                if (!record || record->status != AssetStatus::Ready || record->type != AssetType::ParticleEffect) return false;
                auto& target = binding->entity_manager->get_component<ParticleEmitter2DComponent>(id);
                target.effect_handle = record->handle;
                target.is_playing = false;
                target.emission_accumulator = 0.0f;
                target.lifetime_timer = 0.0f;
                target.delay_remaining = 0.0f;
                return true;
            };
            return &m_particle.value;
        }
        bool hasJoint(const EngineScript::Entity& entity) const override { return alive_with<JointComponent>(entity.id); }
        EngineScript::Joint2D* joint(const EngineScript::Entity& entity) override {
            if (!hasJoint(entity)) return nullptr;
            const JointComponent& source = m_binding.entity_manager->get_component<JointComponent>(entity.id);
            m_joint.id = entity.id;
            m_joint.value.target = make_entity(source.target_entity);
            m_joint.value.collideConnected = source.collide_connected;
            return &m_joint.value;
        }
        void sync_after() {
            require_main_thread(m_binding);
            if (!m_binding.entity_manager) return;
            if (m_transform.id != 0 && alive_with<TransformComponent>(m_transform.id)) {
                TransformComponent& target = m_binding.entity_manager->get_component<TransformComponent>(m_transform.id);
                const bool position_changed = glm::vec2(target.position) != m_transform.value.position;
                target.position = glm::vec3(m_transform.value.position, target.position.z);
                target.rotation = m_transform.value.rotation;
                target.scale = m_transform.value.scale;
                if (position_changed && alive_with<RigidBodyComponent>(m_transform.id)) {
                    PhysicsSystem::set_position(
                        *m_binding.entity_manager, m_transform.id, m_transform.value.position
                    );
                }
            }
            if (m_rigid_body.id != 0 && alive_with<RigidBodyComponent>(m_rigid_body.id)) {
                PhysicsSystem::set_velocity(*m_binding.entity_manager, m_rigid_body.id, m_rigid_body.value.velocity);
            }
            if (m_sprite_renderer.id != 0 && alive_with<SpriteComponent>(m_sprite_renderer.id)) {
                m_binding.entity_manager->get_component<SpriteComponent>(m_sprite_renderer.id).flip_x = m_sprite_renderer.value.flipX;
            }
            if (m_particle.id != 0 && alive_with<ParticleEmitter2DComponent>(m_particle.id)) {
                ParticleEmitter2DComponent& target = m_binding.entity_manager->get_component<ParticleEmitter2DComponent>(m_particle.id);
                if (m_particle.value.emitRate != m_particle.initial_emit_rate) {
                    target.emit_rate_override = m_particle.value.emitRate;
                }
                if (m_particle.value.burstCount != m_particle.initial_burst_count) {
                    target.burst_count_override = m_particle.value.burstCount;
                }
                if (m_particle.value.active != m_particle.initial_active) {
                    target.active = m_particle.value.active;
                }
            }
            if (m_joint.id != 0 && alive_with<JointComponent>(m_joint.id)) {
                JointComponent& target = m_binding.entity_manager->get_component<JointComponent>(m_joint.id);
                target.target_entity = m_joint.value.target.id;
                target.collide_connected = m_joint.value.collideConnected;
            }
        }

        void begin_invocation() {
            // Dynamic component views are valid only for the callback that queried them.
            // Keeping their entity ids across callbacks would write stale snapshots back
            // during the async scheduler's otherwise empty synchronization pass.
            m_transform.id = 0;
            m_rigid_body.id = 0;
            m_sprite_renderer.id = 0;
            m_joint.id = 0;
        }
    private:
        class EntityAnimator final : public EngineScript::Animator2D {
        public:
            explicit EntityAnimator(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
            void bind(EntityID entity) { m_entity = entity; }
            void play(const std::string& clip) override {
                require_main_thread(m_binding);
                if (m_binding.entity_manager && m_binding.entity_manager->is_alive(m_entity) &&
                    m_binding.entity_manager->has_component<AnimationComponent>(m_entity)) {
                    SpriteAnimationSystem::play_clip(*m_binding.entity_manager, m_entity, clip);
                }
            }
        private:
            const ScriptRuntimeBinding& m_binding;
            EntityID m_entity{0};
        };

        template <typename Predicate>
        std::vector<EngineScript::Entity> find_all(Predicate predicate) const {
            require_main_thread(m_binding);
            std::vector<EngineScript::Entity> results;
            if (!m_binding.entity_manager) return results;
            m_binding.entity_manager->for_each<TagComponent>([&](EntityID id, const TagComponent& value) {
                if (predicate(value)) results.push_back(make_entity(id));
            });
            return results;
        }
        template <typename Component>
        bool alive_with(EntityID id) const {
            require_main_thread(m_binding);
            return m_binding.entity_manager && m_binding.entity_manager->is_alive(id) &&
                m_binding.entity_manager->has_component<Component>(id);
        }
        EngineScript::Entity find(const std::string& value) const {
            require_main_thread(m_binding);
            if (!m_binding.entity_manager) return {};
            EntityID result = 0;
            m_binding.entity_manager->for_each<TagComponent>([&](EntityID id, const TagComponent& tag) {
                if (result == 0 && tag.tag == value) result = id;
            });
            return make_entity(result);
        }
        EngineScript::Entity make_entity(EntityID id) const {
            require_main_thread(m_binding);
            EngineScript::Entity entity;
            entity.id = id;
            if (!m_binding.entity_manager || !m_binding.entity_manager->is_alive(id)) return entity;
            if (m_binding.entity_manager->has_component<TagComponent>(id)) {
                entity.name = m_binding.entity_manager->get_component<TagComponent>(id).tag;
                entity.tag = entity.name;
            }
            entity.runtime_context = const_cast<ScriptRuntimeBinding*>(&m_binding);
            entity.destroy_callback = [](void* context, uint32_t entity_id) {
                auto* binding = static_cast<ScriptRuntimeBinding*>(context);
                if (!binding) return;
                require_main_thread(*binding);
                auto* entities = binding->entity_manager;
                if (entities && entities->is_alive(entity_id)) entities->destroy_entity_subtree(entity_id);
            };
            bind_dynamic_component_access(entity, m_binding);
            return entity;
        }
        const ScriptRuntimeBinding& m_binding;
        EntityAnimator m_entity_animator{m_binding};
        struct { EntityID id{0}; EngineScript::Transform2D value; } m_transform;
        struct { EntityID id{0}; EngineScript::RigidBody2D value; } m_rigid_body;
        struct { EntityID id{0}; EngineScript::SpriteRenderer2D value; } m_sprite_renderer;
        struct {
            EntityID id{0};
            EngineScript::ParticleEmitter2D value;
            float initial_emit_rate{0.0f};
            int initial_burst_count{0};
            bool initial_active{false};
        } m_particle;
        struct { EntityID id{0}; EngineScript::Joint2D value; } m_joint;
    };

    ScriptRuntimeBinding& m_binding;
    Input m_input;
    Log m_log;
    Audio m_audio;
    Scene m_scene;
    Prefabs m_prefabs;
    EngineScript::Events m_events;
    PlayerPrefs m_prefs;
    Assets m_assets;
    Particles m_particles;
    WorldText m_world_text;
    UI m_ui;
    Tilemap m_tilemap;
    Display m_display;
    Physics m_physics;
    Animator m_animator;
    EntityService m_entities{m_binding};
    EngineScript::RigidBody2D m_rigid_body;
    EngineScript::SpriteRenderer2D m_sprite;
    EngineScript::Transform2D m_transform;
    EngineScript::Time m_time;
    EngineScript::Jobs m_jobs;
    EngineScript::TaskScope m_task_scope;
    std::vector<void*> m_arguments;
};

namespace {

struct RuntimeObjectRef {
    enum class Kind {
        Input,
        Log,
        RigidBody,
        BodyVelocity,
        Animator,
        SpriteRenderer,
        Audio,
        Scene
    };

    Kind kind{Kind::Input};
    bool available{true};
};

struct RuntimeCollection;
using RuntimeCollectionRef = std::shared_ptr<RuntimeCollection>;

using RuntimeValue = std::variant<
    std::monostate,
    double,
    bool,
    std::string,
    EntityID,
    EngineScript::Entity,
    RuntimeObjectRef,
    RuntimeCollectionRef
>;
struct RuntimeCollection {
    enum class Kind { Array, Map, Set };
    Kind kind{Kind::Array};
    std::vector<RuntimeValue> values;
    std::map<std::string, RuntimeValue> mapped_values;
};
using RuntimeEnvironment = std::unordered_map<std::string, RuntimeValue>;

double runtime_number(const RuntimeValue& value) {
    if (const auto* number = std::get_if<double>(&value)) return *number;
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? 1.0 : 0.0;
    return 0.0;
}

bool runtime_boolean(const RuntimeValue& value) {
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean;
    if (const auto* number = std::get_if<double>(&value)) return *number != 0.0;
    if (const auto* object = std::get_if<RuntimeObjectRef>(&value)) return object->available;
    if (const auto* collection = std::get_if<RuntimeCollectionRef>(&value)) return *collection != nullptr;
    return !std::holds_alternative<std::monostate>(value);
}

std::string runtime_string(const RuntimeValue& value) {
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    return {};
}

class InterpretedBehaviour final : public EngineScript::NativeBehaviour {
public:
    InterpretedBehaviour(std::shared_ptr<const EngineScript::IRClass> definition, ScriptRuntimeBinding* binding)
        : m_definition(std::move(definition)), m_binding(binding) {}

    void update(float delta_time) override {
        execute_method("update", {static_cast<double>(delta_time)});
    }

    void on_start() override {
        execute_method("on_start", {});
    }

    void on_destroy() override {
        execute_method("on_destroy", {});
    }

    void on_collision_enter(const EngineScript::Entity& other) override {
        execute_method("on_collision_enter", {other});
    }

    void on_button_clicked() override {
        execute_method("on_button_clicked", {});
    }

    bool set_exposed_property(
        const std::string& name,
        const ExposedPropertyValue& value
    ) override {
        ScriptBehaviourSlot* component = script_component();
        if (!component) return false;
        auto field = std::find_if(component->public_fields.begin(), component->public_fields.end(),
            [&name](const ScriptPublicField& candidate) { return candidate.name == name; });
        if (field == component->public_fields.end()) return false;

        switch (field->type) {
            case ScriptFieldType::Boolean:
                if (const auto* typed = std::get_if<bool>(&value)) field->value = *typed;
                else return false;
                return true;
            case ScriptFieldType::Number:
                if (const auto* typed = std::get_if<double>(&value)) field->value = *typed;
                else return false;
                return true;
            case ScriptFieldType::String:
                if (const auto* typed = std::get_if<std::string>(&value)) field->value = *typed;
                else return false;
                return true;
            case ScriptFieldType::Vector2:
                if (const auto* typed = std::get_if<glm::vec2>(&value)) field->value = ScriptVector2{typed->x, typed->y};
                else return false;
                return true;
            case ScriptFieldType::Entity:
                if (const auto* typed = std::get_if<EngineScript::Entity>(&value)) field->value = typed->id;
                else return false;
                return true;
            case ScriptFieldType::Asset:
                return false;
        }
        return false;
    }

    bool get_exposed_property(
        const std::string& name,
        ExposedPropertyValue& value
    ) const override {
        const ScriptBehaviourSlot* component = script_component();
        if (!component) return false;
        const auto field = std::find_if(component->public_fields.begin(), component->public_fields.end(),
            [&name](const ScriptPublicField& candidate) { return candidate.name == name; });
        if (field == component->public_fields.end()) return false;

        if (const auto* typed = std::get_if<bool>(&field->value)) value = *typed;
        else if (const auto* typed = std::get_if<double>(&field->value)) value = *typed;
        else if (const auto* typed = std::get_if<std::string>(&field->value)) value = *typed;
        else if (const auto* typed = std::get_if<ScriptVector2>(&field->value)) value = glm::vec2{typed->x, typed->y};
        else if (const auto* typed = std::get_if<EntityID>(&field->value)) value = make_entity(*typed);
        else return false;
        return true;
    }

private:
    EngineScript::Entity make_entity(EntityID id) const {
        EngineScript::Entity entity;
        entity.id = id;
        if (m_binding && m_binding->entity_manager && m_binding->entity_manager->is_alive(id) &&
            m_binding->entity_manager->has_component<TagComponent>(id)) {
            entity.name = m_binding->entity_manager->get_component<TagComponent>(id).tag;
            entity.tag = entity.name;
        }
        entity.runtime_context = m_binding ? m_binding->entity_manager : nullptr;
        entity.destroy_callback = [](void* context, uint32_t entity_id) {
            auto* entities = static_cast<EntityManager*>(context);
            if (entities && entities->is_alive(entity_id)) entities->destroy_entity_subtree(entity_id);
        };
        if (m_binding) bind_dynamic_component_access(entity, *m_binding);
        return entity;
    }

    ScriptBehaviourSlot* script_component() const {
        if (!m_binding || !m_binding->entity_manager ||
            !m_binding->entity_manager->is_alive(m_binding->entity) ||
            !m_binding->entity_manager->has_component<ScriptComponent>(m_binding->entity)) {
            return nullptr;
        }
        auto& scripts = m_binding->entity_manager->get_component<ScriptComponent>(m_binding->entity);
        const auto found = std::find_if(
            scripts.instances.begin(), scripts.instances.end(),
            [this](const ScriptBehaviourSlot& candidate) { return candidate.id == m_binding->slot_id; }
        );
        return found == scripts.instances.end() ? nullptr : &*found;
    }

    RuntimeValue read_property(const std::string& name) const {
        const ScriptBehaviourSlot* component = script_component();
        if (!component) return {};
        for (const auto& field : component->public_fields) {
            if (field.name != name) continue;
            if (const auto* value = std::get_if<double>(&field.value)) return *value;
            if (const auto* value = std::get_if<bool>(&field.value)) return *value;
            if (const auto* value = std::get_if<std::string>(&field.value)) return *value;
            if (const auto* value = std::get_if<EntityID>(&field.value)) return *value;
        }
        return {};
    }

    void write_property(const std::string& name, const RuntimeValue& value) const {
        ScriptBehaviourSlot* component = script_component();
        if (!component) return;
        for (auto& field : component->public_fields) {
            if (field.name != name) continue;
            if (field.type == ScriptFieldType::Number) field.value = runtime_number(value);
            else if (field.type == ScriptFieldType::Boolean) field.value = runtime_boolean(value);
            else if (field.type == ScriptFieldType::String) field.value = runtime_string(value);
            else if (field.type == ScriptFieldType::Entity) {
                if (const auto* entity = std::get_if<EntityID>(&value)) field.value = *entity;
            }
            return;
        }
    }

    RuntimeValue read_identifier(const std::string& name, const RuntimeEnvironment& environment) const {
        const auto local = environment.find(name);
        if (local != environment.end()) return local->second;

        for (const auto& parameter : m_definition->constructor_params) {
            if (parameter.name != name || !parameter.type) continue;
            const std::string& type = parameter.type->name;
            if (type == "Input") return RuntimeObjectRef{RuntimeObjectRef::Kind::Input, true};
            if (type == "Log") return RuntimeObjectRef{RuntimeObjectRef::Kind::Log, true};
            if (type == "RigidBody2D") {
                const bool available = m_binding && m_binding->entity_manager &&
                    m_binding->entity_manager->has_component<RigidBodyComponent>(m_binding->entity);
                return RuntimeObjectRef{RuntimeObjectRef::Kind::RigidBody, available};
            }
            if (type == "Animator2D") {
                const bool available = m_binding && m_binding->entity_manager &&
                    m_binding->entity_manager->has_component<AnimationComponent>(m_binding->entity);
                return RuntimeObjectRef{RuntimeObjectRef::Kind::Animator, available};
            }
            if (type == "SpriteRenderer2D") {
                const bool available = m_binding && m_binding->entity_manager &&
                    m_binding->entity_manager->has_component<SpriteComponent>(m_binding->entity);
                return RuntimeObjectRef{RuntimeObjectRef::Kind::SpriteRenderer, available};
            }
            if (type == "Audio") {
                const bool available = m_binding && m_binding->services &&
                    (m_binding->services->play_sound || m_binding->services->play_music);
                return RuntimeObjectRef{RuntimeObjectRef::Kind::Audio, available};
            }
            if (type == "Scene") {
                const bool available = m_binding && m_binding->services &&
                    (m_binding->services->request_scene_load || m_binding->services->current_scene_name);
                return RuntimeObjectRef{RuntimeObjectRef::Kind::Scene, available};
            }
        }

        return read_property(name);
    }

    RuntimeValue evaluate(const EngineScript::IROperation* operation, RuntimeEnvironment& environment) const {
        using namespace EngineScript;
        if (!operation) return {};

        if (const auto* constant = dynamic_cast<const IRConstant*>(operation)) {
            if (constant->is_null) return {};
            if (constant->result_type && constant->result_type->is_bool()) return constant->bool_val;
            if (constant->result_type && constant->result_type->is_string()) return constant->string_val;
            if (constant->result_type && constant->result_type->is_integer()) return static_cast<double>(constant->int_val);
            return constant->float_val;
        }
        if (const auto* variable = dynamic_cast<const IRVarRef*>(operation)) {
            return read_identifier(variable->name, environment);
        }
        if (const auto* literal = dynamic_cast<const IRArrayLiteral*>(operation)) {
            auto collection = std::make_shared<RuntimeCollection>();
            for (const auto& element : literal->elements) collection->values.push_back(evaluate(element.get(), environment));
            return collection;
        }
        if (const auto* literal = dynamic_cast<const IRCollectionLiteral*>(operation)) {
            auto collection = std::make_shared<RuntimeCollection>();
            if (literal->collection_kind == TypeKind::Map) {
                collection->kind = RuntimeCollection::Kind::Map;
                for (const auto& [key, value] : literal->map_entries) {
                    collection->mapped_values[runtime_string(evaluate(key.get(), environment))] = evaluate(value.get(), environment);
                }
            } else {
                collection->kind = literal->collection_kind == TypeKind::Set
                    ? RuntimeCollection::Kind::Set : RuntimeCollection::Kind::Array;
                for (const auto& element : literal->elements) collection->values.push_back(evaluate(element.get(), environment));
            }
            return collection;
        }
        if (const auto* index = dynamic_cast<const IRIndexGet*>(operation)) {
            const RuntimeValue target = evaluate(index->target.get(), environment);
            const auto* collection = std::get_if<RuntimeCollectionRef>(&target);
            if (!collection || !*collection) return {};
            if ((*collection)->kind == RuntimeCollection::Kind::Map) {
                const auto found = (*collection)->mapped_values.find(runtime_string(evaluate(index->index.get(), environment)));
                return found == (*collection)->mapped_values.end() ? RuntimeValue{} : found->second;
            }
            const double raw_index = runtime_number(evaluate(index->index.get(), environment));
            if (raw_index < 0.0 || raw_index >= static_cast<double>((*collection)->values.size())) {
                throw ScriptRuntimeError("collection index is out of range");
            }
            return (*collection)->values[static_cast<std::size_t>(raw_index)];
        }
        if (const auto* call = dynamic_cast<const IRCall*>(operation)) {
            const auto parameter = std::find_if(
                m_definition->constructor_params.begin(),
                m_definition->constructor_params.end(),
                [call](const EngineScript::IRParam& candidate) {
                    return candidate.name == call->callee_name && candidate.type && candidate.type->name == "Log";
                }
            );
            if (parameter != m_definition->constructor_params.end()) {
                const std::string message = call->args.empty()
                    ? std::string{}
                    : runtime_string(evaluate(call->args.front().get(), environment));
                ENGINE_LOG_INFO("[SCRIPT] {}", message);
                return {};
            }
        }
        if (const auto* unary = dynamic_cast<const IRUnary*>(operation)) {
            const RuntimeValue operand = evaluate(unary->operand.get(), environment);
            if (unary->op == TokenType::Exclamation) return !runtime_boolean(operand);
            if (unary->op == TokenType::Minus) return -runtime_number(operand);
            return runtime_number(operand);
        }
        if (const auto* binary = dynamic_cast<const IRBinary*>(operation)) {
            const RuntimeValue left = evaluate(binary->left.get(), environment);
            if (binary->op == TokenType::AmpAmp && !runtime_boolean(left)) return false;
            if (binary->op == TokenType::PipePipe && runtime_boolean(left)) return true;
            const RuntimeValue right = evaluate(binary->right.get(), environment);
            switch (binary->op) {
                case TokenType::Plus: {
                    if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) {
                        return runtime_string(left) + runtime_string(right);
                    }
                    return runtime_number(left) + runtime_number(right);
                }
                case TokenType::Minus: return runtime_number(left) - runtime_number(right);
                case TokenType::Star: return runtime_number(left) * runtime_number(right);
                case TokenType::Slash: return runtime_number(right) == 0.0 ? 0.0 : runtime_number(left) / runtime_number(right);
                case TokenType::Less: return runtime_number(left) < runtime_number(right);
                case TokenType::LessEquals: return runtime_number(left) <= runtime_number(right);
                case TokenType::Greater: return runtime_number(left) > runtime_number(right);
                case TokenType::GreaterEquals: return runtime_number(left) >= runtime_number(right);
                case TokenType::EqualsEquals: {
                    if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) {
                        return runtime_string(left) == runtime_string(right);
                    }
                    return runtime_number(left) == runtime_number(right);
                }
                case TokenType::ExclamationEquals: {
                    if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) {
                        return runtime_string(left) != runtime_string(right);
                    }
                    return runtime_number(left) != runtime_number(right);
                }
                case TokenType::AmpAmp: return runtime_boolean(left) && runtime_boolean(right);
                case TokenType::PipePipe: return runtime_boolean(left) || runtime_boolean(right);
                default: return {};
            }
        }
        if (const auto* member = dynamic_cast<const IRMemberGet*>(operation)) {
            const RuntimeValue target = evaluate(member->target.get(), environment);

            if (const auto* entity = std::get_if<EngineScript::Entity>(&target)) {
                if (member->member_name == "id") return static_cast<double>(entity->id);
                if (member->member_name == "name") return entity->name;
                if (member->member_name == "tag") return entity->tag;
            }

            // Serialized Entity fields still store a stable EntityID.
            if (const auto* entity_id = std::get_if<EntityID>(&target)) {
                if (member->member_name == "id") return static_cast<double>(*entity_id);
                if (member->member_name == "name" || member->member_name == "tag") {
                    if (m_binding && m_binding->entity_manager && m_binding->entity_manager->is_alive(*entity_id)) {
                        if (m_binding->entity_manager->has_component<TagComponent>(*entity_id)) {
                            return m_binding->entity_manager->get_component<TagComponent>(*entity_id).tag;
                        }
                    }
                    return std::string("");
                }
            }

            // String target (e.g. str.length, str.size)
            if (const auto* str = std::get_if<std::string>(&target)) {
                if (member->member_name == "length" || member->member_name == "size") {
                    return static_cast<double>(str->size());
                }
            }

            const auto* object = std::get_if<RuntimeObjectRef>(&target);
            if (!object || !object->available || !m_binding || !m_binding->entity_manager) return {};
            if (object->kind == RuntimeObjectRef::Kind::RigidBody) {
                if (member->member_name == "velocity") {
                    return RuntimeObjectRef{RuntimeObjectRef::Kind::BodyVelocity, true};
                }
                if (member->member_name == "isGrounded") {
                    return PhysicsSystem::is_grounded(*m_binding->entity_manager, m_binding->entity);
                }
            }
            if (object->kind == RuntimeObjectRef::Kind::BodyVelocity) {
                const auto& body = m_binding->entity_manager->get_component<RigidBodyComponent>(m_binding->entity);
                if (member->member_name == "x") return static_cast<double>(body.velocity.x);
                if (member->member_name == "y") return static_cast<double>(body.velocity.y);
            }
            if (object->kind == RuntimeObjectRef::Kind::SpriteRenderer &&
                member->member_name == "flipX" &&
                m_binding->entity_manager->has_component<SpriteComponent>(m_binding->entity)) {
                return m_binding->entity_manager->get_component<SpriteComponent>(m_binding->entity).flip_x;
            }
            return {};
        }
        if (const auto* method = dynamic_cast<const IRMethodCall*>(operation)) {
            const RuntimeValue target = evaluate(method->target.get(), environment);

            if (const auto* collection_ref = std::get_if<RuntimeCollectionRef>(&target); collection_ref && *collection_ref) {
                auto& collection = **collection_ref;
                if (method->method_name == "push" || method->method_name == "add") {
                    if (!method->args.empty()) collection.values.push_back(evaluate(method->args[0].get(), environment));
                    return {};
                }
                if (method->method_name == "clear") {
                    collection.values.clear();
                    collection.mapped_values.clear();
                    return {};
                }
                if (collection.kind == RuntimeCollection::Kind::Map && method->method_name == "set" && method->args.size() == 2) {
                    collection.mapped_values[runtime_string(evaluate(method->args[0].get(), environment))] =
                        evaluate(method->args[1].get(), environment);
                    return {};
                }
                if (collection.kind == RuntimeCollection::Kind::Map && method->method_name == "get" && !method->args.empty()) {
                    const auto found = collection.mapped_values.find(runtime_string(evaluate(method->args[0].get(), environment)));
                    return found == collection.mapped_values.end() ? RuntimeValue{} : found->second;
                }
            }

            // String methods (e.g. other.name.includes("Coin"))
            if (const auto* str = std::get_if<std::string>(&target)) {
                if (method->method_name == "includes" || method->method_name == "contains") {
                    std::string arg = !method->args.empty() ? runtime_string(evaluate(method->args.front().get(), environment)) : "";
                    return EngineScript::StringOps::includes(*str, arg);
                }
                if (method->method_name == "startsWith" || method->method_name == "starts_with") {
                    std::string arg = !method->args.empty() ? runtime_string(evaluate(method->args.front().get(), environment)) : "";
                    return EngineScript::StringOps::starts_with(*str, arg);
                }
                if (method->method_name == "endsWith" || method->method_name == "ends_with") {
                    std::string arg = !method->args.empty() ? runtime_string(evaluate(method->args.front().get(), environment)) : "";
                    return EngineScript::StringOps::ends_with(*str, arg);
                }
                if (method->method_name == "indexOf" || method->method_name == "index_of") {
                    std::string arg = !method->args.empty() ? runtime_string(evaluate(method->args.front().get(), environment)) : "";
                    return static_cast<double>(EngineScript::StringOps::index_of(*str, arg));
                }
                if (method->method_name == "toLowerCase" || method->method_name == "to_lower") {
                    return EngineScript::StringOps::to_lower(*str);
                }
                if (method->method_name == "toUpperCase" || method->method_name == "to_upper") {
                    return EngineScript::StringOps::to_upper(*str);
                }
                if (method->method_name == "length" || method->method_name == "size") {
                    return static_cast<double>(str->size());
                }
            }

            if (const auto* entity = std::get_if<EngineScript::Entity>(&target)) {
                if (method->method_name == "destroy") {
                    entity->destroy();
                    return true;
                }
            }

            // Serialized Entity fields also expose destroy through their ID.
            if (const auto* entity_id = std::get_if<EntityID>(&target)) {
                if (method->method_name == "destroy" && m_binding && m_binding->entity_manager) {
                    m_binding->entity_manager->destroy_entity_subtree(*entity_id);
                    return true;
                }
            }

            const auto* object = std::get_if<RuntimeObjectRef>(&target);
            if (!object || !object->available) return {};
            if (object->kind == RuntimeObjectRef::Kind::Input && !method->args.empty()) {
                const std::string argument = runtime_string(evaluate(method->args.front().get(), environment));
                if (method->method_name == "axis") return static_cast<double>(Input::get_axis(argument));
                if (method->method_name == "pressed") return Input::is_action_just_pressed(argument);
            }
            if (object->kind == RuntimeObjectRef::Kind::Animator && method->method_name == "play" &&
                !method->args.empty() && m_binding && m_binding->entity_manager) {
                SpriteAnimationSystem::play_clip(
                    *m_binding->entity_manager,
                    m_binding->entity,
                    runtime_string(evaluate(method->args.front().get(), environment))
                );
            }
            if (object->kind == RuntimeObjectRef::Kind::Audio && m_binding && m_binding->services) {
                const ScriptRuntimeServices& services = *m_binding->services;
                std::string error;
                if (method->method_name == "playSound" && services.play_sound && !method->args.empty()) {
                    const float volume = method->args.size() >= 2
                        ? static_cast<float>(runtime_number(evaluate(method->args[1].get(), environment))) : 1.0f;
                    const float pitch = method->args.size() >= 3
                        ? static_cast<float>(runtime_number(evaluate(method->args[2].get(), environment))) : 1.0f;
                    return services.play_sound(
                        runtime_string(evaluate(method->args[0].get(), environment)), volume, pitch, error);
                }
                if (method->method_name == "playMusic" && services.play_music && !method->args.empty()) {
                    const float volume = method->args.size() >= 2
                        ? static_cast<float>(runtime_number(evaluate(method->args[1].get(), environment))) : 1.0f;
                    const bool loop = method->args.size() >= 3
                        ? runtime_boolean(evaluate(method->args[2].get(), environment)) : true;
                    return services.play_music(
                        runtime_string(evaluate(method->args[0].get(), environment)), volume, loop, error);
                }
                if (method->method_name == "stopMusic" && services.stop_music) services.stop_music();
                if (method->method_name == "pauseMusic" && services.pause_music) services.pause_music();
                if (method->method_name == "resumeMusic" && services.resume_music) services.resume_music();
                if (method->method_name == "setMasterVolume" && services.set_master_volume && !method->args.empty()) {
                    services.set_master_volume(static_cast<float>(runtime_number(evaluate(method->args[0].get(), environment))));
                }
                if (method->method_name == "setMusicVolume" && services.set_music_volume && !method->args.empty()) {
                    services.set_music_volume(static_cast<float>(runtime_number(evaluate(method->args[0].get(), environment))));
                }
                if (method->method_name == "setSfxVolume" && services.set_sfx_volume && !method->args.empty()) {
                    services.set_sfx_volume(static_cast<float>(runtime_number(evaluate(method->args[0].get(), environment))));
                }
                if (method->method_name == "masterVolume" && services.get_master_volume) return services.get_master_volume();
                if (method->method_name == "musicVolume" && services.get_music_volume) return services.get_music_volume();
                if (method->method_name == "sfxVolume" && services.get_sfx_volume) return services.get_sfx_volume();
            }
            if (object->kind == RuntimeObjectRef::Kind::Scene && m_binding && m_binding->services) {
                const ScriptRuntimeServices& services = *m_binding->services;
                std::string error;
                if (method->method_name == "load" && services.request_scene_load && !method->args.empty()) {
                    return services.request_scene_load(
                        runtime_string(evaluate(method->args[0].get(), environment)), error);
                }
                if (method->method_name == "currentName" && services.current_scene_name) {
                    return services.current_scene_name();
                }
            }
            return {};
        }
        if (const auto* optional = dynamic_cast<const IROptionalChain*>(operation)) {
            const RuntimeValue target = evaluate(optional->target.get(), environment);
            const auto* object = std::get_if<RuntimeObjectRef>(&target);
            if (!object || !object->available) return {};
            if (optional->is_method_call && object->kind == RuntimeObjectRef::Kind::Animator &&
                optional->member_name == "play" && !optional->method_args.empty() &&
                m_binding && m_binding->entity_manager) {
                SpriteAnimationSystem::play_clip(
                    *m_binding->entity_manager,
                    m_binding->entity,
                    runtime_string(evaluate(optional->method_args.front().get(), environment))
                );
            }
            return {};
        }
        return {};
    }

    enum class ExecutionSignal { None, Return, Break, Continue };

    ExecutionSignal execute(const EngineScript::IROperation* operation, RuntimeEnvironment& environment) const {
        using namespace EngineScript;
        if (!operation) return ExecutionSignal::None;
        if (const auto* ret = dynamic_cast<const IRReturn*>(operation)) {
            static_cast<void>(ret);
            return ExecutionSignal::Return;
        }
        if (const auto* declaration = dynamic_cast<const IRVarDecl*>(operation)) {
            environment[declaration->name] = evaluate(declaration->initializer.get(), environment);
            return ExecutionSignal::None;
        }
        if (const auto* assignment = dynamic_cast<const IRAssign*>(operation)) {
            RuntimeValue value = evaluate(assignment->value.get(), environment);
            auto local = environment.find(assignment->target_name);
            RuntimeValue current = local != environment.end() ? local->second : read_property(assignment->target_name);
            if (assignment->assignment_op == TokenType::PlusEquals) value = runtime_number(current) + runtime_number(value);
            else if (assignment->assignment_op == TokenType::MinusEquals) value = runtime_number(current) - runtime_number(value);
            if (local != environment.end()) local->second = value;
            else write_property(assignment->target_name, value);
            return ExecutionSignal::None;
        }
        if (const auto* member_set = dynamic_cast<const IRMemberSet*>(operation)) {
            const RuntimeValue target = evaluate(member_set->target.get(), environment);
            const auto* object = std::get_if<RuntimeObjectRef>(&target);
            if (object && object->available && object->kind == RuntimeObjectRef::Kind::BodyVelocity &&
                m_binding && m_binding->entity_manager &&
                m_binding->entity_manager->has_component<RigidBodyComponent>(m_binding->entity)) {
                glm::vec2 velocity = m_binding->entity_manager->get_component<RigidBodyComponent>(m_binding->entity).velocity;
                const float value = static_cast<float>(runtime_number(evaluate(member_set->value.get(), environment)));
                if (member_set->member_name == "x") velocity.x = value;
                if (member_set->member_name == "y") velocity.y = value;
                PhysicsSystem::set_velocity(*m_binding->entity_manager, m_binding->entity, velocity);
            }
            if (object && object->available && object->kind == RuntimeObjectRef::Kind::SpriteRenderer &&
                member_set->member_name == "flipX" && m_binding && m_binding->entity_manager &&
                m_binding->entity_manager->has_component<SpriteComponent>(m_binding->entity)) {
                m_binding->entity_manager->get_component<SpriteComponent>(m_binding->entity).flip_x =
                    runtime_boolean(evaluate(member_set->value.get(), environment));
            }
            return ExecutionSignal::None;
        }
        if (const auto* index_set = dynamic_cast<const IRIndexSet*>(operation)) {
            const RuntimeValue target = evaluate(index_set->target.get(), environment);
            const auto* collection_ref = std::get_if<RuntimeCollectionRef>(&target);
            if (collection_ref && *collection_ref) {
                auto& collection = **collection_ref;
                const RuntimeValue value = evaluate(index_set->value.get(), environment);
                if (collection.kind == RuntimeCollection::Kind::Map) {
                    collection.mapped_values[runtime_string(evaluate(index_set->index.get(), environment))] = value;
                } else {
                    const double raw_index = runtime_number(evaluate(index_set->index.get(), environment));
                    if (raw_index < 0.0 || raw_index >= static_cast<double>(collection.values.size())) {
                        throw ScriptRuntimeError("collection index is out of range");
                    }
                    collection.values[static_cast<std::size_t>(raw_index)] = value;
                }
            }
            return ExecutionSignal::None;
        }
        if (const auto* branch = dynamic_cast<const IRIf*>(operation)) {
            const auto& selected = runtime_boolean(evaluate(branch->condition.get(), environment))
                ? branch->then_operations
                : branch->else_operations;
            for (const auto& nested : selected) {
                const ExecutionSignal signal = execute(nested.get(), environment);
                if (signal != ExecutionSignal::None) return signal;
            }
            return ExecutionSignal::None;
        }
        if (const auto* loop = dynamic_cast<const IRWhile*>(operation)) {
            while (runtime_boolean(evaluate(loop->condition.get(), environment))) {
                for (const auto& nested : loop->body) {
                    const ExecutionSignal signal = execute(nested.get(), environment);
                    if (signal == ExecutionSignal::Return) return signal;
                    if (signal == ExecutionSignal::Break) return ExecutionSignal::None;
                    if (signal == ExecutionSignal::Continue) break;
                }
            }
            return ExecutionSignal::None;
        }
        if (const auto* loop = dynamic_cast<const IRFor*>(operation)) {
            if (loop->initializer) static_cast<void>(execute(loop->initializer.get(), environment));
            while (!loop->condition || runtime_boolean(evaluate(loop->condition.get(), environment))) {
                bool should_break = false;
                for (const auto& nested : loop->body) {
                    const ExecutionSignal signal = execute(nested.get(), environment);
                    if (signal == ExecutionSignal::Return) return signal;
                    if (signal == ExecutionSignal::Break) { should_break = true; break; }
                    if (signal == ExecutionSignal::Continue) break;
                }
                if (should_break) break;
                if (loop->update) static_cast<void>(execute(loop->update.get(), environment));
            }
            return ExecutionSignal::None;
        }
        if (const auto* loop = dynamic_cast<const IRForIn*>(operation)) {
            const RuntimeValue target = evaluate(loop->collection.get(), environment);
            const auto* collection_ref = std::get_if<RuntimeCollectionRef>(&target);
            if (!collection_ref || !*collection_ref) return ExecutionSignal::None;
            for (const RuntimeValue& value : (*collection_ref)->values) {
                environment[loop->variable_name] = value;
                bool should_break = false;
                for (const auto& nested : loop->body) {
                    const ExecutionSignal signal = execute(nested.get(), environment);
                    if (signal == ExecutionSignal::Return) return signal;
                    if (signal == ExecutionSignal::Break) { should_break = true; break; }
                    if (signal == ExecutionSignal::Continue) break;
                }
                if (should_break) break;
            }
            return ExecutionSignal::None;
        }
        if (dynamic_cast<const IRBreak*>(operation)) return ExecutionSignal::Break;
        if (dynamic_cast<const IRContinue*>(operation)) return ExecutionSignal::Continue;
        static_cast<void>(evaluate(operation, environment));
        return ExecutionSignal::None;
    }

    void execute_method(const std::string& name, std::vector<RuntimeValue> arguments) const {
        if (!m_definition) return;
        const auto found = std::find_if(
            m_definition->methods.begin(),
            m_definition->methods.end(),
            [&name](const EngineScript::IRMethod& method) { return method.name == name; }
        );
        if (found == m_definition->methods.end()) return;

        RuntimeEnvironment environment;
        for (std::size_t index = 0; index < found->params.size() && index < arguments.size(); ++index) {
            environment.emplace(found->params[index].name, std::move(arguments[index]));
        }
        for (const auto& block : found->blocks) {
            for (const auto& statement : block.statements) {
                if (execute(statement.op.get(), environment) == ExecutionSignal::Return) return;
            }
        }
    }

    std::shared_ptr<const EngineScript::IRClass> m_definition;
    ScriptRuntimeBinding* m_binding{nullptr};
};

struct CompiledAsset {
    bool success{false};
    std::string error;
    std::string chunk_name;
    std::vector<ScriptPublicField> fields;
    std::vector<EngineScript::BehaviourManifest> behaviours;
};

std::vector<ScriptPublicField> public_fields_from_behaviour(const EngineScript::BehaviourManifest& behaviour) {
    std::vector<ScriptPublicField> fields;
    for (const auto& property : behaviour.properties) {
        ScriptPublicField field;
        field.name = property.name;
        if (property.type == "Float" || property.type == "Int") {
            field.type = ScriptFieldType::Number;
            try {
                field.value = std::stod(property.default_value_str.empty() ? "0.0" : property.default_value_str);
            } catch (...) {
                field.value = 0.0;
            }
        } else if (property.type == "Bool") {
            field.type = ScriptFieldType::Boolean;
            field.value = property.default_value_str == "true";
        } else if (property.type == "String") {
            field.type = ScriptFieldType::String;
            field.value = property.default_value_str;
        } else if (property.type == "Vec2") {
            field.type = ScriptFieldType::Vector2;
            ScriptVector2 value{0.0f, 0.0f};
            if (!property.default_value_str.empty()) {
                const auto parsed = nlohmann::json::parse(
                    property.default_value_str, nullptr, false
                );
                if (parsed.is_array() && parsed.size() == 2 &&
                    parsed[0].is_number() && parsed[1].is_number()) {
                    value.x = parsed[0].get<float>();
                    value.y = parsed[1].get<float>();
                }
            }
            field.value = value;
        } else if (property.type.starts_with("ComponentRef<") || property.is_behaviour_reference) {
            field.type = ScriptFieldType::Entity;
            field.value = EntityID{0};
            field.component_type = property.is_behaviour_reference ? property.type : property.type.substr(
                std::string{"ComponentRef<"}.size(), property.type.size() - std::string{"ComponentRef<"}.size() - 1);
        } else if (property.type == "Prefab") {
            field.type = ScriptFieldType::Asset;
            field.value = AssetHandle{};
            field.asset_type = "Prefab";
        } else {
            continue;
        }
        fields.push_back(std::move(field));
    }
    return fields;
}

CompiledAsset compile_asset(AssetHandle handle, const AssetDatabase& database) {
    CompiledAsset result;
    const AssetRecord* record = database.find(handle);
    if (!record || record->type != AssetType::Script || record->status != AssetStatus::Ready) {
        result.error = "script asset is missing or unavailable";
        return result;
    }
    EngineScript::EngineScriptWorkspace ws(database.project_root().string());
    for (const AssetRecord& script_record : database.records()) {
        if (script_record.type != AssetType::Script || script_record.status != AssetStatus::Ready) continue;
        const std::string source = FileSystem::read_text(database.resolve(script_record.handle));
        if (!source.empty()) ws.add_source_file(script_record.source_path, source);
    }
    const auto res = ws.compile();
    if (!res.success) {
        result.error = res.diagnostics.empty() ? "EngineScript compilation failed" : res.diagnostics.front().message;
        return result;
    }
    result.chunk_name = "@" + record->source_path;
    result.success = true;
    for (const auto& behaviour : ws.metadata_manifest().behaviours) {
        if (behaviour.module_path != record->source_path) continue;
        result.behaviours.push_back(behaviour);
        auto fields = public_fields_from_behaviour(behaviour);
        result.fields.insert(result.fields.end(), fields.begin(), fields.end());
    }
    if (result.behaviours.size() != 1) {
        result.success = false;
        result.error = "script asset must export exactly one @behaviour class";
    }
    return result;
}

std::uint64_t current_time_millis() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

std::optional<std::uint64_t> file_signature(const std::string& path) {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) return std::nullopt;
    return static_cast<std::uint64_t>(time.time_since_epoch().count());
}

EngineScript::Entity script_entity(
    EntityManager& entities,
    EntityID id,
    const ScriptRuntimeBinding* binding
) {
    EngineScript::Entity result;
    result.id = id;
    if (entities.is_alive(id) && entities.has_component<TagComponent>(id)) {
        result.name = entities.get_component<TagComponent>(id).tag;
        result.tag = result.name;
    }
    result.runtime_context = &entities;
    result.destroy_callback = [](void* context, uint32_t entity_id) {
        auto* runtime_entities = static_cast<EntityManager*>(context);
        if (runtime_entities && runtime_entities->is_alive(entity_id)) {
            runtime_entities->destroy_entity_subtree(entity_id);
        }
    };
    if (binding) bind_dynamic_component_access(result, *binding);
    return result;
}

bool push_exposed_property(
    EngineScript::NativeBehaviour& behaviour,
    const ScriptPublicField& field,
    EntityManager& entities,
    const ScriptRuntimeBinding* binding = nullptr
) {
    switch (field.type) {
        case ScriptFieldType::Boolean:
            if (const auto* value = std::get_if<bool>(&field.value)) return behaviour.set_exposed_property(field.name, *value);
            break;
        case ScriptFieldType::Number:
            if (const auto* value = std::get_if<double>(&field.value)) return behaviour.set_exposed_property(field.name, *value);
            break;
        case ScriptFieldType::String:
            if (const auto* value = std::get_if<std::string>(&field.value)) return behaviour.set_exposed_property(field.name, *value);
            break;
        case ScriptFieldType::Vector2:
            if (const auto* value = std::get_if<ScriptVector2>(&field.value)) {
                return behaviour.set_exposed_property(field.name, glm::vec2{value->x, value->y});
            }
            break;
        case ScriptFieldType::Entity:
            if (const auto* value = std::get_if<EntityID>(&field.value)) {
                return behaviour.set_exposed_property(field.name, script_entity(entities, *value, binding));
            }
            break;
        case ScriptFieldType::Asset:
            if (const auto* value = std::get_if<AssetHandle>(&field.value)) {
                EngineScript::Asset asset;
                asset.handle = value->value;
                if (binding && binding->asset_database) {
                    if (const AssetRecord* record = binding->asset_database->find(*value)) {
                        asset.path = record->source_path;
                        asset.type = field.asset_type;
                        asset.status = record->status == AssetStatus::Ready ? "Ready" : "Unavailable";
                        asset.ready = record->status == AssetStatus::Ready;
                    }
                }
                return behaviour.set_exposed_property(field.name, asset);
            }
            break;
    }
    return false;
}

void pull_exposed_property(
    const EngineScript::NativeBehaviour& behaviour,
    ScriptPublicField& field
) {
    EngineScript::NativeBehaviour::ExposedPropertyValue value;
    if (!behaviour.get_exposed_property(field.name, value)) return;

    if (field.type == ScriptFieldType::Boolean) {
        if (const auto* typed = std::get_if<bool>(&value)) field.value = *typed;
    } else if (field.type == ScriptFieldType::Number) {
        if (const auto* typed = std::get_if<double>(&value)) field.value = *typed;
    } else if (field.type == ScriptFieldType::String) {
        if (const auto* typed = std::get_if<std::string>(&value)) field.value = *typed;
    } else if (field.type == ScriptFieldType::Vector2) {
        if (const auto* typed = std::get_if<glm::vec2>(&value)) field.value = ScriptVector2{typed->x, typed->y};
    } else if (field.type == ScriptFieldType::Asset) {
        if (const auto* typed = std::get_if<EngineScript::Asset>(&value)) field.value = AssetHandle{typed->handle};
    } else if (field.type == ScriptFieldType::Entity) {
        if (const auto* typed = std::get_if<EngineScript::Entity>(&value)) field.value = typed->id;
    }
}

void push_exposed_properties(
    EngineScript::NativeBehaviour& behaviour,
    const ScriptBehaviourSlot& component,
    EntityManager& entities,
    const ScriptRuntimeBinding* binding = nullptr
) {
    for (const ScriptPublicField& field : component.public_fields) {
        static_cast<void>(push_exposed_property(behaviour, field, entities, binding));
    }
}

void pull_exposed_properties(
    const EngineScript::NativeBehaviour& behaviour,
    ScriptBehaviourSlot& component
) {
    for (ScriptPublicField& field : component.public_fields) {
        pull_exposed_property(behaviour, field);
    }
}

ScriptBehaviourSlot* find_script_slot(EntityManager& entities, EntityID entity, ScriptSlotID slot_id) {
    if (!entities.is_alive(entity) || !entities.has_component<ScriptComponent>(entity)) return nullptr;
    auto& scripts = entities.get_component<ScriptComponent>(entity);
    const auto found = std::find_if(
        scripts.instances.begin(), scripts.instances.end(),
        [slot_id](const ScriptBehaviourSlot& candidate) { return candidate.id == slot_id; }
    );
    return found == scripts.instances.end() ? nullptr : &*found;
}

const ScriptBehaviourSlot* find_script_slot(
    const EntityManager& entities,
    EntityID entity,
    ScriptSlotID slot_id
) {
    if (!entities.is_alive(entity) || !entities.has_component<ScriptComponent>(entity)) return nullptr;
    const auto& scripts = entities.get_component<ScriptComponent>(entity);
    const auto found = std::find_if(
        scripts.instances.begin(), scripts.instances.end(),
        [slot_id](const ScriptBehaviourSlot& candidate) { return candidate.id == slot_id; }
    );
    return found == scripts.instances.end() ? nullptr : &*found;
}

} // namespace

ScriptRuntime::ScriptRuntime() {}

ScriptRuntime::~ScriptRuntime() {
    shutdown();
}

ScriptRuntime::ScriptRuntime(ScriptRuntime&& other) noexcept {
    *this = std::move(other);
}

ScriptRuntime& ScriptRuntime::operator=(ScriptRuntime&& other) noexcept {
    if (this == &other) return *this;
    shutdown();

    EventDispatcher* dispatcher = other.m_event_dispatcher;
    other.unsubscribe_from_events();
    m_entity_manager = other.m_entity_manager;
    m_asset_database = other.m_asset_database;
    m_event_dispatcher = dispatcher;
    m_services = std::move(other.m_services);
    m_instances = std::move(other.m_instances);
    m_native_workspace = std::move(other.m_native_workspace);
    m_native_async_scheduler = std::move(other.m_native_async_scheduler);
    m_typed_event_bus = std::move(other.m_typed_event_bus);
    m_native_module = std::move(other.m_native_module);
    m_statuses = std::move(other.m_statuses);
    m_source_signatures = std::move(other.m_source_signatures);
    m_reload_errors = std::move(other.m_reload_errors);
    m_next_hot_reload_poll_ms = other.m_next_hot_reload_poll_ms;
    m_pending_events = std::move(other.m_pending_events);
    m_pending_component_removals = std::move(other.m_pending_component_removals);
    m_phase_sequence = other.m_phase_sequence;
    m_phase_active = false;

    other.m_entity_manager = nullptr;
    other.m_asset_database = nullptr;
    other.m_services = {};
    other.m_instances.clear();
    other.m_native_workspace.reset();
    other.m_native_async_scheduler.reset();
    other.m_typed_event_bus.reset();
    other.m_native_module.reset();
    other.m_statuses.clear();
    other.m_source_signatures.clear();
    other.m_reload_errors.clear();
    other.m_next_hot_reload_poll_ms = 0;
    other.m_pending_events.clear();
    other.m_pending_component_removals.clear();
    other.m_phase_sequence = 0;
    other.m_phase_active = false;

    subscribe_to_events();
    return *this;
}

ScriptInspectionResult ScriptRuntime::inspect_asset(
    AssetHandle handle,
    const AssetDatabase& asset_database
) {
    const CompiledAsset compiled = compile_asset(handle, asset_database);
    return {compiled.success, compiled.fields, compiled.error};
}

ScriptRuntimeStartResult ScriptRuntime::start(
    EntityManager& entity_manager,
    const AssetDatabase& asset_database,
    EventDispatcher* event_dispatcher,
    ScriptRuntimeServices services
) {
    shutdown();
    m_statuses.clear();
    m_entity_manager = &entity_manager;
    m_asset_database = &asset_database;
    m_event_dispatcher = event_dispatcher;
    m_services = std::move(services);
    if (!m_native_workspace || m_native_workspace->source_manager().project_root() != asset_database.project_root().string()) {
        m_native_workspace = std::make_unique<EngineScript::EngineScriptWorkspace>(asset_database.project_root().string());
    }
    m_native_async_scheduler = std::make_unique<EngineScript::AsyncScheduler>();
    m_typed_event_bus = std::make_unique<EngineScript::TypedEventBus>();
    subscribe_to_events();

    ScriptRuntimeStartResult result;
    std::unordered_set<std::string> ready_script_paths;
    for (const AssetRecord& record : asset_database.records()) {
        if (record.type != AssetType::Script || record.status != AssetStatus::Ready) continue;
        const std::string source = FileSystem::read_text(asset_database.resolve(record.handle));
        if (source.empty()) continue;
        ready_script_paths.insert(record.source_path);
        m_native_workspace->add_source_file(record.source_path, source);
        m_source_signatures[record.handle] = file_signature(asset_database.resolve(record.handle));
    }

    const std::vector<std::string> registered_scripts = m_native_workspace->registered_files();
    for (const auto& registered_path : registered_scripts) {
        if (!ready_script_paths.contains(registered_path)) {
            m_native_workspace->remove_source_file(registered_path);
        }
    }

    const EngineScript::CompilationResult compilation = m_native_workspace->compile(false);
    if (!compilation.success) {
        const std::string error = compilation.diagnostics.empty()
            ? "EngineScript project compilation failed"
            : compilation.diagnostics.front().message;
        std::string source_path;
        uint32_t line = 0;
        uint32_t column = 0;
        if (!compilation.diagnostics.empty()) {
            const EngineScript::Diagnostic& diagnostic = compilation.diagnostics.front();
            line = diagnostic.span.start_line;
            column = diagnostic.span.start_column;
            if (const EngineScript::SourceFile* source = m_native_workspace->source_manager().get_file(diagnostic.span.file_id)) {
                source_path = source->path;
            }
        }
        entity_manager.for_each<ScriptComponent>([&](EntityID entity, const ScriptComponent& component) {
            for (const ScriptBehaviourSlot& instance : component.instances) {
                if (!instance.enabled) continue;
                ++result.failed;
                set_error(entity, error, source_path, line, column);
            }
        });
        return result;
    }

    std::shared_ptr<EngineScript::GameModule> generated_native_module;
    std::string native_build_error;
    if (!m_services.native_module_path.empty()) {
        std::unique_ptr<EngineScript::GameModule> loaded = EngineScript::GameModule::load_dynamic_library(
            m_services.native_module_path,
            &native_build_error
        );
        if (loaded) generated_native_module = std::shared_ptr<EngineScript::GameModule>(std::move(loaded));
    } else {
        const EngineScript::NativeModuleBuildResult native_build = EngineScript::NativeModuleBuilder::build(
            *m_native_workspace,
            asset_database.project_root().string(),
            m_next_native_build_number++
        );
        if (native_build.success) {
            std::unique_ptr<EngineScript::GameModule> loaded = EngineScript::GameModule::load_dynamic_library(
                native_build.library_path,
                &native_build_error
            );
            if (loaded) generated_native_module = std::shared_ptr<EngineScript::GameModule>(std::move(loaded));
        } else {
            native_build_error = native_build.error;
        }
    }
    if (!generated_native_module) {
        const std::string error = native_build_error.empty()
            ? "EngineScript native module could not be loaded"
            : native_build_error;
        entity_manager.for_each<ScriptComponent>([&](EntityID entity, const ScriptComponent& component) {
            for (const ScriptBehaviourSlot& instance : component.instances) {
                if (!instance.enabled) continue;
                ++result.failed;
                set_error(entity, error);
            }
        });
        return result;
    }

    m_native_module = std::move(generated_native_module);

    entity_manager.for_each<ScriptComponent>([&](EntityID entity, const ScriptComponent& scripts) {
        std::vector<ScriptSlotID> ordered_slots;
        ordered_slots.reserve(scripts.instances.size());
        for (const ScriptBehaviourSlot& slot : scripts.instances) ordered_slots.push_back(slot.id);
        for (const ScriptSlotID slot_id : ordered_slots) {
            ScriptBehaviourSlot* slot = find_script_slot(entity_manager, entity, slot_id);
            if (!slot || !slot->enabled) continue;
            std::string error;
            if (instantiate_slot(entity, *slot, error)) ++result.started;
            else {
                ++result.failed;
                set_error(entity, std::move(error));
            }
        }
    });

    return result;
}

bool ScriptRuntime::instantiate_slot(EntityID entity, ScriptBehaviourSlot& slot, std::string& error, bool start_lifecycle) {
    if (!slot.enabled) return true;
    if (!m_entity_manager || !m_asset_database || !m_native_workspace || !m_native_module ||
        !m_entity_manager->is_alive(entity)) {
        error = "EngineScript runtime is not ready";
        return false;
    }
    if (std::any_of(m_instances.begin(), m_instances.end(), [entity, &slot](const Instance& instance) {
            return instance.entity == entity && instance.slot_id == slot.id && instance.behaviour;
        })) return true;
    if (!m_instantiating_slots.insert(slot.id).second) {
        error = "cyclic required behaviour dependency";
        return false;
    }
    struct InstantiationGuard {
        std::unordered_set<ScriptSlotID>& slots;
        ScriptSlotID id;
        ~InstantiationGuard() { slots.erase(id); }
    } guard{m_instantiating_slots, slot.id};

    const AssetRecord* record = m_asset_database->find(slot.script_handle);
    if (!record || record->type != AssetType::Script || record->status != AssetStatus::Ready) {
        error = "script asset is missing or unavailable";
        return false;
    }
    const auto& behaviours = m_native_workspace->metadata_manifest().behaviours;
    const auto manifest = std::find_if(behaviours.begin(), behaviours.end(), [record](const auto& behaviour) {
        return behaviour.module_path == record->source_path;
    });
    if (manifest == behaviours.end() ||
        std::count_if(behaviours.begin(), behaviours.end(), [record](const auto& behaviour) {
            return behaviour.module_path == record->source_path;
        }) != 1) {
        error = "script asset must export exactly one @behaviour class";
        return false;
    }

    const auto has_native_component = [this, entity](const std::string& type) {
        if (type == "Transform2D") return m_entity_manager->has_component<TransformComponent>(entity);
        if (type == "RigidBody2D") return m_entity_manager->has_component<RigidBodyComponent>(entity);
        if (type == "SpriteRenderer2D") return m_entity_manager->has_component<SpriteComponent>(entity);
        if (type == "Animator2D") return m_entity_manager->has_component<AnimationComponent>(entity);
        if (type == "ParticleEmitter2D") return m_entity_manager->has_component<ParticleEmitter2DComponent>(entity);
        return false;
    };
    const auto is_native_component = [](const std::string& type) {
        return type == "Transform2D" || type == "RigidBody2D" || type == "SpriteRenderer2D" ||
            type == "Animator2D" || type == "ParticleEmitter2D";
    };
    const auto is_service = [](const std::string& type) {
        static const std::unordered_set<std::string> services{
            "Input", "Log", "Audio", "Scene", "Time", "Entities", "Display", "Physics",
            "Events", "Assets", "UI", "Tilemap", "PlayerPrefs", "Particles", "WorldText", "Prefabs"
        };
        return services.contains(type);
    };

    for (const auto& dependency : manifest->dependencies) {
        if (dependency.is_service && !is_service(dependency.type)) {
            error = "unsupported injected service: " + dependency.type;
            return false;
        }
        if (!dependency.is_component || dependency.is_optional) continue;
        if (is_native_component(dependency.type)) {
            if (!has_native_component(dependency.type)) {
                error = "missing required " + dependency.type + " component";
                return false;
            }
            continue;
        }
        if (count_behaviour_components(entity, dependency.type) != 0) continue;
        if (!m_entity_manager->has_component<ScriptComponent>(entity)) {
            error = "missing required " + dependency.type + " behaviour";
            return false;
        }
        ScriptBehaviourSlot* dependency_slot = nullptr;
        auto& scripts = m_entity_manager->get_component<ScriptComponent>(entity);
        for (ScriptBehaviourSlot& candidate : scripts.instances) {
            const AssetRecord* candidate_record = m_asset_database->find(candidate.script_handle);
            if (!candidate.enabled || !candidate_record) continue;
            const auto candidate_manifest = std::find_if(
                behaviours.begin(), behaviours.end(), [candidate_record](const auto& behaviour) {
                    return behaviour.module_path == candidate_record->source_path;
                }
            );
            if (candidate_manifest != behaviours.end() && candidate_manifest->name == dependency.type) {
                dependency_slot = &candidate;
                break;
            }
        }
        if (!dependency_slot || !instantiate_slot(entity, *dependency_slot, error)) {
            if (error.empty()) error = "missing required " + dependency.type + " behaviour";
            return false;
        }
    }

    std::vector<ScriptPublicField> compiled_fields = public_fields_from_behaviour(*manifest);
    for (const ScriptPublicField& default_field : compiled_fields) {
        const auto existing = std::find_if(slot.public_fields.begin(), slot.public_fields.end(),
            [&default_field](const ScriptPublicField& field) {
                return field.name == default_field.name && field.type == default_field.type;
            });
        if (existing == slot.public_fields.end()) slot.public_fields.push_back(default_field);
    }

    Instance instance;
    instance.entity = entity;
    instance.slot_id = slot.id;
    instance.eligible_phase = m_phase_sequence + (m_phase_active ? 1u : 0u);
    instance.script_handle = slot.script_handle;
    instance.script_path = record->source_path;
    instance.class_name = manifest->name;
    instance.field_definitions = std::move(compiled_fields);
    instance.binding = std::make_unique<ScriptRuntimeBinding>();
    instance.binding->runtime = this;
    instance.binding->entity_manager = m_entity_manager;
    instance.binding->event_dispatcher = m_event_dispatcher;
    instance.binding->asset_database = m_asset_database;
    instance.binding->entity = entity;
    instance.binding->slot_id = slot.id;
    instance.binding->services = &m_services;
    instance.binding->async_scheduler = m_native_async_scheduler.get();
    instance.binding->typed_event_bus = m_typed_event_bus.get();
    instance.binding->main_thread_id = std::this_thread::get_id();

    const auto registration = std::find_if(
        m_native_module->descriptor().behaviour_types.begin(),
        m_native_module->descriptor().behaviour_types.end(),
        [&instance](const auto& candidate) { return candidate.type_name == instance.class_name; }
    );
    if (registration == m_native_module->descriptor().behaviour_types.end()) {
        error = "native EngineScript behaviour registration is missing";
        return false;
    }
    instance.native_host = std::make_unique<NativeBehaviourHost>(*instance.binding);
    if (!instance.native_host->prepare(*registration, error)) return false;
    instance.native_host->sync_before();
    instance.behaviour.reset(m_native_module->create_behaviour(instance.class_name, instance.native_host->arguments()));
    if (!instance.behaviour) {
        error = "could not instantiate EngineScript behaviour " + instance.class_name;
        return false;
    }
    instance.behaviour->entity_id = entity;
    instance.behaviour->owner_entity = script_entity(*m_entity_manager, entity, instance.binding.get());
    instance.behaviour->task_scope_id = slot.id;
    instance.behaviour->async_scheduler = m_native_async_scheduler.get();
    instance.behaviour->entities = instance.native_host->entities();
    push_exposed_properties(*instance.behaviour, slot, *m_entity_manager, instance.binding.get());
    if (start_lifecycle) try {
        instance.behaviour->on_start();
    } catch (const std::exception& runtime_error) {
        error = std::string{"EngineScript runtime error in on_start: "} + runtime_error.what();
        return false;
    } catch (...) {
        error = "EngineScript runtime error in on_start: unknown exception";
        return false;
    }
    if (start_lifecycle) { instance.started = true; instance.native_host->sync_after(); pull_exposed_properties(*instance.behaviour, slot); }
    m_instances.push_back(std::move(instance));
    return true;
}

bool ScriptRuntime::prepare_entity(EntityID entity) {
    if (!m_entity_manager || !m_entity_manager->has_component<ScriptComponent>(entity)) return true;
    // create_prefab() stages an entity through instantiate_prefab(), which also
    // queues its normal end-of-frame start. Preparing it immediately must take
    // ownership of that start and discard any runtime instances left behind by
    // a recycled EntityID.
    std::erase(m_pending_entity_starts, entity);
    discard_entity_instances(entity);
    auto& scripts = m_entity_manager->get_component<ScriptComponent>(entity);
    for (auto& slot : scripts.instances) { std::string error; if (slot.enabled && !instantiate_slot(entity, slot, error, false)) { set_error(entity, error); return false; } }
    return true;
}

void ScriptRuntime::discard_entity_instances(EntityID entity) {
    for (auto instance = m_instances.begin(); instance != m_instances.end();) {
        if (instance->entity != entity) {
            ++instance;
            continue;
        }
        if (instance->behaviour) {
            if (m_native_async_scheduler) {
                m_native_async_scheduler->cancel_scope(instance->behaviour->task_scope_id);
            }
            if (m_typed_event_bus) {
                m_typed_event_bus->cancel_scope(instance->behaviour->task_scope_id);
            }
            instance->behaviour->cancel_owned_tasks();
        }
        instance = m_instances.erase(instance);
    }
    m_statuses.erase(entity);
}

bool ScriptRuntime::activate_entity(EntityID entity) {
    for (auto& instance : m_instances) if (instance.entity == entity && !instance.started && instance.behaviour) {
        auto* slot = find_script_slot(*m_entity_manager, entity, instance.slot_id); if (!slot) continue;
        try {
            // A staged prefab can be positioned after its native host was prepared.
            // Refresh cached component proxies so sync_after() cannot restore the old
            // creation transform (typically 0,0) after on_start().
            if (instance.native_host) instance.native_host->sync_before();
            instance.behaviour->on_start();
            instance.started = true;
            if (instance.native_host) instance.native_host->sync_after();
            pull_exposed_properties(*instance.behaviour, *slot);
        }
        catch (const std::exception& e) { set_error(entity, e.what(), instance.script_path); return false; }
    }
    return true;
}

void ScriptRuntime::queue_entity_start(EntityID entity) {
    if (!m_entity_manager || entity == 0 || !m_entity_manager->is_alive(entity)) return;
    // Entity IDs are recycled immediately. A newly instantiated prefab must
    // never inherit the native behaviour object or execution status belonging
    // to the previous entity that used the same numeric ID.
    discard_entity_instances(entity);
    if (std::find(m_pending_entity_starts.begin(), m_pending_entity_starts.end(), entity) == m_pending_entity_starts.end()) {
        m_pending_entity_starts.push_back(entity);
    }
}

void ScriptRuntime::subscribe_to_events() {
    if (!m_event_dispatcher) return;

    m_event_subscriptions.push_back(
        m_event_dispatcher->subscribe<CollisionEvent>([this](const CollisionEvent& event) {
            PendingEvent pending;
            pending.type = PendingEventType::CollisionEnter;
            pending.entity_a = event.entity_a;
            pending.entity_b = event.entity_b;
            pending.normal_x = event.contact_normal.x;
            pending.normal_y = event.contact_normal.y;
            pending.is_trigger = event.is_trigger;
            m_pending_events.push_back(std::move(pending));
        })
    );

    m_event_subscriptions.push_back(
        m_event_dispatcher->subscribe<CollisionEndEvent>([this](const CollisionEndEvent& event) {
            PendingEvent pending;
            pending.type = PendingEventType::CollisionExit;
            pending.entity_a = event.entity_a;
            pending.entity_b = event.entity_b;
            pending.is_trigger = event.is_trigger;
            m_pending_events.push_back(std::move(pending));
        })
    );

    m_event_subscriptions.push_back(
        m_event_dispatcher->subscribe<UIButtonClickedEvent>([this](const UIButtonClickedEvent& event) {
            PendingEvent pending;
            pending.type = PendingEventType::ButtonClicked;
            pending.entity_a = event.entity;
            m_pending_events.push_back(std::move(pending));
        })
    );

    m_event_subscriptions.push_back(
        m_event_dispatcher->subscribe<UIFocusChangedEvent>([this](const UIFocusChangedEvent& event) {
            PendingEvent pending;
            pending.type = PendingEventType::FocusChanged;
            pending.entity_a = event.previous_entity;
            pending.entity_b = event.focused_entity;
            m_pending_events.push_back(std::move(pending));
        })
    );
}

void ScriptRuntime::unsubscribe_from_events() {
    if (!m_event_dispatcher) {
        m_event_subscriptions.clear();
        return;
    }
    for (const std::uint64_t id : m_event_subscriptions) {
        m_event_dispatcher->unsubscribe(id);
    }
    m_event_subscriptions.clear();
}

void ScriptRuntime::dispatch_events() {
    if (!m_entity_manager) {
        m_pending_events.clear();
        return;
    }

    for (const auto& event : m_pending_events) {
        if (event.type == PendingEventType::ButtonClicked) {
            for (auto& instance : m_instances) {
                if (!instance.behaviour || instance.entity != event.entity_a) continue;
                if (instance.native_host) instance.native_host->sync_before();
                try {
                    instance.behaviour->on_button_clicked();
                } catch (const std::exception& runtime_error) {
                    set_error(instance.entity,
                        std::string{"EngineScript runtime error in on_button_clicked: "} + runtime_error.what(),
                        instance.script_path);
                    if (m_native_async_scheduler) m_native_async_scheduler->cancel_scope(instance.behaviour->task_scope_id);
                    if (m_typed_event_bus) m_typed_event_bus->cancel_scope(instance.behaviour->task_scope_id);
                    instance.behaviour->cancel_owned_tasks();
                    continue;
                } catch (...) {
                    set_error(instance.entity, "EngineScript runtime error in on_button_clicked: unknown exception",
                        instance.script_path);
                    continue;
                }
                if (instance.native_host) instance.native_host->sync_after();
                if (ScriptBehaviourSlot* slot = find_script_slot(
                        *m_entity_manager, instance.entity, instance.slot_id)) {
                    pull_exposed_properties(*instance.behaviour, *slot);
                }
            }
        } else if (event.type == PendingEventType::CollisionEnter) {
            for (auto& instance : m_instances) {
                if (!instance.behaviour) continue;
                const EntityID other_id = instance.entity == event.entity_a
                    ? event.entity_b
                    : instance.entity == event.entity_b ? event.entity_a : 0;
                if (other_id != 0) {
                    EngineScript::Entity other;
                    other.id = other_id;
                    if (m_entity_manager->is_alive(other_id) &&
                        m_entity_manager->has_component<TagComponent>(other_id)) {
                        other.name = m_entity_manager->get_component<TagComponent>(other_id).tag;
                        other.tag = other.name;
                    }
                    other.runtime_context = m_entity_manager;
                    other.destroy_callback = [](void* context, uint32_t id) {
                        auto* entities = static_cast<EntityManager*>(context);
                        if (entities && entities->is_alive(id)) entities->destroy_entity_subtree(id);
                    };
                    if (instance.binding) bind_dynamic_component_access(other, *instance.binding);
                    if (instance.native_host) instance.native_host->sync_before();
                    try {
                        instance.behaviour->on_collision_enter(other);
                    } catch (const std::exception& runtime_error) {
                        set_error(instance.entity,
                            std::string{"EngineScript runtime error in on_collision_enter: "} + runtime_error.what(),
                            instance.script_path);
                        if (m_native_async_scheduler) m_native_async_scheduler->cancel_scope(instance.behaviour->task_scope_id);
                        if (m_typed_event_bus) m_typed_event_bus->cancel_scope(instance.behaviour->task_scope_id);
                        instance.behaviour->cancel_owned_tasks();
                        continue;
                    } catch (...) {
                        set_error(instance.entity, "EngineScript runtime error in on_collision_enter: unknown exception",
                            instance.script_path);
                        continue;
                    }
                    if (instance.native_host) instance.native_host->sync_after();
                    if (ScriptBehaviourSlot* slot = find_script_slot(
                            *m_entity_manager, instance.entity, instance.slot_id)) {
                        pull_exposed_properties(
                            *instance.behaviour,
                            *slot
                        );
                    }
                }
            }
        }
    }

    m_pending_events.clear();
    flush_pending_component_removals();
}

ScriptHotReloadResult ScriptRuntime::reload_changed_scripts(bool force) {
    if (!m_services.hot_reload_enabled && !force) return {};
    if (!m_entity_manager || !m_asset_database) return {};

    const std::uint64_t now = current_time_millis();
    if (!force && now < m_next_hot_reload_poll_ms) return {};
    m_next_hot_reload_poll_ms = now + 500;

    ScriptHotReloadResult result;
    std::unordered_set<AssetHandle> checked_assets;
    m_entity_manager->for_each<ScriptComponent>([&](EntityID, const ScriptComponent& component) {
        for (const ScriptBehaviourSlot& instance : component.instances) {
            if (instance.enabled && instance.script_handle.is_valid()) checked_assets.insert(instance.script_handle);
        }
    });

    std::vector<AssetHandle> changed_assets;

    for (const AssetHandle handle : checked_assets) {
        const AssetRecord* record = m_asset_database->find(handle);
        if (!record || record->type != AssetType::Script || record->status != AssetStatus::Ready) {
            changed_assets.push_back(handle);
            continue;
        }

        const std::string path = m_asset_database->resolve(handle);
        const std::optional<std::uint64_t> current_signature = file_signature(path);
        const auto previous_signature_it = m_source_signatures.find(handle);
        const bool signature_known = previous_signature_it != m_source_signatures.end();
        const bool changed = !signature_known || (current_signature && current_signature != previous_signature_it->second);

        if (!changed && !force) continue;
        changed_assets.push_back(handle);
    }

    if (changed_assets.empty()) return result;

    EngineScript::EngineScriptWorkspace candidate_workspace(m_asset_database->project_root().string());
    for (const AssetRecord& record : m_asset_database->records()) {
        if (record.type != AssetType::Script || record.status != AssetStatus::Ready) continue;
        const std::string source = FileSystem::read_text(m_asset_database->resolve(record.handle));
        if (!source.empty()) candidate_workspace.add_source_file(record.source_path, source);
    }
    const EngineScript::CompilationResult candidate_compilation = candidate_workspace.compile(false);
    std::string candidate_error;
    if (!candidate_compilation.success) {
        candidate_error = candidate_compilation.diagnostics.empty()
            ? "EngineScript hot reload compilation failed"
            : candidate_compilation.diagnostics.front().message;
    } else {
        const EngineScript::NativeModuleBuildResult candidate_build = EngineScript::NativeModuleBuilder::build(
            candidate_workspace,
            m_asset_database->project_root().string(),
            m_next_native_build_number
        );
        if (!candidate_build.success) {
            candidate_error = candidate_build.error;
        } else {
            std::string load_error;
            if (!EngineScript::GameModule::load_dynamic_library(candidate_build.library_path, &load_error)) {
                candidate_error = load_error;
            }
        }
    }
    if (!candidate_error.empty()) {
        result.failed = changed_assets.size();
        for (const AssetHandle handle : changed_assets) m_reload_errors[handle] = candidate_error;
        ENGINE_LOG_ERROR("[SCRIPT RELOAD] Candidate rejected; active module kept: {}", candidate_error);
        return result;
    }

    EntityManager* entity_manager = m_entity_manager;
    const AssetDatabase* asset_database = m_asset_database;
    EventDispatcher* event_dispatcher = m_event_dispatcher;
    ScriptRuntimeServices services = m_services;
    const ScriptRuntimeStartResult restarted = start(
        *entity_manager,
        *asset_database,
        event_dispatcher,
        std::move(services)
    );
    result.reloaded = restarted.started;
    result.failed = restarted.failed;

    if (restarted.failed > 0) {
        std::string error = "EngineScript hot reload failed after candidate validation";
        for (const AssetHandle handle : changed_assets) m_reload_errors[handle] = error;
        ENGINE_LOG_ERROR("[SCRIPT RELOAD] {}", error);
    } else {
        for (const AssetHandle handle : changed_assets) m_reload_errors.erase(handle);
    }

    return result;
}

void ScriptRuntime::update(float delta_time) {
    if (m_native_async_scheduler) {
        for (auto& inst : m_instances) {
            const auto status_found = m_statuses.find(inst.entity);
            if (status_found != m_statuses.end() && status_found->second.state == ScriptExecutionState::Error) continue;
            if (!m_entity_manager || !inst.behaviour) continue;
            ScriptBehaviourSlot* component = find_script_slot(*m_entity_manager, inst.entity, inst.slot_id);
            if (!component || !component->enabled) continue;
            if (inst.native_host) inst.native_host->sync_before();
            push_exposed_properties(*inst.behaviour, *component, *m_entity_manager, inst.binding.get());
        }
        if (m_native_async_scheduler) {
            m_native_async_scheduler->step(delta_time, delta_time);
        }
        for (auto& inst : m_instances) {
            if (!m_entity_manager || !inst.behaviour) continue;
            ScriptBehaviourSlot* component = find_script_slot(*m_entity_manager, inst.entity, inst.slot_id);
            if (!component) continue;
            if (inst.native_host) inst.native_host->sync_after();
            pull_exposed_properties(*inst.behaviour, *component);
        }
    }
    for (auto& inst : m_instances) {
        if (inst.behaviour) inst.behaviour->reap_completed_tasks();
        const auto status_found = m_statuses.find(inst.entity);
        if (status_found != m_statuses.end() && status_found->second.state == ScriptExecutionState::Error) continue;
        if (!m_entity_manager) continue;
        ScriptBehaviourSlot* component = find_script_slot(*m_entity_manager, inst.entity, inst.slot_id);
        if (component && component->enabled && inst.behaviour) {
            if (inst.native_host) inst.native_host->sync_before();
            push_exposed_properties(*inst.behaviour, *component, *m_entity_manager, inst.binding.get());
            try {
                inst.behaviour->update(delta_time);
                if (inst.native_host) inst.native_host->sync_after();
                if (ScriptBehaviourSlot* live_slot = find_script_slot(
                        *m_entity_manager, inst.entity, inst.slot_id)) {
                    pull_exposed_properties(*inst.behaviour, *live_slot);
                }
            } catch (const std::exception& runtime_error) {
                set_error(inst.entity, std::string{"EngineScript runtime error in update: "} + runtime_error.what(),
                    inst.script_path);
                if (m_native_async_scheduler) m_native_async_scheduler->cancel_scope(inst.behaviour->task_scope_id);
                if (m_typed_event_bus) m_typed_event_bus->cancel_scope(inst.behaviour->task_scope_id);
                inst.behaviour->cancel_owned_tasks();
            } catch (...) {
                set_error(inst.entity, "EngineScript runtime error in update: unknown exception", inst.script_path);
            }
        }
    }
    flush_pending_component_removals();

    std::vector<EntityID> pending = std::move(m_pending_entity_starts);
    m_pending_entity_starts.clear();
    for (const EntityID entity : pending) {
        if (!m_entity_manager || !m_entity_manager->is_alive(entity) ||
            !m_entity_manager->has_component<ScriptComponent>(entity)) continue;
        ScriptComponent& scripts = m_entity_manager->get_component<ScriptComponent>(entity);
        std::vector<ScriptSlotID> slot_ids;
        for (const ScriptBehaviourSlot& slot : scripts.instances) slot_ids.push_back(slot.id);
        for (const ScriptSlotID slot_id : slot_ids) {
            ScriptBehaviourSlot* slot = find_script_slot(*m_entity_manager, entity, slot_id);
            if (!slot || !slot->enabled) continue;
            std::string error;
            if (!instantiate_slot(entity, *slot, error)) set_error(entity, std::move(error));
        }
        activate_entity(entity);
    }
}

void ScriptRuntime::shutdown() {
    unsubscribe_from_events();
    for (auto& inst : m_instances) {
        if (inst.behaviour) {
            if (m_native_async_scheduler) m_native_async_scheduler->cancel_scope(inst.behaviour->task_scope_id);
            if (m_typed_event_bus) m_typed_event_bus->cancel_scope(inst.behaviour->task_scope_id);
            inst.behaviour->cancel_owned_tasks();
            inst.behaviour->on_destroy();
        }
    }
    // Music started through the gameplay service belongs to the active script
    // session. Enforce cleanup even when authored behaviours omit on_destroy().
    if (m_services.stop_music) m_services.stop_music();
    m_instances.clear();
    m_pending_entity_starts.clear();
    m_native_module.reset();
    if (m_typed_event_bus) m_typed_event_bus->clear();
    m_typed_event_bus.reset();
    m_native_async_scheduler.reset();

    m_entity_manager = nullptr;
    m_asset_database = nullptr;
    m_services = {};
    m_statuses.clear();
    m_source_signatures.clear();
    m_reload_errors.clear();
    m_next_hot_reload_poll_ms = 0;
    m_pending_events.clear();
}

void ScriptRuntime::set_error(
    EntityID entity,
    std::string message,
    std::string source_path,
    uint32_t line,
    uint32_t column
) {
    if (source_path.empty() && m_entity_manager && m_asset_database &&
        m_entity_manager->is_alive(entity) &&
        m_entity_manager->has_component<ScriptComponent>(entity)) {
        const ScriptComponent& component = m_entity_manager->get_component<ScriptComponent>(entity);
        for (const ScriptBehaviourSlot& instance : component.instances) {
            if (const AssetRecord* asset = m_asset_database->find(instance.script_handle)) {
                source_path = asset->source_path;
                break;
            }
        }
    }
    m_statuses[entity] = {ScriptExecutionState::Error, std::move(message), source_path, line, column};
    LogMetadata metadata;
    metadata.category = "script";
    metadata.system = "EngineScript";
    metadata.entity = entity;
    if (!source_path.empty()) metadata.source = LogSourceLocation{std::move(source_path), line, column};
    Log::error(m_statuses[entity].message, std::move(metadata));
}

const ScriptExecutionStatus* ScriptRuntime::status(EntityID entity) const {
    const auto found = m_statuses.find(entity);
    return found == m_statuses.end() ? nullptr : &found->second;
}

EngineScript::ComponentReference ScriptRuntime::find_behaviour_component(
    EntityID entity,
    const std::string& type_name,
    std::size_t index
) {
    std::size_t current = 0;
    for (Instance& instance : m_instances) {
        if (instance.entity != entity || instance.class_name != type_name || !instance.behaviour) continue;
        if (current++ != index) continue;
        EngineScript::ComponentReference reference;
        reference.runtime_context = this;
        reference.entity_id = entity;
        reference.slot_id = instance.slot_id;
        reference.generation = instance.generation;
        reference.type_name = type_name;
        reference.resolve_callback = [](void* context, uint32_t target_entity, uint64_t slot_id,
                                         uint32_t generation, const char* requested_type) -> void* {
            auto* runtime = static_cast<ScriptRuntime*>(context);
            if (!runtime || !requested_type) return nullptr;
            for (Instance& candidate : runtime->m_instances) {
                if (candidate.entity == target_entity && candidate.slot_id == slot_id &&
                    candidate.generation == generation && candidate.class_name == requested_type &&
                    candidate.behaviour) {
                    return candidate.behaviour.get();
                }
            }
            return nullptr;
        };
        reference.remove_callback = [](void* context, uint32_t target_entity, uint64_t slot_id,
                                        uint32_t generation, const char* requested_type) {
            auto* runtime = static_cast<ScriptRuntime*>(context);
            if (!runtime || !requested_type) return false;
            EngineScript::ComponentReference pending;
            pending.runtime_context = runtime;
            pending.entity_id = target_entity;
            pending.slot_id = slot_id;
            pending.generation = generation;
            pending.type_name = requested_type;
            return runtime->remove_behaviour_component(pending);
        };
        return reference;
    }
    return {};
}

std::size_t ScriptRuntime::count_behaviour_components(EntityID entity, const std::string& type_name) const {
    return static_cast<std::size_t>(std::count_if(
        m_instances.begin(), m_instances.end(),
        [entity, &type_name](const Instance& instance) {
            return instance.entity == entity && instance.class_name == type_name && instance.behaviour;
        }
    ));
}

EngineScript::ComponentReference ScriptRuntime::add_behaviour_component(
    EntityID entity,
    const std::string& type_name
) {
    if (!m_entity_manager || !m_entity_manager->is_alive(entity) || !m_native_module ||
        !m_native_module->has_behaviour_type(type_name)) {
        ENGINE_LOG_WARN("[SCRIPT] Cannot add unavailable EngineScript component {}", type_name);
        return {};
    }
    const auto& behaviours = m_native_workspace->metadata_manifest().behaviours;
    const auto manifest = std::find_if(behaviours.begin(), behaviours.end(), [&type_name](const auto& behaviour) {
        return behaviour.name == type_name;
    });
    if (manifest == behaviours.end()) {
        ENGINE_LOG_WARN("[SCRIPT] Cannot resolve EngineScript component asset for {}", type_name);
        return {};
    }
    const auto record = std::find_if(m_asset_database->records().begin(), m_asset_database->records().end(),
        [&manifest](const AssetRecord& candidate) {
            return candidate.type == AssetType::Script && candidate.status == AssetStatus::Ready &&
                candidate.source_path == manifest->module_path;
        });
    if (record == m_asset_database->records().end()) {
        ENGINE_LOG_WARN("[SCRIPT] Cannot resolve EngineScript component asset for {}", type_name);
        return {};
    }

    const bool created_container = !m_entity_manager->has_component<ScriptComponent>(entity);
    if (created_container) {
        ScriptComponent scripts;
        scripts.instances.clear();
        m_entity_manager->add_component<ScriptComponent>(entity, std::move(scripts));
    }
    auto& scripts = m_entity_manager->get_component<ScriptComponent>(entity);
    ScriptBehaviourSlot slot;
    slot.script_handle = record->handle;
    slot.runtime_only = true;
    const ScriptSlotID slot_id = slot.id;
    scripts.instances.push_back(std::move(slot));

    ScriptBehaviourSlot* attached = find_script_slot(*m_entity_manager, entity, slot_id);
    std::string error;
    if (!attached || !instantiate_slot(entity, *attached, error)) {
        std::erase_if(scripts.instances, [slot_id](const ScriptBehaviourSlot& candidate) {
            return candidate.id == slot_id;
        });
        if (created_container && scripts.instances.empty()) {
            m_entity_manager->remove_component<ScriptComponent>(entity);
        }
        ENGINE_LOG_WARN("[SCRIPT] Cannot add {}: {}", type_name, error);
        return {};
    }
    return find_behaviour_component(entity, type_name, count_behaviour_components(entity, type_name) - 1);
}

bool ScriptRuntime::remove_behaviour_component(const EngineScript::ComponentReference& reference) {
    const auto found = std::find_if(
        m_instances.begin(), m_instances.end(),
        [&reference](const Instance& instance) {
            return instance.entity == reference.entity_id && instance.slot_id == reference.slot_id &&
                instance.generation == reference.generation && instance.class_name == reference.type_name &&
                instance.behaviour;
        }
    );
    if (found == m_instances.end()) return false;
    const bool already_pending = std::any_of(
        m_pending_component_removals.begin(), m_pending_component_removals.end(),
        [&reference](const EngineScript::ComponentReference& pending) {
            return pending.entity_id == reference.entity_id && pending.slot_id == reference.slot_id &&
                pending.generation == reference.generation;
        }
    );
    if (already_pending) return false;
    m_pending_component_removals.push_back(reference);
    return true;
}

void ScriptRuntime::flush_pending_component_removals() {
    for (const EngineScript::ComponentReference& reference : m_pending_component_removals) {
        const auto found = std::find_if(
            m_instances.begin(), m_instances.end(),
            [&reference](const Instance& instance) {
                return instance.entity == reference.entity_id && instance.slot_id == reference.slot_id &&
                    instance.generation == reference.generation && instance.class_name == reference.type_name;
            }
        );
        if (found == m_instances.end()) continue;
        if (found->behaviour) {
            if (m_native_async_scheduler) m_native_async_scheduler->cancel_scope(found->behaviour->task_scope_id);
            if (m_typed_event_bus) m_typed_event_bus->cancel_scope(found->behaviour->task_scope_id);
            found->behaviour->cancel_owned_tasks();
            found->behaviour->on_destroy();
        }
        if (m_entity_manager) {
            if (ScriptBehaviourSlot* slot = find_script_slot(*m_entity_manager, found->entity, found->slot_id)) {
                auto& scripts = m_entity_manager->get_component<ScriptComponent>(found->entity);
                std::erase_if(scripts.instances, [slot_id = slot->id](const ScriptBehaviourSlot& candidate) {
                    return candidate.id == slot_id;
                });
            }
        }
        m_instances.erase(found);
    }
    m_pending_component_removals.clear();
}

const std::string* ScriptRuntime::reload_error(AssetHandle handle) const {
    const auto found = m_reload_errors.find(handle);
    return found == m_reload_errors.end() ? nullptr : &found->second;
}

} // namespace Engine
