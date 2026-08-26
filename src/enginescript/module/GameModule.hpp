#pragma once

#include "../async/AsyncScheduler.hpp"
#include "../async/Jobs.hpp"
#include "../async/Result.hpp"
#include "../async/TaskScope.hpp"
#include "../async/TypedEventBus.hpp"
#include "../core/LanguageVersion.hpp"
#include "../runtime/Collections.hpp"
#include "../runtime/Math.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <cstddef>
#include <array>
#include <charconv>
#include <system_error>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <variant>
#include <algorithm>
#include <cctype>
#include <string_view>
#include <type_traits>
#include <cmath>
#include <limits>
#include <utility>

namespace EngineScript {

template <typename Interface, typename Value>
[[nodiscard]] std::shared_ptr<Interface> boxInterface(Value value) {
    static_assert(std::is_base_of_v<Interface, Value>);
    return std::make_shared<Value>(std::move(value));
}

namespace StringOps {

template <typename Value>
requires (std::is_arithmetic_v<Value> && !std::is_same_v<Value, bool>)
[[nodiscard]] inline std::string from_numeric(Value value) {
    if constexpr (std::is_floating_point_v<Value>) {
        if (std::isnan(value)) return "nan";
        if (std::isinf(value)) return value < Value{0} ? "-inf" : "inf";
    }

    std::array<char, 128> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return error == std::errc{}
        ? std::string(buffer.data(), end)
        : std::string{};
}

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

template <typename Target, typename Source>
requires (std::is_arithmetic_v<Target> && std::is_arithmetic_v<Source> &&
          !std::is_same_v<Target, bool> && !std::is_same_v<Source, bool>)
[[nodiscard]] inline Target numericConvert(Source value) {
    if constexpr (std::is_same_v<Target, Source>) {
        return value;
    } else if constexpr (std::is_floating_point_v<Source> && std::is_integral_v<Target>) {
        if (std::isnan(value)) return Target{0};
        if (std::isinf(value)) {
            return value > Source{0}
                ? std::numeric_limits<Target>::max()
                : std::numeric_limits<Target>::lowest();
        }
        const long double truncated = std::trunc(static_cast<long double>(value));
        const long double minimum = static_cast<long double>(std::numeric_limits<Target>::lowest());
        const long double maximum = static_cast<long double>(std::numeric_limits<Target>::max());
        if (truncated <= minimum) return std::numeric_limits<Target>::lowest();
        if (truncated >= maximum) return std::numeric_limits<Target>::max();
        return static_cast<Target>(truncated);
    } else if constexpr (std::is_integral_v<Source> && std::is_integral_v<Target>) {
        if (std::cmp_less(value, std::numeric_limits<Target>::lowest())) {
            return std::numeric_limits<Target>::lowest();
        }
        if (std::cmp_greater(value, std::numeric_limits<Target>::max())) {
            return std::numeric_limits<Target>::max();
        }
        return static_cast<Target>(value);
    } else {
        static_assert(std::numeric_limits<Target>::is_iec559,
            "EngineScript floating-point conversions require IEEE-754 targets");
        return static_cast<Target>(value);
    }
}

class Input {
public:
    virtual ~Input() = default;
    [[nodiscard]] virtual float axis(const std::string& name) const = 0;
    [[nodiscard]] virtual bool down(const std::string& action) const = 0;
    [[nodiscard]] virtual bool pressed(const std::string& action) const = 0;
    [[nodiscard]] virtual bool released(const std::string& action) const = 0;
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
    static constexpr const char* ENGINE_SCRIPT_TYPE_NAME = "RigidBody2D";
    glm::vec2 velocity{0.0f, 0.0f};
    bool isGrounded{false};

    using IgnoreOneWayCallback = void (*)(void*, uint32_t, bool);
    void* runtime_context{nullptr};
    uint32_t entity_id{0};
    IgnoreOneWayCallback ignore_one_way_callback{nullptr};

