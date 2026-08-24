#pragma once

#include "../async/AsyncScheduler.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>
#include <algorithm>
#include <cctype>

namespace EngineScript {

namespace StringOps {

inline bool includes(const std::string& value, const std::string& needle) {
    return value.find(needle) != std::string::npos;
}

inline bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

inline bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline int index_of(const std::string& value, const std::string& needle) {
    const std::size_t position = value.find(needle);
    return position == std::string::npos ? -1 : static_cast<int>(position);
}

inline std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

inline std::string to_upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

} // namespace StringOps

constexpr uint32_t ENGINE_SCRIPT_ABI_VERSION = 17;
constexpr const char* ENGINE_SCRIPT_SDK_HASH = "engine2d_abi_v17";

class Input {
public:
    virtual ~Input() = default;
    [[nodiscard]] virtual float axis(const std::string& name) const = 0;
    [[nodiscard]] virtual bool pressed(const std::string& action) const = 0;
};

class Log {
public:
    virtual ~Log() = default;
    virtual void write(const std::string& message) const = 0;

    void operator()(const std::string& message) const {
        write(message);
    }
};

struct RigidBody2D {
    glm::vec2 velocity{0.0f, 0.0f};
    bool isGrounded{false};
};

struct SpriteRenderer2D {
    bool flipX{false};
};

struct Transform2D {
    glm::vec2 position{0.0f, 0.0f};
    float rotation{0.0f};
    glm::vec2 scale{1.0f, 1.0f};
};

struct Time {
    float deltaTime{0.0f};
    float realDeltaTime{0.0f};
    float elapsed{0.0f};
    float realElapsed{0.0f};
    uint64_t frameCount{0};
};

class Display {
public:
    virtual ~Display() = default;

    int width{0};
    int height{0};
    int pixelWidth{0};
    int pixelHeight{0};
    float dpiScale{1.0f};
    bool isFullscreen{false};

    virtual bool setVsync(bool enabled) = 0;
    virtual bool setFullscreen(bool enabled) = 0;
};

struct Entity {
    uint32_t id = 0;
    std::string name;
    std::string tag;

    using DestroyCallback = void (*)(void*, uint32_t);
    void* runtime_context = nullptr;
    DestroyCallback destroy_callback = nullptr;

    void destroy() const {
        if (destroy_callback) destroy_callback(runtime_context, id);
    }
};

struct ParticleEmitter2D {
    float emitRate{0.0f};
    int burstCount{0};
    bool active{false};
};

struct Joint2D {
    Entity target{};
    bool collideConnected{false};
};

struct RaycastHit {
    bool hit{false};
    Entity entity{};
    glm::vec2 point{0.0f, 0.0f};
    glm::vec2 normal{0.0f, 0.0f};
    float distance{0.0f};
};

class Physics {
public:
    virtual ~Physics() = default;
    [[nodiscard]] virtual RaycastHit* raycast(
        const glm::vec2& origin,
        const glm::vec2& destination,
        uint32_t mask = 0xFFFFu
    ) = 0;
};

class Entities {
public:
    virtual ~Entities() = default;
    virtual Entity create(const std::string& name) = 0;
    [[nodiscard]] virtual Entity findByName(const std::string& name) const = 0;
    [[nodiscard]] virtual Entity findByTag(const std::string& tag) const = 0;
    [[nodiscard]] virtual bool exists(const Entity& entity) const = 0;
    [[nodiscard]] virtual std::vector<Entity> findAllByTag(const std::string& tag) const = 0;
    [[nodiscard]] virtual std::vector<Entity> findAllWithTransform() const = 0;
    [[nodiscard]] virtual bool hasTransform(const Entity& entity) const = 0;
    [[nodiscard]] virtual Transform2D* transform(const Entity& entity) = 0;
    [[nodiscard]] virtual bool hasRigidBody(const Entity& entity) const = 0;
    [[nodiscard]] virtual RigidBody2D* rigidBody(const Entity& entity) = 0;
    [[nodiscard]] virtual bool hasSpriteRenderer(const Entity& entity) const = 0;
    [[nodiscard]] virtual SpriteRenderer2D* spriteRenderer(const Entity& entity) = 0;
    [[nodiscard]] virtual bool hasParticleEmitter(const Entity& entity) const = 0;
    [[nodiscard]] virtual ParticleEmitter2D* particleEmitter(const Entity& entity) = 0;
    [[nodiscard]] virtual bool hasJoint(const Entity& entity) const = 0;
    [[nodiscard]] virtual Joint2D* joint(const Entity& entity) = 0;
};

class Animator2D {
public:
    virtual ~Animator2D() = default;
    virtual void play(const std::string& clip_name) = 0;
};

class Audio {
public:
    virtual ~Audio() = default;
    virtual bool playSound(const std::string& path, float volume = 1.0f, float pitch = 1.0f) = 0;
    virtual bool playMusic(const std::string& path, float volume = 1.0f, bool loop = true) = 0;
    virtual void stopMusic() = 0;
    virtual void pauseMusic() = 0;
    virtual void resumeMusic() = 0;
    virtual void setMasterVolume(float volume) = 0;
    virtual void setMusicVolume(float volume) = 0;
    virtual void setSfxVolume(float volume) = 0;
    [[nodiscard]] virtual float masterVolume() const = 0;
    [[nodiscard]] virtual float musicVolume() const = 0;
    [[nodiscard]] virtual float sfxVolume() const = 0;
};

class Scene {
public:
    virtual ~Scene() = default;
    virtual bool load(const std::string& path) = 0;
    [[nodiscard]] virtual std::string currentName() const = 0;
    [[nodiscard]] virtual bool isLoading() const = 0;
    [[nodiscard]] virtual std::string lastError() const = 0;
};

class Events {
public:
    virtual ~Events() = default;
    virtual bool emit(const std::string& name, const std::string& payload = {}) = 0;
    virtual bool subscribe(const std::string& name) = 0;
};

struct Asset {
    uint64_t handle{0};
    std::string path;
    std::string type;
    std::string status;
    bool ready{false};
};

struct Texture {};
struct AudioClip {};
struct Font {};
struct DataAsset {};

enum class ResourceType : uint8_t { Texture, AudioClip, Font, DataAsset };

template <typename T>
inline constexpr ResourceType resource_type_for = ResourceType::DataAsset;
template <> inline constexpr ResourceType resource_type_for<Texture> = ResourceType::Texture;
template <> inline constexpr ResourceType resource_type_for<AudioClip> = ResourceType::AudioClip;
template <> inline constexpr ResourceType resource_type_for<Font> = ResourceType::Font;

class Assets {
public:
    virtual ~Assets() = default;
    [[nodiscard]] virtual Asset* find(const std::string& path) = 0;
    [[nodiscard]] virtual bool exists(const std::string& path) const = 0;
    virtual bool request(const std::string& path) = 0;
    virtual Task<Asset*> load_resource(const std::string& path, ResourceType type) = 0;

