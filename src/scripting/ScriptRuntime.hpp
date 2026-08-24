#pragma once

#include "../core/AssetHandle.hpp"
#include "../ecs/EcsTypes.hpp"
#include "../ecs/components/ScriptingComponents.hpp"
#include "../core/Display.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <memory>
#include <cstdint>

namespace EngineScript {
class EngineScriptWorkspace;
class AsyncScheduler;
class GameModule;
class NativeBehaviour;
}

namespace Engine {

class AssetDatabase;
class EntityManager;
class EventDispatcher;
struct ScriptRuntimeBinding;

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
};

enum class ScriptExecutionState {
    Running,
    Error
};

struct ScriptExecutionStatus {
    ScriptExecutionState state{ScriptExecutionState::Running};
    std::string message;
};

struct ScriptInspectionResult {
    bool success{false};
    std::vector<ScriptPublicField> public_fields;
    std::string error;
};

struct ScriptRuntimeStartResult {
    std::size_t started{0};
    std::size_t failed{0};
};

struct ScriptHotReloadResult {
    std::size_t reloaded{0};
    std::size_t failed{0};
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
    void update(float delta_time);
    void dispatch_events();
    [[nodiscard]] ScriptHotReloadResult reload_changed_scripts(bool force = false);
    void shutdown();

    [[nodiscard]] bool is_running() const { return m_entity_manager != nullptr || !m_instances.empty(); }
    [[nodiscard]] std::size_t instance_count() const { return m_instances.size(); }
    [[nodiscard]] const ScriptExecutionStatus* status(EntityID entity) const;
    [[nodiscard]] const std::string* reload_error(AssetHandle handle) const;

    [[nodiscard]] static ScriptInspectionResult inspect_asset(
        AssetHandle handle,
        const AssetDatabase& asset_database
    );

private:
    struct Instance {
        EntityID entity{0};
        AssetHandle script_handle{};
        std::string script_path;
        std::string class_name;
        std::unique_ptr<ScriptRuntimeBinding> binding;
        std::unique_ptr<EngineScript::NativeBehaviour> behaviour;
        std::vector<ScriptPublicField> field_definitions;
    };

    enum class PendingEventType {
        CollisionEnter,
        CollisionExit,
        ButtonClicked,
        FocusChanged,
        Custom
    };

    struct PendingEvent {
        PendingEventType type{PendingEventType::Custom};
        EntityID entity_a{0};
        EntityID entity_b{0};
        float normal_x{0.0f};
        float normal_y{0.0f};
        bool is_trigger{false};
        std::string name;
        std::string payload;
    };

    void queue_custom_event(EntityID source, std::string name, std::string payload);
    void subscribe_to_events();
    void unsubscribe_from_events();
    void set_error(EntityID entity, std::string message);

    EntityManager* m_entity_manager{nullptr};
    const AssetDatabase* m_asset_database{nullptr};
    EventDispatcher* m_event_dispatcher{nullptr};
    ScriptRuntimeServices m_services;
    std::vector<Instance> m_instances;
    std::unique_ptr<EngineScript::EngineScriptWorkspace> m_native_workspace;
    std::unique_ptr<EngineScript::AsyncScheduler> m_native_async_scheduler;
    std::shared_ptr<EngineScript::GameModule> m_native_module;
    std::unordered_map<EntityID, ScriptExecutionStatus> m_statuses;
    std::unordered_map<AssetHandle, std::optional<std::uint64_t>> m_source_signatures;
    std::unordered_map<AssetHandle, std::string> m_reload_errors;
    std::uint64_t m_next_hot_reload_poll_ms{0};
    std::uint32_t m_next_native_build_number{1};
    std::vector<std::uint64_t> m_event_subscriptions;
    std::vector<PendingEvent> m_pending_events;
};

} // namespace Engine
