#include "ScriptRuntime.hpp"

#include "../core/AssetDatabase.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Input.hpp"
#include "../core/Log.hpp"
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
#include "../renderer/SpriteAnimationSystem.hpp"
#include "../events/Event.hpp"
#include "../events/EventDispatcher.hpp"

#include "../enginescript/editor/EngineScriptWorkspace.hpp"
#include "../enginescript/async/AsyncScheduler.hpp"
#include "../enginescript/module/GameModule.hpp"
#include "../enginescript/module/NativeModuleBuilder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <unordered_map>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace Engine {

struct ScriptRuntimeBinding {
    EntityManager* entity_manager{nullptr};
    EventDispatcher* event_dispatcher{nullptr};
    const AssetDatabase* asset_database{nullptr};
    EntityID entity{0};
    const ScriptRuntimeServices* services{nullptr};
    EngineScript::AsyncScheduler* async_scheduler{nullptr};
};

class NativeBehaviourHost {
public:
    explicit NativeBehaviourHost(ScriptRuntimeBinding& binding)
        : m_binding(binding), m_input(binding), m_log(), m_audio(binding), m_scene(binding), m_events(binding), m_assets(binding), m_ui(binding), m_tilemap(binding), m_display(binding), m_physics(binding), m_animator(binding) {}

    bool prepare(const EngineScript::BehaviourTypeRegistration& registration, std::string& error) {
        for (const auto& [type, kind] : registration.constructor_dependencies) {
            if (type == "Input") m_arguments.push_back(&m_input);
            else if (type == "Log") m_arguments.push_back(&m_log);
            else if (type == "Audio") m_arguments.push_back(&m_audio);
            else if (type == "Scene") m_arguments.push_back(&m_scene);
            else if (type == "Events") m_arguments.push_back(&m_events);
            else if (type == "Assets") m_arguments.push_back(&m_assets);
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
            else if (kind == EngineScript::NativeDependencyKind::OptionalComponent) m_arguments.push_back(nullptr);
            else { error = "unsupported native EngineScript dependency: " + type; return false; }
        }
        return true;
    }

    void sync_before() {
        if (!m_binding.entity_manager) return;
        EntityManager& entities = *m_binding.entity_manager;
        if (entities.has_component<RigidBodyComponent>(m_binding.entity)) {
            m_rigid_body.velocity = entities.get_component<RigidBodyComponent>(m_binding.entity).velocity;
            m_rigid_body.isGrounded = PhysicsSystem::is_grounded(entities, m_binding.entity);
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
            transform.position = glm::vec3(m_transform.position, transform.position.z);
            transform.rotation = m_transform.rotation;
            transform.scale = m_transform.scale;
        }
        m_entities.sync_after();
    }