    void setIgnoreOneWay(bool ignore) const {
        if (ignore_one_way_callback) {
            ignore_one_way_callback(runtime_context, entity_id, ignore);
        }
    }
};

struct SpriteRenderer2D {
    static constexpr const char* ENGINE_SCRIPT_TYPE_NAME = "SpriteRenderer2D";
    bool flipX{false};
};

struct Transform2D {
    static constexpr const char* ENGINE_SCRIPT_TYPE_NAME = "Transform2D";
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

struct ComponentReference {
    using ResolveCallback = void* (*)(void*, uint32_t, uint64_t, uint32_t, const char*);
    using RemoveCallback = bool (*)(void*, uint32_t, uint64_t, uint32_t, const char*);

    void* runtime_context{nullptr};
    uint32_t entity_id{0};
    uint64_t slot_id{0};
    uint32_t generation{0};
    std::string type_name;
    ResolveCallback resolve_callback{nullptr};
    RemoveCallback remove_callback{nullptr};

    [[nodiscard]] void* resolve() const {
        return resolve_callback && !type_name.empty()
            ? resolve_callback(runtime_context, entity_id, slot_id, generation, type_name.c_str())
            : nullptr;
    }
};

template <typename T>
[[nodiscard]] constexpr const char* component_type_name() {
    if constexpr (requires { T::ENGINE_SCRIPT_TYPE_NAME; }) {
        return T::ENGINE_SCRIPT_TYPE_NAME;
    } else {
        return nullptr;
    }
}

template <typename T>
class ComponentHandle {
public:
    ComponentHandle() = default;
    ComponentHandle(std::nullptr_t) {}
    explicit ComponentHandle(ComponentReference reference) : m_reference(reference) {}

    [[nodiscard]] T* get() const { return static_cast<T*>(m_reference.resolve()); }
    [[nodiscard]] bool isValid() const { return get() != nullptr; }
    [[nodiscard]] uint64_t slotId() const { return m_reference.slot_id; }
    [[nodiscard]] const ComponentReference& reference() const { return m_reference; }

    T* operator->() const { return get(); }
    operator T*() const { return get(); }
    explicit operator bool() const { return isValid(); }

private:
    ComponentReference m_reference;
};

struct Entity {
    uint32_t id = 0;
    std::string name;
    std::string tag;

    using DestroyCallback = void (*)(void*, uint32_t);
    using FindComponentCallback = ComponentReference (*)(void*, uint32_t, const char*, std::size_t);
    using CountComponentsCallback = std::size_t (*)(void*, uint32_t, const char*);
    using AddComponentCallback = ComponentReference (*)(void*, uint32_t, const char*);
    void* runtime_context = nullptr;
    DestroyCallback destroy_callback = nullptr;
    void* component_context = nullptr;
    FindComponentCallback find_component_callback = nullptr;
    CountComponentsCallback count_components_callback = nullptr;
    AddComponentCallback add_component_callback = nullptr;

    void destroy() const {
        if (destroy_callback) destroy_callback(runtime_context, id);
    }

    template <typename T>
    [[nodiscard]] ComponentHandle<T> getComponent() const {
        const char* type = component_type_name<T>();
        return ComponentHandle<T>{find_component_callback && type
            ? find_component_callback(component_context, id, type, 0)
            : ComponentReference{}};
    }

    template <typename T>
    [[nodiscard]] std::vector<ComponentHandle<T>> getComponents() const {
        std::vector<ComponentHandle<T>> result;
        const char* type = component_type_name<T>();
        if (!find_component_callback || !count_components_callback || !type) return result;
        const std::size_t count = count_components_callback(component_context, id, type);
        result.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            ComponentReference reference = find_component_callback(component_context, id, type, index);
            if (reference.resolve_callback) result.emplace_back(reference);
        }
        return result;
    }

    template <typename T>
    [[nodiscard]] bool hasComponent() const {
        return static_cast<bool>(getComponent<T>());
    }

