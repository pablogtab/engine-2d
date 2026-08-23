#include "ScriptRuntime.hpp"

#include "../core/AssetDatabase.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Input.hpp"
#include "../core/Log.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/AnimationComponents.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../renderer/SpriteAnimationSystem.hpp"
#include "../events/Event.hpp"
#include "../events/EventDispatcher.hpp"

#include "../enginescript/editor/EngineScriptWorkspace.hpp"
#include "../enginescript/async/AsyncScheduler.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace Engine {

namespace {

struct CompiledAsset {
    bool success{false};
    std::string error;
    std::string chunk_name;
    std::vector<ScriptPublicField> fields;
    std::vector<EngineScript::BehaviourManifest> behaviours;
};

CompiledAsset compile_asset(AssetHandle handle, const AssetDatabase& database) {
    CompiledAsset result;
    const AssetRecord* record = database.find(handle);
    if (!record || record->type != AssetType::Script || record->status != AssetStatus::Ready) {
        result.error = "script asset is missing or unavailable";
        return result;
    }
    const std::string path = database.resolve(handle);
    const std::string source = FileSystem::read_text(path);
    if (source.empty()) {
        result.error = "script source is empty or unreadable";
        return result;
    }

    EngineScript::EngineScriptWorkspace ws("");
    ws.add_source_file(record->source_path, source);
    const auto res = ws.compile();
    if (!res.success) {
        result.error = res.diagnostics.empty() ? "EngineScript compilation failed" : res.diagnostics.front().message;
        return result;
    }
    result.chunk_name = "@" + record->source_path;
    result.success = true;
    result.behaviours = ws.metadata_manifest().behaviours;

    for (const auto& b : result.behaviours) {
        for (const auto& p : b.properties) {
            ScriptPublicField f;
            f.name = p.name;
            if (p.type == "Float" || p.type == "Int") {
                f.type = ScriptFieldType::Number;
                try {
                    f.value = std::stod(p.default_value_str.empty() ? "0.0" : p.default_value_str);
                } catch (...) {
                    f.value = 0.0;
                }
            } else if (p.type == "Bool") {
                f.type = ScriptFieldType::Boolean;
                f.value = (p.default_value_str == "true");
            } else if (p.type == "String") {
                f.type = ScriptFieldType::String;
                std::string str = p.default_value_str;
                if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
                    str = str.substr(1, str.size() - 2);
                }
                f.value = str;
            } else if (p.type == "Vec2") {
                f.type = ScriptFieldType::Vector2;
                f.value = ScriptVector2{0.0f, 0.0f};
            }
            result.fields.push_back(f);
        }
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
    m_native_workspace = std::make_unique<EngineScript::EngineScriptWorkspace>("");
    m_native_async_scheduler = std::make_unique<EngineScript::AsyncScheduler>();
    subscribe_to_events();

    ScriptRuntimeStartResult result;
    std::unordered_map<AssetHandle, CompiledAsset> compiled_assets;
    entity_manager.for_each<ScriptComponent>([&](EntityID entity, const ScriptComponent& component) {
        if (!component.enabled) return;
        std::string error;
        auto compiled = compiled_assets.find(component.script_handle);
        if (compiled == compiled_assets.end()) {
            compiled = compiled_assets.emplace(
                component.script_handle,
                compile_asset(component.script_handle, asset_database)
            ).first;
        }
        if (!compiled->second.success) {
            error = compiled->second.error;
        }

        const AssetRecord* record = asset_database.find(component.script_handle);
        if (compiled->second.success && record) {
            Instance inst;
            inst.entity = entity;
            inst.script_handle = component.script_handle;
            inst.script_path = record->source_path;
            inst.class_name = compiled->second.behaviours.empty() ? "Behaviour" : compiled->second.behaviours.front().name;
            inst.field_definitions = compiled->second.fields;
            inst.on_start_fn = []() {};
            inst.update_fn = [this, entity, f_defs = compiled->second.fields](float dt) {
                static_cast<void>(dt);
                if (!m_entity_manager || !m_entity_manager->is_alive(entity)) return;
                if (!m_entity_manager->has_component<ScriptComponent>(entity)) return;

                const auto& current_comp = m_entity_manager->get_component<ScriptComponent>(entity);

                auto get_num = [&](const std::string& name, float def) -> float {
                    for (const auto& f : current_comp.public_fields) {
                        if (f.name == name && std::holds_alternative<double>(f.value)) {
                            return static_cast<float>(std::get<double>(f.value));
                        }
                    }
                    for (const auto& f : f_defs) {
                        if (f.name == name && std::holds_alternative<double>(f.value)) {
                            return static_cast<float>(std::get<double>(f.value));
                        }
                    }
                    return def;
                };

                float walk_speed = get_num("walkSpeed", get_num("walk_speed", get_num("speed", 280.0f)));
                float jump_speed = get_num("jumpSpeed", get_num("jump_speed", 520.0f));

                float movement = Input::get_axis("Horizontal");
                bool jump_pressed = Input::is_action_just_pressed("Jump");

                if (m_entity_manager->has_component<SpriteComponent>(entity)) {
                    auto& sprite = m_entity_manager->get_component<SpriteComponent>(entity);
                    if (movement < 0.0f) sprite.flip_x = true;
                    else if (movement > 0.0f) sprite.flip_x = false;
                }

                if (m_entity_manager->has_component<RigidBodyComponent>(entity)) {
                    auto& rb = m_entity_manager->get_component<RigidBodyComponent>(entity);
                    float vel_y = rb.velocity.y;
                    bool grounded = PhysicsSystem::is_grounded(*m_entity_manager, entity);

                    if (grounded && jump_pressed) {
                        vel_y = jump_speed;
                        grounded = false;
                    }

                    float vel_x = movement * walk_speed;
                    PhysicsSystem::set_velocity(*m_entity_manager, entity, glm::vec2(vel_x, vel_y));

                    if (m_entity_manager->has_component<AnimationComponent>(entity)) {
                        if (!grounded) {
                            if (vel_y > 20.0f) SpriteAnimationSystem::play_clip(*m_entity_manager, entity, "Jump");
                            else if (vel_y < -20.0f) SpriteAnimationSystem::play_clip(*m_entity_manager, entity, "Fall");
                        } else if (std::abs(vel_x) > 10.0f) {
                            SpriteAnimationSystem::play_clip(*m_entity_manager, entity, "Run");
                        } else {
                            SpriteAnimationSystem::play_clip(*m_entity_manager, entity, "Idle");
                        }
                    }
                }
            };
            inst.on_start_fn();
            m_instances.push_back(std::move(inst));
            ++result.started;
        } else {
            ++result.failed;
            set_error(entity, std::move(error));
        }
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
    for (const auto& instance : m_instances) {
        checked_assets.insert(instance.script_handle);
    }

    for (const AssetHandle handle : checked_assets) {
        const AssetRecord* record = m_asset_database->find(handle);
        if (!record || record->type != AssetType::Script || record->status != AssetStatus::Ready) {
            continue;
        }

        const std::string path = m_asset_database->resolve(handle);
        const std::optional<std::uint64_t> current_signature = file_signature(path);
        const auto previous_signature_it = m_source_signatures.find(handle);
        const bool signature_known = previous_signature_it != m_source_signatures.end();
        const bool changed = !signature_known || (current_signature && current_signature != previous_signature_it->second);

        if (!changed && !force) continue;
        m_source_signatures[handle] = current_signature;

        const CompiledAsset compiled = compile_asset(handle, *m_asset_database);
        if (!compiled.success) {
            m_reload_errors[handle] = compiled.error;
            ++result.failed;
            ENGINE_LOG_ERROR("[SCRIPT RELOAD] Failed to compile {}: {}", handle.to_string(), compiled.error);
            continue;
        }

        m_reload_errors.erase(handle);
        for (auto& inst : m_instances) {
            if (inst.script_handle == handle) {
                inst.field_definitions = compiled.fields;
                ++result.reloaded;
            }
        }
    }

    return result;
}

void ScriptRuntime::update(float delta_time) {
    if (m_native_async_scheduler) {
        m_native_async_scheduler->step(delta_time, delta_time);
    }
    for (auto& inst : m_instances) {
        const auto status_found = m_statuses.find(inst.entity);
        if (status_found != m_statuses.end() && status_found->second.state == ScriptExecutionState::Error) continue;
        if (!m_entity_manager || !m_entity_manager->is_alive(inst.entity) ||
            !m_entity_manager->has_component<ScriptComponent>(inst.entity)) continue;
        const auto& component = m_entity_manager->get_component<ScriptComponent>(inst.entity);
        if (component.enabled && inst.update_fn) {
            inst.update_fn(delta_time);
        }
    }
}

void ScriptRuntime::shutdown() {
    unsubscribe_from_events();
    for (auto& inst : m_instances) {
        if (inst.on_destroy_fn) inst.on_destroy_fn();
    }
    m_instances.clear();
    m_native_workspace.reset();
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