    void** arguments() { return m_arguments.empty() ? nullptr : m_arguments.data(); }
    [[nodiscard]] EngineScript::Entities* entities() { return &m_entities; }
    [[nodiscard]] bool is_subscribed_to(const std::string& name) const { return m_events.is_subscribed_to(name); }

private:
    class Input final : public EngineScript::Input {
    public:
        explicit Input(const ScriptRuntimeBinding&) {}
        float axis(const std::string& name) const override { return Engine::Input::get_axis(name); }
        bool pressed(const std::string& action) const override { return Engine::Input::is_action_just_pressed(action); }
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
    class Events final : public EngineScript::Events {
    public:
        explicit Events(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        bool emit(const std::string& name, const std::string& payload) override {
            if (!m_binding.event_dispatcher || name.empty()) return false;
            m_binding.event_dispatcher->queue(ScriptEvent{m_binding.entity, name, payload});
            return true;
        }
        bool subscribe(const std::string& name) override {
            if (name.empty()) return false;
            m_subscriptions.insert(name);
            return true;
        }
        [[nodiscard]] bool is_subscribed_to(const std::string& name) const {
            return m_subscriptions.contains(name);
        }
    private:
        const ScriptRuntimeBinding& m_binding;
        std::unordered_set<std::string> m_subscriptions;
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
        bool request(const std::string& path) override {
            const AssetRecord* record = record_for(path);
            if (!record || record->status != AssetStatus::Ready || !m_binding.async_scheduler || !m_binding.event_dispatcher) return false;
            const AssetHandle handle = record->handle;
            const std::string source_path = record->source_path;
            const EntityID entity = m_binding.entity;
            const AssetDatabase* database = m_binding.asset_database;
            EventDispatcher* dispatcher = m_binding.event_dispatcher;
            const ScriptRuntimeServices* services = m_binding.services;
            m_binding.async_scheduler->schedule_next_frame([database, dispatcher, services, entity, handle, source_path]() {
                std::string error;
                const bool available = database && dispatcher && services && services->preload_asset &&
                    services->preload_asset(handle, error);
                dispatcher->queue(ScriptEvent{
                    entity,
                    available ? "AssetLoaded" : "AssetLoadFailed",
                    available ? source_path : source_path + ": " + error
                });
            }, entity);
            return true;
        }
        EngineScript::Task<EngineScript::Asset*> load_resource(const std::string& path, EngineScript::ResourceType type) override {
            const AssetRecord* record = record_for(path);
            if (!record || record->status != AssetStatus::Ready || !matches(type, record->type) ||
                !m_binding.async_scheduler || !m_binding.services || !m_binding.services->preload_asset) co_return nullptr;
            const AssetHandle handle = record->handle;
            const std::string source_path = record->source_path;
            co_await m_binding.async_scheduler->next_frame(m_binding.entity);
            std::string error;
            if (!m_binding.services->preload_asset(handle, error)) co_return nullptr;
            m_asset = EngineScript::Asset{handle.value, source_path, AssetDatabase::type_name(record->type), "ready", true};
            co_return &m_asset;
        }
    private:
        static bool matches(EngineScript::ResourceType requested, AssetType actual) {
            switch (requested) {
                case EngineScript::ResourceType::Texture: return actual == AssetType::Texture;
                case EngineScript::ResourceType::AudioClip: return actual == AssetType::Audio;
                case EngineScript::ResourceType::Font: return actual == AssetType::Font;
                case EngineScript::ResourceType::DataAsset: return actual == AssetType::Data;
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
        bool alive(EntityID id) const { return m_binding.entity_manager && m_binding.entity_manager->is_alive(id); }
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
            entity.runtime_context = m_binding.entity_manager;
            entity.destroy_callback = [](void* context, uint32_t entity_id) {
                auto* entities = static_cast<EntityManager*>(context);
                if (entities && entities->is_alive(entity_id)) entities->destroy_entity(entity_id);
            };
            return entity;
        }
        const ScriptRuntimeBinding& m_binding;
        EngineScript::RaycastHit m_hit;
    };
    class Animator final : public EngineScript::Animator2D {
    public:
        explicit Animator(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        void play(const std::string& clip) override { if (m_binding.entity_manager) SpriteAnimationSystem::play_clip(*m_binding.entity_manager, m_binding.entity, clip); }
    private: const ScriptRuntimeBinding& m_binding; };
    class EntityService final : public EngineScript::Entities {
    public:
        explicit EntityService(const ScriptRuntimeBinding& binding) : m_binding(binding) {}
        EngineScript::Entity create(const std::string& name) override {
            if (!m_binding.entity_manager) return {};
            EntityManager& entities = *m_binding.entity_manager;
            const EntityID id = entities.create_entity();
            entities.add_component<TagComponent>(id, TagComponent{name});
            entities.add_component<TransformComponent>(id, TransformComponent{});
            return make_entity(id);
        }
        EngineScript::Entity findByName(const std::string& name) const override { return find(name); }
        EngineScript::Entity findByTag(const std::string& tag) const override { return find(tag); }
        bool exists(const EngineScript::Entity& entity) const override {
            return m_binding.entity_manager && m_binding.entity_manager->is_alive(entity.id);
        }
        std::vector<EngineScript::Entity> findAllByTag(const std::string& tag) const override {
            return find_all([&tag](const TagComponent& value) { return value.tag == tag; });
        }
        std::vector<EngineScript::Entity> findAllWithTransform() const override {
            std::vector<EngineScript::Entity> results;
            if (!m_binding.entity_manager) return results;
            m_binding.entity_manager->for_each<TransformComponent>([&](EntityID id, const TransformComponent&) {
                results.push_back(make_entity(id));
            });
            return results;
        }
        bool hasTransform(const EngineScript::Entity& entity) const override {
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
            m_particle.value.emitRate = source.emit_rate;
            m_particle.value.burstCount = source.burst_count;
            m_particle.value.active = source.active;
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
            if (!m_binding.entity_manager) return;
            if (m_transform.id != 0 && alive_with<TransformComponent>(m_transform.id)) {
                TransformComponent& target = m_binding.entity_manager->get_component<TransformComponent>(m_transform.id);
                target.position = glm::vec3(m_transform.value.position, target.position.z);
                target.rotation = m_transform.value.rotation;
                target.scale = m_transform.value.scale;
            }
            if (m_rigid_body.id != 0 && alive_with<RigidBodyComponent>(m_rigid_body.id)) {
                PhysicsSystem::set_velocity(*m_binding.entity_manager, m_rigid_body.id, m_rigid_body.value.velocity);
            }
            if (m_sprite_renderer.id != 0 && alive_with<SpriteComponent>(m_sprite_renderer.id)) {
                m_binding.entity_manager->get_component<SpriteComponent>(m_sprite_renderer.id).flip_x = m_sprite_renderer.value.flipX;
            }
            if (m_particle.id != 0 && alive_with<ParticleEmitter2DComponent>(m_particle.id)) {
                ParticleEmitter2DComponent& target = m_binding.entity_manager->get_component<ParticleEmitter2DComponent>(m_particle.id);
                target.emit_rate = m_particle.value.emitRate;
                target.burst_count = m_particle.value.burstCount;
                target.active = m_particle.value.active;
            }
            if (m_joint.id != 0 && alive_with<JointComponent>(m_joint.id)) {
                JointComponent& target = m_binding.entity_manager->get_component<JointComponent>(m_joint.id);
                target.target_entity = m_joint.value.target.id;
                target.collide_connected = m_joint.value.collideConnected;
            }
        }
    private:
        template <typename Predicate>
        std::vector<EngineScript::Entity> find_all(Predicate predicate) const {
            std::vector<EngineScript::Entity> results;
            if (!m_binding.entity_manager) return results;
            m_binding.entity_manager->for_each<TagComponent>([&](EntityID id, const TagComponent& value) {
                if (predicate(value)) results.push_back(make_entity(id));
            });
            return results;
        }
        template <typename Component>
        bool alive_with(EntityID id) const {
            return m_binding.entity_manager && m_binding.entity_manager->is_alive(id) &&
                m_binding.entity_manager->has_component<Component>(id);
        }
        EngineScript::Entity find(const std::string& value) const {
            if (!m_binding.entity_manager) return {};
            EntityID result = 0;
            m_binding.entity_manager->for_each<TagComponent>([&](EntityID id, const TagComponent& tag) {
                if (result == 0 && tag.tag == value) result = id;
            });
            return make_entity(result);
        }
        EngineScript::Entity make_entity(EntityID id) const {
            EngineScript::Entity entity;
            entity.id = id;
            if (!m_binding.entity_manager || !m_binding.entity_manager->is_alive(id)) return entity;
            if (m_binding.entity_manager->has_component<TagComponent>(id)) {
                entity.name = m_binding.entity_manager->get_component<TagComponent>(id).tag;
                entity.tag = entity.name;
            }
            entity.runtime_context = m_binding.entity_manager;
            entity.destroy_callback = [](void* context, uint32_t entity_id) {
                auto* entities = static_cast<EntityManager*>(context);
                if (entities && entities->is_alive(entity_id)) entities->destroy_entity(entity_id);
            };
            return entity;
        }
        const ScriptRuntimeBinding& m_binding;
        struct { EntityID id{0}; EngineScript::Transform2D value; } m_transform;
        struct { EntityID id{0}; EngineScript::RigidBody2D value; } m_rigid_body;
        struct { EntityID id{0}; EngineScript::SpriteRenderer2D value; } m_sprite_renderer;
        struct { EntityID id{0}; EngineScript::ParticleEmitter2D value; } m_particle;
        struct { EntityID id{0}; EngineScript::Joint2D value; } m_joint;
    };

    ScriptRuntimeBinding& m_binding;
    Input m_input;
    Log m_log;
    Audio m_audio;
    Scene m_scene;
    Events m_events;
    Assets m_assets;
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

using RuntimeValue = std::variant<
    std::monostate,
    double,
    bool,
    std::string,
    EntityID,
    EngineScript::Entity,
    RuntimeObjectRef
>;
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

    bool set_exposed_property(
        const std::string& name,
        const ExposedPropertyValue& value
    ) override {
        ScriptComponent* component = script_component();
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
        const ScriptComponent* component = script_component();
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
            if (entities && entities->is_alive(entity_id)) entities->destroy_entity(entity_id);
        };
        return entity;
    }

    ScriptComponent* script_component() const {
        if (!m_binding || !m_binding->entity_manager ||
            !m_binding->entity_manager->is_alive(m_binding->entity) ||
            !m_binding->entity_manager->has_component<ScriptComponent>(m_binding->entity)) {
            return nullptr;
        }
        return &m_binding->entity_manager->get_component<ScriptComponent>(m_binding->entity);
    }

    RuntimeValue read_property(const std::string& name) const {
        const ScriptComponent* component = script_component();
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
        ScriptComponent* component = script_component();
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
                    m_binding->entity_manager->destroy_entity(*entity_id);
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

    bool execute(const EngineScript::IROperation* operation, RuntimeEnvironment& environment) const {
        using namespace EngineScript;
        if (!operation) return false;
        if (const auto* ret = dynamic_cast<const IRReturn*>(operation)) {
            return true;
        }
        if (const auto* declaration = dynamic_cast<const IRVarDecl*>(operation)) {
            environment[declaration->name] = evaluate(declaration->initializer.get(), environment);
            return false;
        }
        if (const auto* assignment = dynamic_cast<const IRAssign*>(operation)) {
            RuntimeValue value = evaluate(assignment->value.get(), environment);
            auto local = environment.find(assignment->target_name);
            RuntimeValue current = local != environment.end() ? local->second : read_property(assignment->target_name);
            if (assignment->assignment_op == TokenType::PlusEquals) value = runtime_number(current) + runtime_number(value);
            else if (assignment->assignment_op == TokenType::MinusEquals) value = runtime_number(current) - runtime_number(value);
            if (local != environment.end()) local->second = value;
            else write_property(assignment->target_name, value);
            return false;
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
            return false;
        }
        if (const auto* branch = dynamic_cast<const IRIf*>(operation)) {
            const auto& selected = runtime_boolean(evaluate(branch->condition.get(), environment))
                ? branch->then_operations
                : branch->else_operations;
            for (const auto& nested : selected) {
                if (execute(nested.get(), environment)) return true;
            }
            return false;
        }
        static_cast<void>(evaluate(operation, environment));
        return false;
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
                if (execute(statement.op.get(), environment)) return;
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
            field.value = ScriptVector2{0.0f, 0.0f};
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

EngineScript::Entity script_entity(EntityManager& entities, EntityID id) {
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
            runtime_entities->destroy_entity(entity_id);
        }
    };
    return result;
}

bool push_exposed_property(
    EngineScript::NativeBehaviour& behaviour,
    const ScriptPublicField& field,
    EntityManager& entities
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
                return behaviour.set_exposed_property(field.name, script_entity(entities, *value));
            }
            break;
        case ScriptFieldType::Asset:
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
    } else if (field.type == ScriptFieldType::Entity) {
        if (const auto* typed = std::get_if<EngineScript::Entity>(&value)) field.value = typed->id;
    }
}

void push_exposed_properties(
    EngineScript::NativeBehaviour& behaviour,
    const ScriptComponent& component,
    EntityManager& entities
) {
    for (const ScriptPublicField& field : component.public_fields) {
        static_cast<void>(push_exposed_property(behaviour, field, entities));
    }
}

void pull_exposed_properties(
    const EngineScript::NativeBehaviour& behaviour,
    ScriptComponent& component
) {
    for (ScriptPublicField& field : component.public_fields) {
        pull_exposed_property(behaviour, field);
    }
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
    m_native_module = std::move(other.m_native_module);
    m_statuses = std::move(other.m_statuses);
    m_source_signatures = std::move(other.m_source_signatures);
    m_reload_errors = std::move(other.m_reload_errors);
    m_next_hot_reload_poll_ms = other.m_next_hot_reload_poll_ms;
    m_pending_events = std::move(other.m_pending_events);

    other.m_entity_manager = nullptr;
    other.m_asset_database = nullptr;
    other.m_services = {};
    other.m_instances.clear();
    other.m_native_workspace.reset();
    other.m_native_async_scheduler.reset();
    other.m_native_module.reset();
    other.m_statuses.clear();
    other.m_source_signatures.clear();
    other.m_reload_errors.clear();
    other.m_next_hot_reload_poll_ms = 0;
    other.m_pending_events.clear();

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
        entity_manager.for_each<ScriptComponent>([&](EntityID entity, const ScriptComponent& component) {
            if (!component.enabled) return;
            ++result.failed;
            set_error(entity, error);
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
            if (!component.enabled) return;
            ++result.failed;
            set_error(entity, error);
        });
        return result;
    }