    template <typename T>
    [[nodiscard]] ComponentHandle<T> addComponent() const {
        const char* type = component_type_name<T>();
        return ComponentHandle<T>{add_component_callback && type
            ? add_component_callback(component_context, id, type)
            : ComponentReference{}};
    }

    template <typename T>
    bool removeComponent(const ComponentHandle<T>& component) const {
        const ComponentReference& reference = component.reference();
        return reference.remove_callback && reference.remove_callback(
            reference.runtime_context,
            reference.entity_id,
            reference.slot_id,
            reference.generation,
            reference.type_name.c_str()
        );
    }
};

struct Asset {
    uint64_t handle{0};
    std::string path;
    std::string type;
    std::string status;
    bool ready{false};
};

// A prefab is an Asset used as an instantiation template. Keeping the same
// runtime representation preserves the stable AssetHandle across renames.
using Prefab = Asset;

struct ParticleEmitter2D {
    static constexpr const char* ENGINE_SCRIPT_TYPE_NAME = "ParticleEmitter2D";
    float emitRate{0.0f};
    int burstCount{0};
    bool active{false};
    bool isPlaying{false};
    uint64_t effectHandle{0};

    using PlayCallback = bool (*)(void*, uint32_t);
    using StopCallback = bool (*)(void*, uint32_t, bool);
    using BurstCallback = bool (*)(void*, uint32_t, int);
    using SetEffectCallback = bool (*)(void*, uint32_t, const Asset*);
    void* runtime_context{nullptr};
    uint32_t entity_id{0};
    PlayCallback play_callback{nullptr};
    StopCallback stop_callback{nullptr};
    BurstCallback burst_callback{nullptr};
    SetEffectCallback set_effect_callback{nullptr};

