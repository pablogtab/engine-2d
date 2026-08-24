#pragma once

#include "../async/AsyncScheduler.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace EngineScript {

constexpr uint32_t ENGINE_SCRIPT_ABI_VERSION = 1;
constexpr const char* ENGINE_SCRIPT_SDK_HASH = "engine2d_abi_v1";

class Input {
public:
    virtual ~Input() = default;
    [[nodiscard]] virtual float axis(const std::string& name) const = 0;
    [[nodiscard]] virtual bool pressed(const std::string& action) const = 0;
};

struct RigidBody2D {
    glm::vec2 velocity{0.0f, 0.0f};
    bool isGrounded{false};
};

class Animator2D {
public:
    virtual ~Animator2D() = default;
    virtual void play(const std::string& clip_name) = 0;
};

class NativeBehaviour {
public:
    virtual ~NativeBehaviour() = default;
    virtual void update(float delta_time) {}
    virtual void on_start() {}
    virtual void on_destroy() {}
    virtual void on_collision_enter(uint32_t other_entity) { static_cast<void>(other_entity); }

    [[nodiscard]] AsyncScheduler::DelayAwaiter delay(float seconds) const {
        return async_scheduler
            ? async_scheduler->delay(seconds, task_scope_id)
            : AsyncScheduler::DelayAwaiter{nullptr, seconds, task_scope_id, false};
    }

    uint32_t entity_id = 0;
    uint32_t task_scope_id = 0;
    AsyncScheduler* async_scheduler = nullptr;
};

using BehaviourFactoryFn = std::function<void*(void** args)>;

struct BehaviourTypeRegistration {
    std::string type_name;
    BehaviourFactoryFn factory = nullptr;
    std::vector<std::string> injected_services;
    std::vector<std::string> required_components;
    std::vector<std::string> optional_components;
};

struct ModuleDescriptor {
    uint32_t abi_version = ENGINE_SCRIPT_ABI_VERSION;
    std::string sdk_hash = ENGINE_SCRIPT_SDK_HASH;
    std::string module_name;
    uint32_t build_number = 0;
    std::vector<BehaviourTypeRegistration> behaviour_types;
};

class GameModule {
public:
    explicit GameModule(ModuleDescriptor descriptor);
    ~GameModule();

    bool is_compatible() const;
    const ModuleDescriptor& descriptor() const { return m_descriptor; }

    NativeBehaviour* create_behaviour(const std::string& type_name, void** args = nullptr) const;
    bool has_behaviour_type(const std::string& type_name) const;

    void set_library_handle(void* handle) { m_lib_handle = handle; }
    void* library_handle() const { return m_lib_handle; }

private:
    ModuleDescriptor m_descriptor;
    void* m_lib_handle = nullptr;
    std::unordered_map<std::string, BehaviourTypeRegistration> m_types_map;
};

} // namespace EngineScript
