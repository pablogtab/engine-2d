#pragma once

#include "../core/AssetHandle.hpp"
#include "../ecs/EcsTypes.hpp"
#include "../ecs/components/ScriptingComponents.hpp"
#include "../core/Display.hpp"
#include "../enginescript/module/GameModule.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <optional>
#include <memory>
#include <list>
#include <cstdint>
#include <glm/glm.hpp>

namespace EngineScript {
class EngineScriptWorkspace;
class AsyncScheduler;
class TypedEventBus;
class GameModule;
class NativeBehaviour;
struct ParticleEmitOptions;
struct FloatingTextOptions;
}

namespace Engine {

class AssetDatabase;
class EntityManager;
class EventDispatcher;
struct ScriptRuntimeBinding;
class NativeBehaviourHost;

struct ScriptDisplayInfo {
    int pixel_width{0};
    int pixel_height{0};
    float dpi_scale{1.0f};
    SafeAreaInsets safe_area;
    WindowMode window_mode{WindowMode::Windowed};
    bool vsync{true};
    int reference_width{0};
    int reference_height{0};
};

struct ScriptDisplayRequest {
    std::optional<WindowMode> window_mode;
    std::optional<bool> vsync;
};

struct ScriptRuntimeServices {
    bool hot_reload_enabled{false};
    bool async_native_build{false};
    std::function<bool(const std::string&, float, float, std::string&)> play_sound;
    std::function<bool(const std::string&, float, bool, std::string&)> play_music;
    std::function<void()> stop_music;
    std::function<void()> pause_music;
    std::function<void()> resume_music;
    std::function<void(float)> set_master_volume;
    std::function<void(float)> set_music_volume;
    std::function<void(float)> set_sfx_volume;
    std::function<float()> get_master_volume;
    std::function<float()> get_music_volume;
    std::function<float()> get_sfx_volume;
    std::function<ScriptDisplayInfo()> query_display;
    std::function<bool(const ScriptDisplayRequest&, std::string&)> apply_display;
    std::function<bool(const std::string&, std::string&)> request_scene_load;
    std::function<std::string()> current_scene_name;
    std::function<bool()> is_scene_loading;
    std::function<std::string()> scene_load_error;
    std::function<EntityID(AssetHandle, const glm::vec2&)> instantiate_prefab;
    std::function<EntityID(AssetHandle)> create_prefab;
    std::function<bool(EntityID, const glm::vec2&)> spawn_prefab;
    std::function<bool(AssetHandle, std::string&)> preload_asset;
    std::function<bool(AssetHandle, const glm::vec2&, int)> emit_particles;
    std::function<bool(AssetHandle, const glm::vec2&, const EngineScript::ParticleEmitOptions&)> emit_particles_with_options;
    std::function<bool(const std::string&, const glm::vec2&, const EngineScript::FloatingTextOptions&)> show_world_text;
    std::function<bool(EntityID)> play_particle_emitter;
    std::function<bool(EntityID, bool)> stop_particle_emitter;
    std::function<bool(EntityID, int)> burst_particle_emitter;
    std::string native_module_path;
};

enum class ScriptExecutionState {
    Running,
    Error
};

struct ScriptExecutionStatus {
    ScriptExecutionState state{ScriptExecutionState::Running};
    std::string message;
    std::string source_path;
    uint32_t line{0};
    uint32_t column{0};

    [[nodiscard]] bool has_source_location() const { return !source_path.empty() && line != 0; }
};

struct ScriptInspectionResult {
    bool success{false};
    std::vector<ScriptPublicField> public_fields;
    std::string error;
};

struct ScriptRuntimeStartResult {
    std::size_t started{0};
    std::size_t failed{0};
    bool pending{false};
    bool native_cache_hit{false};
};

struct ScriptHotReloadResult {
    std::size_t reloaded{0};
    std::size_t failed{0};
};

enum class ScriptNativeBuildPhase {
    None,
    CheckingCache,
    GeneratingSources,
    CompilingNative
};

class ScriptRuntime {
public:
    ScriptRuntime();
    ~ScriptRuntime();

    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;
    ScriptRuntime(ScriptRuntime&& other) noexcept;
    ScriptRuntime& operator=(ScriptRuntime&& other) noexcept;