    std::unordered_map<std::string, std::string> definitions_by_path;
    std::unordered_set<std::string> invalid_behaviour_modules;
    for (const auto& module : m_native_workspace->ir_modules()) {
        for (const auto& definition : module.classes) {
            if (!definition.is_behaviour) continue;
            const std::string registered_type = definition.name;
            if (!definitions_by_path.emplace(
                    module.module_path,
                    registered_type
                ).second) {
                invalid_behaviour_modules.insert(module.module_path);
            }
        }
    }
    m_native_module = std::move(generated_native_module);

    entity_manager.for_each<ScriptComponent>([&](EntityID entity, const ScriptComponent& component) {
        if (!component.enabled) return;
        const AssetRecord* record = asset_database.find(component.script_handle);
        if (!record || record->type != AssetType::Script || record->status != AssetStatus::Ready) {
            ++result.failed;
            set_error(entity, "script asset is missing or unavailable");
            return;
        }

        const auto definition = definitions_by_path.find(record->source_path);
        const auto manifest = std::find_if(
            m_native_workspace->metadata_manifest().behaviours.begin(),
            m_native_workspace->metadata_manifest().behaviours.end(),
            [record](const EngineScript::BehaviourManifest& behaviour) {
                return behaviour.module_path == record->source_path;
            }
        );
        if (definition == definitions_by_path.end() ||
            manifest == m_native_workspace->metadata_manifest().behaviours.end() ||
            invalid_behaviour_modules.contains(record->source_path)) {
            ++result.failed;
            set_error(entity, "script asset must export exactly one @behaviour class");
            return;
        }

        for (const auto& dependency : manifest->dependencies) {
            if (dependency.is_optional) continue;
            if (dependency.is_component && dependency.type == "RigidBody2D" &&
                !entity_manager.has_component<RigidBodyComponent>(entity)) {
                ++result.failed;
                set_error(entity, "missing required RigidBody2D component");
                return;
            }
            if (dependency.is_component && dependency.type == "Animator2D" &&
                !entity_manager.has_component<AnimationComponent>(entity)) {
                ++result.failed;
                set_error(entity, "missing required Animator2D component");
                return;
            }
            if (dependency.is_component && dependency.type == "SpriteRenderer2D" &&
                !entity_manager.has_component<SpriteComponent>(entity)) {
                ++result.failed;
                set_error(entity, "missing required SpriteRenderer2D component");
                return;
            }
            if (dependency.is_component && dependency.type == "Transform2D" &&
                !entity_manager.has_component<TransformComponent>(entity)) {
                ++result.failed;
                set_error(entity, "missing required Transform2D component");
                return;
            }
            if (dependency.is_service && dependency.type != "Input" && dependency.type != "Log" &&
                dependency.type != "Audio" && dependency.type != "Scene" && dependency.type != "Time" &&
                dependency.type != "Entities" && dependency.type != "Display" && dependency.type != "Physics" &&
                dependency.type != "Events" && dependency.type != "Assets" && dependency.type != "UI" &&
                dependency.type != "Tilemap") {
                ++result.failed;
                set_error(entity, "unsupported injected service: " + dependency.type);
                return;
            }
        }

        std::vector<ScriptPublicField> compiled_fields = public_fields_from_behaviour(*manifest);
        auto& mutable_component = entity_manager.get_component<ScriptComponent>(entity);
        for (const ScriptPublicField& default_field : compiled_fields) {
            const auto existing = std::find_if(
                mutable_component.public_fields.begin(),
                mutable_component.public_fields.end(),
                [&default_field](const ScriptPublicField& field) {
                    return field.name == default_field.name && field.type == default_field.type;
                }
            );
            if (existing == mutable_component.public_fields.end()) {
                mutable_component.public_fields.push_back(default_field);
            }
        }

        Instance instance;
        instance.entity = entity;
        instance.script_handle = component.script_handle;
        instance.script_path = record->source_path;
        instance.class_name = definition->second;
        instance.field_definitions = std::move(compiled_fields);
        instance.binding = std::make_unique<ScriptRuntimeBinding>();
        instance.binding->entity_manager = &entity_manager;
        instance.binding->event_dispatcher = event_dispatcher;
        instance.binding->asset_database = &asset_database;
        instance.binding->entity = entity;
        instance.binding->services = &m_services;
        instance.binding->async_scheduler = m_native_async_scheduler.get();
        void** factory_arguments = nullptr;
        {
            const auto registration = std::find_if(
                m_native_module->descriptor().behaviour_types.begin(),
                m_native_module->descriptor().behaviour_types.end(),
                [&instance](const EngineScript::BehaviourTypeRegistration& candidate) {
                    return candidate.type_name == instance.class_name;
                }
            );
            if (registration == m_native_module->descriptor().behaviour_types.end()) {
                ++result.failed;
                set_error(entity, "native EngineScript behaviour registration is missing");
                return;
            }
            instance.native_host = std::make_unique<NativeBehaviourHost>(*instance.binding);
            std::string host_error;
            if (!instance.native_host->prepare(*registration, host_error)) {
                ++result.failed;
                set_error(entity, host_error);
                return;
            }
            instance.native_host->sync_before();
            factory_arguments = instance.native_host->arguments();
        }
        instance.behaviour.reset(m_native_module->create_behaviour(
            instance.class_name,
            factory_arguments
        ));
        if (!instance.behaviour) {
            ++result.failed;
            set_error(entity, "could not instantiate EngineScript behaviour " + instance.class_name);
            return;
        }
        instance.behaviour->entity_id = entity;
        instance.behaviour->task_scope_id = entity;
        instance.behaviour->async_scheduler = m_native_async_scheduler.get();
        instance.behaviour->entities = instance.native_host ? instance.native_host->entities() : nullptr;
        push_exposed_properties(*instance.behaviour, mutable_component, entity_manager);
        instance.behaviour->on_start();
        if (instance.native_host) instance.native_host->sync_after();
        pull_exposed_properties(*instance.behaviour, mutable_component);
        m_instances.push_back(std::move(instance));
        ++result.started;
    });