    template <typename T>
    Task<Asset*> load(const std::string& path) {
        return load_resource(path, resource_type_for<T>);
    }
};

template <typename T, typename E = std::string>
class Result {
public:
    static Result success(T value) {
        Result result;
        result.m_value = std::move(value);
        return result;
    }

    static Result failure(E error) {
        Result result;
        result.m_error = std::move(error);
        return result;
    }

    [[nodiscard]] bool isOk() const { return m_value.has_value(); }
    [[nodiscard]] bool isError() const { return !isOk(); }
    [[nodiscard]] const T* value() const { return m_value ? &*m_value : nullptr; }
    [[nodiscard]] const E* error() const { return m_error ? &*m_error : nullptr; }

private:
    std::optional<T> m_value;
    std::optional<E> m_error;
};

template <typename E>
class Result<void, E> {
public:
    static Result success() { return Result{}; }
    static Result failure(E error) {
        Result result;
        result.m_error = std::move(error);
        return result;
    }

    [[nodiscard]] bool isOk() const { return !m_error.has_value(); }
    [[nodiscard]] bool isError() const { return !isOk(); }
    [[nodiscard]] const E* error() const { return m_error ? &*m_error : nullptr; }

private:
    std::optional<E> m_error;
};

class UI {
public:
    virtual ~UI() = default;
    virtual bool setText(const Entity& entity, const std::string& text) = 0;
    virtual bool setVisible(const Entity& entity, bool visible) = 0;
};

class Tilemap {
public:
    virtual ~Tilemap() = default;
    [[nodiscard]] virtual int getTile(const Entity& entity, int x, int y) const = 0;
    virtual bool setTile(const Entity& entity, int x, int y, int tile_id) = 0;
};

class NativeBehaviour {
public:
    using ExposedPropertyValue = std::variant<bool, double, std::string, glm::vec2, Entity>;

    virtual ~NativeBehaviour() = default;
    virtual void update(float delta_time) {}
    virtual void on_start() {}
    virtual void on_destroy() {}
    virtual void on_collision_enter(const Entity& other) { static_cast<void>(other); }
    virtual void on_event(const std::string& name, const std::string& payload) {
        static_cast<void>(name);
        static_cast<void>(payload);
    }
    virtual bool set_exposed_property(const std::string& name, const ExposedPropertyValue& value) {
        static_cast<void>(name);
        static_cast<void>(value);
        return false;
    }
    virtual bool get_exposed_property(const std::string& name, ExposedPropertyValue& value) const {
        static_cast<void>(name);
        static_cast<void>(value);
        return false;
    }