    bool play() const { return play_callback && play_callback(runtime_context, entity_id); }
    bool stop(bool clear = false) const {
        return stop_callback && stop_callback(runtime_context, entity_id, clear);
    }
    bool burst(int count = 0) const {
        return burst_callback && burst_callback(runtime_context, entity_id, count);
    }
    bool setEffect(const Asset* effect) const {
        return set_effect_callback && set_effect_callback(runtime_context, entity_id, effect);
    }
};

struct Joint2D {
    static constexpr const char* ENGINE_SCRIPT_TYPE_NAME = "Joint2D";
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
    virtual bool destroy(const Entity& entity) = 0;
    virtual bool setTag(const Entity& entity, const std::string& tag) = 0;
    [[nodiscard]] virtual Entity parent(const Entity& entity) const = 0;
    virtual bool setParent(const Entity& child, const Entity& parent) = 0;
    virtual bool clearParent(const Entity& child) = 0;
    [[nodiscard]] virtual Entity findByName(const std::string& name) const = 0;
    [[nodiscard]] virtual Entity findByTag(const std::string& tag) const = 0;
    [[nodiscard]] virtual bool exists(const Entity& entity) const = 0;
    [[nodiscard]] virtual std::vector<Entity> findAllByTag(const std::string& tag) const = 0;
    [[nodiscard]] virtual Entity findByGameplayTag(const std::string& tag) const = 0;
    [[nodiscard]] virtual std::vector<Entity> findAllByGameplayTag(const std::string& tag) const = 0;
    [[nodiscard]] virtual bool hasGameplayTag(const Entity& entity, const std::string& tag) const = 0;
    virtual bool addGameplayTag(const Entity& entity, const std::string& tag) = 0;
    virtual bool removeGameplayTag(const Entity& entity, const std::string& tag) = 0;
    [[nodiscard]] virtual std::vector<std::string> gameplayTags(const Entity& entity) const = 0;
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
    static constexpr const char* ENGINE_SCRIPT_TYPE_NAME = "Animator2D";
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

class Prefabs {
public:
    virtual ~Prefabs() = default;
    [[nodiscard]] virtual Entity create(const Prefab& prefab) = 0;
    virtual bool spawn(const Entity& instance, const glm::vec2& position) = 0;
    [[nodiscard]] virtual Entity instantiate(const Prefab& prefab, const glm::vec2& position) = 0;
};

class PlayerPrefs {
public:
    virtual ~PlayerPrefs() = default;
    [[nodiscard]] virtual bool has(const std::string& key) const = 0;
    [[nodiscard]] virtual int getInt(const std::string& key, int fallback = 0) const = 0;
    [[nodiscard]] virtual float getFloat(const std::string& key, float fallback = 0.0f) const = 0;
    [[nodiscard]] virtual bool getBool(const std::string& key, bool fallback = false) const = 0;
    [[nodiscard]] virtual std::string getString(const std::string& key, const std::string& fallback = {}) const = 0;
    [[nodiscard]] virtual glm::vec2 getVec2(const std::string& key, const glm::vec2& fallback = {}) const = 0;
    [[nodiscard]] virtual glm::vec3 getVec3(const std::string& key, const glm::vec3& fallback = {}) const = 0;
    virtual void setInt(const std::string& key, int value) = 0;
    virtual void setFloat(const std::string& key, float value) = 0;
    virtual void setBool(const std::string& key, bool value) = 0;
    virtual void setString(const std::string& key, const std::string& value) = 0;
    virtual void setVec2(const std::string& key, const glm::vec2& value) = 0;
    virtual void setVec3(const std::string& key, const glm::vec3& value) = 0;
    virtual void remove(const std::string& key) = 0;
    virtual void clear() = 0;
    [[nodiscard]] virtual bool save() = 0;
};

struct ParticleEmitOptions {
    int count{1};
    bool overrideVelocity{false};
    glm::vec2 velocity{0.0f, 0.0f};
    bool overrideVelocityVariation{false};
    glm::vec2 velocityVariation{0.0f, 0.0f};
    bool overrideColor{false};
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float scale{1.0f};
    float rotation{0.0f};
};

class Particles {
public:
    virtual ~Particles() = default;
    virtual bool emit(const Asset* effect, const glm::vec2& position, int count = 1) = 0;
    virtual bool emitWithOptions(
        const Asset* effect,
        const glm::vec2& position,
        const ParticleEmitOptions& options
    ) = 0;
};

struct Texture {};
struct AudioClip {};
struct Font {};
struct DataAsset {};
struct ParticleEffect2D {};

enum class ResourceType : uint8_t { Texture, AudioClip, Font, DataAsset, ParticleEffect };

template <typename T>
inline constexpr ResourceType resource_type_for = ResourceType::DataAsset;
template <> inline constexpr ResourceType resource_type_for<Texture> = ResourceType::Texture;
template <> inline constexpr ResourceType resource_type_for<AudioClip> = ResourceType::AudioClip;
template <> inline constexpr ResourceType resource_type_for<Font> = ResourceType::Font;
template <> inline constexpr ResourceType resource_type_for<ParticleEffect2D> = ResourceType::ParticleEffect;

class Assets {
public:
    virtual ~Assets() = default;
    [[nodiscard]] virtual Asset* find(const std::string& path) = 0;
    [[nodiscard]] virtual bool exists(const std::string& path) const = 0;
    virtual Task<Result<Asset*, std::string>> load_resource(const std::string& path, ResourceType type) = 0;

    template <typename T>
    Task<Result<Asset*, std::string>> load(const std::string& path);
};

template <typename T>
Task<Result<Asset*, std::string>> Assets::load(const std::string& path) {
    return load_resource(path, resource_type_for<T>);
}

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
    using ExposedPropertyValue = std::variant<bool, double, std::string, glm::vec2, Entity, Asset>;

    virtual ~NativeBehaviour() = default;
    virtual void update(float delta_time) {}
    virtual void on_start() {}
    virtual void on_destroy() {}
    virtual void on_collision_enter(const Entity& other) { static_cast<void>(other); }
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