    [[nodiscard]] ScriptRuntimeStartResult start(
        EntityManager& entity_manager,
        const AssetDatabase& asset_database,
        EventDispatcher* event_dispatcher = nullptr,
        ScriptRuntimeServices services = {}
    );
    [[nodiscard]] ScriptRuntimeStartResult poll_start();
    [[nodiscard]] bool is_starting() const;
    [[nodiscard]] float startup_elapsed_seconds() const;
    [[nodiscard]] ScriptNativeBuildPhase native_build_phase() const;
    bool prepare_entity(EntityID entity);
    bool activate_entity(EntityID entity);
    // Activates scripts after the current update pass, avoiding mutation while iterating them.
    void queue_entity_start(EntityID entity);
    void update(float delta_time);
    void dispatch_events();
    [[nodiscard]] ScriptHotReloadResult reload_changed_scripts(bool force = false);
    void shutdown();

    [[nodiscard]] bool is_running() const { return m_entity_manager != nullptr || !m_instances.empty(); }
    [[nodiscard]] std::size_t instance_count() const { return m_instances.size(); }
    [[nodiscard]] const ScriptExecutionStatus* status(EntityID entity) const;
    [[nodiscard]] const std::string* reload_error(AssetHandle handle) const;

    [[nodiscard]] EngineScript::ComponentReference find_behaviour_component(
        EntityID entity,
        const std::string& type_name,
        std::size_t index
    );
    [[nodiscard]] std::size_t count_behaviour_components(EntityID entity, const std::string& type_name) const;
    [[nodiscard]] EngineScript::ComponentReference add_behaviour_component(EntityID entity, const std::string& type_name);
    bool remove_behaviour_component(const EngineScript::ComponentReference& reference);

    [[nodiscard]] static ScriptInspectionResult inspect_asset(
        AssetHandle handle,
        const AssetDatabase& asset_database
    );

private:
    struct Instance {
        EntityID entity{0};
        ScriptSlotID slot_id{0};
        std::uint32_t generation{1};
        std::uint64_t eligible_phase{0};
        bool started{false};
        AssetHandle script_handle{};
        std::string script_path;
        std::string class_name;
        std::unique_ptr<ScriptRuntimeBinding> binding;
        std::unique_ptr<NativeBehaviourHost> native_host;
        std::unique_ptr<EngineScript::NativeBehaviour> behaviour;
        std::vector<ScriptPublicField> field_definitions;
    };

    enum class PendingEventType {
        CollisionEnter,
        CollisionExit,
        ButtonClicked,
        FocusChanged
    };

    struct PendingEvent {
        PendingEventType type{PendingEventType::CollisionEnter};
        EntityID entity_a{0};
        EntityID entity_b{0};
        float normal_x{0.0f};
        float normal_y{0.0f};
        bool is_trigger{false};
    };

    void subscribe_to_events();
    void unsubscribe_from_events();
    void set_error(EntityID entity, std::string message, std::string source_path = {}, uint32_t line = 0, uint32_t column = 0);
    [[nodiscard]] ScriptRuntimeStartResult finish_start(
        std::shared_ptr<EngineScript::GameModule> module,
        std::string native_build_error,
        bool native_cache_hit
    );
    bool instantiate_slot(EntityID entity, ScriptBehaviourSlot& slot, std::string& error, bool start_lifecycle = true);
    void discard_entity_instances(EntityID entity);
    void flush_pending_component_removals();

    EntityManager* m_entity_manager{nullptr};
    const AssetDatabase* m_asset_database{nullptr};
    EventDispatcher* m_event_dispatcher{nullptr};
    ScriptRuntimeServices m_services;
    std::list<Instance> m_instances;
    std::vector<EntityID> m_pending_entity_starts;
    std::unique_ptr<EngineScript::EngineScriptWorkspace> m_native_workspace;
    std::unique_ptr<EngineScript::AsyncScheduler> m_native_async_scheduler;
    std::unique_ptr<EngineScript::TypedEventBus> m_typed_event_bus;
    std::shared_ptr<EngineScript::GameModule> m_native_module;
    struct PendingNativeBuild;
    std::unique_ptr<PendingNativeBuild> m_pending_native_build;
    std::unordered_map<EntityID, ScriptExecutionStatus> m_statuses;
    std::unordered_map<AssetHandle, std::optional<std::uint64_t>> m_source_signatures;
    std::unordered_map<AssetHandle, std::string> m_reload_errors;
    std::uint64_t m_next_hot_reload_poll_ms{0};
    std::uint32_t m_next_native_build_number{1};
    std::vector<std::uint64_t> m_event_subscriptions;
    std::vector<PendingEvent> m_pending_events;
    std::vector<EngineScript::ComponentReference> m_pending_component_removals;
    std::unordered_set<ScriptSlotID> m_instantiating_slots;
    std::uint64_t m_phase_sequence{0};
    bool m_phase_active{false};
};

} // namespace Engine