    [[nodiscard]] AsyncScheduler::DelayAwaiter delay(float seconds) const {
        return async_scheduler
            ? async_scheduler->delay(seconds, task_scope_id)
            : AsyncScheduler::DelayAwaiter{nullptr, seconds, task_scope_id, false};
    }

    [[nodiscard]] AsyncScheduler::DelayAwaiter realDelay(float seconds) const {
        return async_scheduler
            ? async_scheduler->real_delay(seconds, task_scope_id)
            : AsyncScheduler::DelayAwaiter{nullptr, seconds, task_scope_id, true};
    }

    [[nodiscard]] AsyncScheduler::NextFrameAwaiter nextFrame() const {
        return async_scheduler
            ? async_scheduler->next_frame(task_scope_id)
            : AsyncScheduler::NextFrameAwaiter{nullptr, task_scope_id};
    }

    [[nodiscard]] AsyncScheduler::FixedUpdateAwaiter fixedUpdate() const {
        return async_scheduler
            ? async_scheduler->fixed_update(task_scope_id)
            : AsyncScheduler::FixedUpdateAwaiter{nullptr, task_scope_id};
    }

    [[nodiscard]] Entity instantiate(const std::string& name) const {
        return entities ? entities->create(name) : Entity{};
    }

    [[nodiscard]] std::vector<Entity> findEntities(const std::string& tag) const {
        return entities ? entities->findAllByTag(tag) : std::vector<Entity>{};
    }

    template <typename T>
    void launch(Task<T> task) {
        if (task.is_valid() && !task.is_done()) {
            m_owned_tasks.push_back(std::make_unique<OwnedTask<T>>(std::move(task)));
        }
    }

    void reap_completed_tasks() {
        std::erase_if(m_owned_tasks, [](const std::unique_ptr<OwnedTaskBase>& task) {
            return !task || task->is_done();
        });
    }

    void cancel_owned_tasks() {
        for (const auto& task : m_owned_tasks) if (task) task->cancel();
        m_owned_tasks.clear();
    }

    uint32_t entity_id = 0;
    uint32_t task_scope_id = 0;
    AsyncScheduler* async_scheduler = nullptr;
    Entities* entities = nullptr;

private:
    struct OwnedTaskBase {
        virtual ~OwnedTaskBase() = default;
        [[nodiscard]] virtual bool is_done() const = 0;
        virtual void cancel() = 0;
    };

    template <typename T>
    struct OwnedTask final : OwnedTaskBase {
        explicit OwnedTask(Task<T> task_value) : task(std::move(task_value)) {}
        [[nodiscard]] bool is_done() const override { return task.is_done(); }
        void cancel() override { task.cancel(); }
        Task<T> task;
    };

    std::vector<std::unique_ptr<OwnedTaskBase>> m_owned_tasks;
};

using BehaviourFactoryFn = std::function<void*(void** args)>;

enum class NativeDependencyKind : uint8_t {
    Service,
    RequiredComponent,
    OptionalComponent
};

struct NativeDependencyExport {
    const char* type_name{nullptr};
    NativeDependencyKind kind{NativeDependencyKind::Service};
};

struct NativeBehaviourExport {
    const char* type_name{nullptr};
    NativeBehaviour* (*factory)(void** args){nullptr};
    const NativeDependencyExport* dependencies{nullptr};
    std::size_t dependency_count{0};
};

struct NativeModuleExports {
    uint32_t abi_version{0};
    const char* sdk_hash{nullptr};
    const char* module_name{nullptr};
    uint32_t build_number{0};
    const NativeBehaviourExport* behaviour_types{nullptr};
    std::size_t behaviour_type_count{0};
};

using GetNativeModuleExportsFn = const NativeModuleExports* (*)();

inline constexpr const char* ENGINE_SCRIPT_MODULE_EXPORT_SYMBOL = "engine_script_module_exports";

// Native libraries expose this symbol with C linkage. The returned data must
// remain valid until the loader releases the library.
extern "C" const NativeModuleExports* engine_script_module_exports();

struct BehaviourTypeRegistration {
    std::string type_name;
    BehaviourFactoryFn factory = nullptr;
    std::vector<std::string> injected_services;
    std::vector<std::string> required_components;
    std::vector<std::string> optional_components;
    std::vector<std::pair<std::string, NativeDependencyKind>> constructor_dependencies;
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

    [[nodiscard]] static std::unique_ptr<GameModule> from_native_exports(
        const NativeModuleExports& exports,
        std::string* error = nullptr
    );
    [[nodiscard]] static std::unique_ptr<GameModule> load_dynamic_library(
        const std::string& path,
        std::string* error = nullptr
    );

    void* library_handle() const { return m_lib_handle; }

private:
    ModuleDescriptor m_descriptor;
    void* m_lib_handle = nullptr;
    std::unordered_map<std::string, BehaviourTypeRegistration> m_types_map;
};

} // namespace EngineScript