    return result;
}

void ScriptRuntime::queue_custom_event(EntityID source, std::string name, std::string payload) {
    PendingEvent event;
    event.type = PendingEventType::Custom;
    event.entity_a = source;
    event.name = std::move(name);
    event.payload = std::move(payload);
    m_pending_events.push_back(std::move(event));
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
    m_event_subscriptions.push_back(
        m_event_dispatcher->subscribe<ScriptEvent>([this](const ScriptEvent& event) {
            queue_custom_event(event.source, event.name, event.payload);
        })
    );
    m_event_subscriptions.push_back(
        m_event_dispatcher->subscribe<GameSessionEvent>([this](const GameSessionEvent& event) {
            queue_custom_event(0, event.name, event.error.empty() ? event.path : event.error);
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
        if (event.type == PendingEventType::CollisionEnter) {
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
                        if (entities && entities->is_alive(id)) entities->destroy_entity(id);
                    };
                    instance.behaviour->on_collision_enter(other);
                    if (m_entity_manager->is_alive(instance.entity) &&
                        m_entity_manager->has_component<ScriptComponent>(instance.entity)) {
                        pull_exposed_properties(
                            *instance.behaviour,
                            m_entity_manager->get_component<ScriptComponent>(instance.entity)
                        );
                    }
                }
            }
        }
        if (event.type == PendingEventType::Custom) {
            for (auto& instance : m_instances) {
                if (!instance.behaviour || !instance.native_host || !instance.native_host->is_subscribed_to(event.name)) continue;
                instance.native_host->sync_before();
                instance.behaviour->on_event(event.name, event.payload);
                instance.native_host->sync_after();
                if (m_entity_manager->is_alive(instance.entity) &&
                    m_entity_manager->has_component<ScriptComponent>(instance.entity)) {
                    pull_exposed_properties(
                        *instance.behaviour,
                        m_entity_manager->get_component<ScriptComponent>(instance.entity)
                    );
                }
            }
        }
    }

