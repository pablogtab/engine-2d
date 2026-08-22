#pragma once

#include "../core/AssetHandle.hpp"
#include "../ecs/EcsTypes.hpp"
#include "../ecs/components/ScriptingComponents.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace Engine {

class AssetDatabase;
class EntityManager;

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

class ScriptRuntime {
public:
    ScriptRuntime() = default;
    ~ScriptRuntime();

    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;
    ScriptRuntime(ScriptRuntime&& other) noexcept;
    ScriptRuntime& operator=(ScriptRuntime&& other) noexcept;

    [[nodiscard]] ScriptRuntimeStartResult start(
        EntityManager& entity_manager,
        const AssetDatabase& asset_database
    );
    void update(float delta_time);
    void shutdown();

    [[nodiscard]] bool is_running() const { return m_state != nullptr; }
    [[nodiscard]] std::size_t instance_count() const { return m_instances.size(); }
    [[nodiscard]] const ScriptExecutionStatus* status(EntityID entity) const;

    [[nodiscard]] static ScriptInspectionResult inspect_asset(
        AssetHandle handle,
        const AssetDatabase& asset_database
    );

private:
    struct Instance {
        EntityID entity{0};
        AssetHandle script_handle{};
        int thread_ref{-1};
        int module_ref{-1};
        int context_ref{-1};
        lua_State* thread{nullptr};
        bool created{false};
        std::vector<ScriptPublicField> field_definitions;
        std::vector<ScriptPublicField> authored_fields;
    };

    [[nodiscard]] bool create_instance(
        EntityID entity,
        const ScriptComponent& component,
        const std::string& bytecode,
        const std::string& chunk_name,
        const std::vector<ScriptPublicField>& field_definitions,
        std::string& error
    );
    [[nodiscard]] bool call(Instance& instance, const char* callback, float delta_time, bool pass_delta);
    void synchronize_public_fields(Instance& instance, const ScriptComponent& component);
    void release_instance(Instance& instance);
    void set_error(EntityID entity, std::string message);

    lua_State* m_state{nullptr};
    EntityManager* m_entity_manager{nullptr};
    std::vector<Instance> m_instances;
    std::unordered_map<EntityID, ScriptExecutionStatus> m_statuses;
};

} // namespace Engine