    m_pending_events.clear();
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
        if (component.enabled && component.script_handle.is_valid()) checked_assets.insert(component.script_handle);
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
            if (!m_entity_manager || !m_entity_manager->is_alive(inst.entity) ||
                !m_entity_manager->has_component<ScriptComponent>(inst.entity) || !inst.behaviour) continue;
            const auto& component = m_entity_manager->get_component<ScriptComponent>(inst.entity);
            if (!component.enabled) continue;
            if (inst.native_host) inst.native_host->sync_before();
            push_exposed_properties(*inst.behaviour, component, *m_entity_manager);
        }
        if (m_native_async_scheduler) {
            m_native_async_scheduler->step(delta_time, delta_time);
        }
        for (auto& inst : m_instances) {
            if (!m_entity_manager || !m_entity_manager->is_alive(inst.entity) ||
                !m_entity_manager->has_component<ScriptComponent>(inst.entity) || !inst.behaviour) continue;
            if (inst.native_host) inst.native_host->sync_after();
            pull_exposed_properties(
                *inst.behaviour,
                m_entity_manager->get_component<ScriptComponent>(inst.entity)
            );
        }
    }
    for (auto& inst : m_instances) {
        if (inst.behaviour) inst.behaviour->reap_completed_tasks();
        const auto status_found = m_statuses.find(inst.entity);
        if (status_found != m_statuses.end() && status_found->second.state == ScriptExecutionState::Error) continue;
        if (!m_entity_manager || !m_entity_manager->is_alive(inst.entity) ||
            !m_entity_manager->has_component<ScriptComponent>(inst.entity)) continue;
        const auto& component = m_entity_manager->get_component<ScriptComponent>(inst.entity);
        if (component.enabled && inst.behaviour) {
            if (inst.native_host) inst.native_host->sync_before();
            push_exposed_properties(*inst.behaviour, component, *m_entity_manager);
            inst.behaviour->update(delta_time);
            if (inst.native_host) inst.native_host->sync_after();
            if (m_entity_manager->is_alive(inst.entity) &&
                m_entity_manager->has_component<ScriptComponent>(inst.entity)) {
                pull_exposed_properties(
                    *inst.behaviour,
                    m_entity_manager->get_component<ScriptComponent>(inst.entity)
                );
            }
        }
    }
}

void ScriptRuntime::shutdown() {
    unsubscribe_from_events();
    for (auto& inst : m_instances) {
        if (inst.behaviour) {
            if (m_native_async_scheduler) m_native_async_scheduler->cancel_scope(inst.behaviour->task_scope_id);
            inst.behaviour->cancel_owned_tasks();
            inst.behaviour->on_destroy();
        }
    }
    m_instances.clear();
    m_native_module.reset();
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

void ScriptRuntime::set_error(EntityID entity, std::string message) {
    m_statuses[entity] = {ScriptExecutionState::Error, std::move(message)};
    ENGINE_LOG_ERROR("[SCRIPT] Entity {}: {}", entity, m_statuses[entity].message);
}

const ScriptExecutionStatus* ScriptRuntime::status(EntityID entity) const {
    const auto found = m_statuses.find(entity);
    return found == m_statuses.end() ? nullptr : &found->second;
}

const std::string* ScriptRuntime::reload_error(AssetHandle handle) const {
    const auto found = m_reload_errors.find(handle);
    return found == m_reload_errors.end() ? nullptr : &found->second;
}

} // namespace Engine
