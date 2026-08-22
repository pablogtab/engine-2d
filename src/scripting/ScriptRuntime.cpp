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

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <string_view>

namespace Engine {
namespace {

constexpr int k_invalid_reference = -1;

std::string stack_error(lua_State* state, std::string fallback) {
    if (state && lua_isstring(state, -1)) {
        if (const char* message = lua_tostring(state, -1); message) return message;
    }
    return fallback;
}

bool has_strict_directive(std::string_view source) {
    constexpr std::string_view strict = "--!strict";
    if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
    return source.starts_with(strict) &&
        (source.size() == strict.size() || source[strict.size()] == '\n' || source[strict.size()] == '\r');
}

std::optional<ScriptFieldType> field_type_from_name(std::string_view name) {
    if (name == "boolean") return ScriptFieldType::Boolean;
    if (name == "number") return ScriptFieldType::Number;
    if (name == "string") return ScriptFieldType::String;
    if (name == "vector2") return ScriptFieldType::Vector2;
    if (name == "entity") return ScriptFieldType::Entity;
    if (name == "asset") return ScriptFieldType::Asset;
    return std::nullopt;
}

bool read_default_value(
    lua_State* state,
    ScriptFieldType type,
    ScriptFieldValue& result,
    std::string& error
) {
    switch (type) {
        case ScriptFieldType::Boolean:
            if (!lua_isboolean(state, -1)) { error = "boolean default must be a boolean"; return false; }
            result = lua_toboolean(state, -1) != 0;
            return true;
        case ScriptFieldType::Number:
            if (!lua_isnumber(state, -1)) { error = "number default must be a number"; return false; }
            result = static_cast<double>(lua_tonumber(state, -1));
            return true;
        case ScriptFieldType::String:
            if (!lua_isstring(state, -1)) { error = "string default must be a string"; return false; }
            result = std::string(lua_tostring(state, -1));
            return true;
        case ScriptFieldType::Vector2: {
            if (!lua_istable(state, -1) || lua_objlen(state, -1) != 2) {
                error = "vector2 default must be {x, y}";
                return false;
            }
            lua_rawgeti(state, -1, 1);
            lua_rawgeti(state, -2, 2);
            const bool valid = lua_isnumber(state, -2) && lua_isnumber(state, -1);
            if (valid) {
                result = ScriptVector2{
                    static_cast<float>(lua_tonumber(state, -2)),
                    static_cast<float>(lua_tonumber(state, -1))
                };
            }
            lua_pop(state, 2);
            if (!valid) error = "vector2 default must contain two numbers";
            return valid;
        }
        case ScriptFieldType::Entity: {
            if (!lua_isnumber(state, -1)) { error = "entity default must be an entity ID"; return false; }
            const lua_Number number = lua_tonumber(state, -1);
            if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number) {
                error = "entity default must be a non-negative integer";
                return false;
            }
            result = static_cast<EntityID>(number);
            return true;
        }
        case ScriptFieldType::Asset:
            if (lua_isnil(state, -1)) { result = AssetHandle{}; return true; }
            if (!lua_isstring(state, -1)) { error = "asset default must be a handle string or nil"; return false; }
            if (const auto handle = AssetHandle::from_string(lua_tostring(state, -1)); handle) {
                result = *handle;
                return true;
            }
            error = "asset default handle is invalid";
            return false;
    }
    error = "unsupported script field type";
    return false;
}

bool read_public_fields(lua_State* state, int module_index, std::vector<ScriptPublicField>& fields, std::string& error) {
    module_index = lua_absindex(state, module_index);
    lua_getfield(state, module_index, "fields");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return true;
    }
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        error = "script fields must be an array";
        return false;
    }

    const int fields_index = lua_absindex(state, -1);
    const int count = lua_objlen(state, fields_index);
    fields.reserve(static_cast<std::size_t>(count));
    for (int index = 1; index <= count; ++index) {
        lua_rawgeti(state, fields_index, index);
        if (!lua_istable(state, -1)) {
            lua_pop(state, 2);
            error = "script field descriptor must be a table";
            return false;
        }
        const int descriptor_index = lua_absindex(state, -1);
        lua_getfield(state, descriptor_index, "name");
        if (!lua_isstring(state, -1) || std::string_view(lua_tostring(state, -1)).empty()) {
            lua_pop(state, 3);
            error = "script field descriptor requires a non-empty name";
            return false;
        }
        ScriptPublicField field;
        field.name = lua_tostring(state, -1);
        lua_pop(state, 1);

        if (std::any_of(fields.begin(), fields.end(), [&field](const ScriptPublicField& existing) {
                return existing.name == field.name;
            })) {
            lua_pop(state, 2);
            error = "script field names must be unique";
            return false;
        }

        lua_getfield(state, descriptor_index, "type");
        const auto type = lua_isstring(state, -1)
            ? field_type_from_name(lua_tostring(state, -1)) : std::nullopt;
        lua_pop(state, 1);
        if (!type) {
            lua_pop(state, 2);
            error = "script field '" + field.name + "' has an unsupported type";
            return false;
        }
        field.type = *type;

        lua_getfield(state, descriptor_index, "default");
        std::string value_error;
        const bool valid_default = read_default_value(state, field.type, field.value, value_error);
        lua_pop(state, 1);
        if (!valid_default) {
            lua_pop(state, 2);
            error = "script field '" + field.name + "' " + value_error;
            return false;
        }
        fields.push_back(std::move(field));
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    return true;
}

bool load_module(
    lua_State* state,
    const std::string& bytecode,
    const std::string& chunk_name,
    std::string& error
) {
    if (luau_load(state, chunk_name.c_str(), bytecode.data(), bytecode.size(), 0) != LUA_OK) {
        error = stack_error(state, "failed to load script bytecode");
        lua_settop(state, 0);
        return false;
    }
    if (lua_pcall(state, 0, 1, 0) != LUA_OK) {
        error = stack_error(state, "script module failed during initialization");
        lua_settop(state, 0);
        return false;
    }
    if (!lua_istable(state, -1)) {
        error = "script module must return a table";
        lua_settop(state, 0);
        return false;
    }
    return true;
}

struct CompiledAsset {
    bool success{false};
    std::string bytecode;
    std::string chunk_name;
    std::vector<ScriptPublicField> fields;
    std::string error;
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
    if (!has_strict_directive(source)) {
        result.error = "script must start with --!strict";
        return result;
    }
    try {
        Luau::CompileOptions options;
        options.optimizationLevel = 1;
        options.debugLevel = 1;
        options.typeInfoLevel = 1;
        result.bytecode = Luau::compile(source, options);
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    }
    result.chunk_name = "@" + record->source_path;

    lua_State* inspection_state = luaL_newstate();
    if (!inspection_state) {
        result.error = "could not create Luau inspection state";
        return result;
    }
    luaL_openlibs(inspection_state);
    luaL_sandbox(inspection_state);
    if (!load_module(inspection_state, result.bytecode, result.chunk_name, result.error) ||
        !read_public_fields(inspection_state, -1, result.fields, result.error)) {
        lua_close(inspection_state);
        return result;
    }
    lua_close(inspection_state);
    result.success = true;
    return result;
}

void push_field_value(lua_State* state, const ScriptPublicField& field) {
    switch (field.type) {
        case ScriptFieldType::Boolean: lua_pushboolean(state, std::get<bool>(field.value)); break;
        case ScriptFieldType::Number: lua_pushnumber(state, std::get<double>(field.value)); break;
        case ScriptFieldType::String: lua_pushstring(state, std::get<std::string>(field.value).c_str()); break;
        case ScriptFieldType::Vector2: {
            const ScriptVector2 value = std::get<ScriptVector2>(field.value);
            lua_createtable(state, 2, 0);
            lua_pushnumber(state, value.x);
            lua_rawseti(state, -2, 1);
            lua_pushnumber(state, value.y);
            lua_rawseti(state, -2, 2);
            break;
        }
        case ScriptFieldType::Entity: lua_pushinteger(state, std::get<EntityID>(field.value)); break;
        case ScriptFieldType::Asset: {
            const AssetHandle handle = std::get<AssetHandle>(field.value);
            if (handle.is_valid()) lua_pushstring(state, handle.to_string().c_str());
            else lua_pushnil(state);
            break;
        }
    }
}

EntityManager* bound_entity_manager(lua_State* state) {
    return static_cast<EntityManager*>(lua_touserdata(state, lua_upvalueindex(1)));
}

EntityID bound_entity(lua_State* state) {
    return static_cast<EntityID>(lua_tointeger(state, lua_upvalueindex(2)));
}

int get_position(lua_State* state) {
    EntityManager* entity_manager = bound_entity_manager(state);
    const EntityID entity = bound_entity(state);
    if (!entity_manager || !entity_manager->is_alive(entity) ||
        !entity_manager->has_component<TransformComponent>(entity)) {
        luaL_error(state, "entity %u has no Transform", entity);
        return 0;
    }
    const glm::vec3 position = entity_manager->get_component<TransformComponent>(entity).position;
    lua_pushnumber(state, position.x);
    lua_pushnumber(state, position.y);
    lua_pushnumber(state, position.z);
    return 3;
}

int set_position(lua_State* state) {
    EntityManager* entity_manager = bound_entity_manager(state);
    const EntityID entity = bound_entity(state);
    if (!entity_manager || !entity_manager->is_alive(entity) ||
        !entity_manager->has_component<TransformComponent>(entity)) {
        luaL_error(state, "entity %u has no Transform", entity);
        return 0;
    }
    auto& transform = entity_manager->get_component<TransformComponent>(entity);
    transform.position.x = static_cast<float>(luaL_checknumber(state, 1));
    transform.position.y = static_cast<float>(luaL_checknumber(state, 2));
    transform.position.z = static_cast<float>(luaL_optnumber(state, 3, transform.position.z));
    return 0;
}

int get_axis(lua_State* state) {
    lua_pushnumber(state, Input::get_axis(luaL_checkstring(state, 1)));
    return 1;
}

int is_action_pressed(lua_State* state) {
    lua_pushboolean(state, Input::is_action_pressed(luaL_checkstring(state, 1)));
    return 1;
}

int is_action_just_pressed(lua_State* state) {
    lua_pushboolean(state, Input::is_action_just_pressed(luaL_checkstring(state, 1)));
    return 1;
}

int is_action_just_released(lua_State* state) {
    lua_pushboolean(state, Input::is_action_just_released(luaL_checkstring(state, 1)));
    return 1;
}

int get_velocity(lua_State* state) {
    EntityManager* entity_manager = bound_entity_manager(state);
    const EntityID entity = bound_entity(state);
    if (!entity_manager || !entity_manager->is_alive(entity) ||
        !entity_manager->has_component<RigidBodyComponent>(entity)) {
        luaL_error(state, "entity %u has no RigidBody", entity);
        return 0;
    }
    const glm::vec2 velocity = entity_manager->get_component<RigidBodyComponent>(entity).velocity;
    lua_pushnumber(state, velocity.x);
    lua_pushnumber(state, velocity.y);
    return 2;
}

int set_velocity(lua_State* state) {
    EntityManager* entity_manager = bound_entity_manager(state);
    const EntityID entity = bound_entity(state);
    if (!entity_manager || !entity_manager->is_alive(entity) ||
        !entity_manager->has_component<RigidBodyComponent>(entity)) {
        luaL_error(state, "entity %u has no RigidBody", entity);
        return 0;
    }
    PhysicsSystem::set_velocity(
        *entity_manager,
        entity,
        glm::vec2(
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2))
        )
    );
    return 0;
}

int is_grounded(lua_State* state) {
    EntityManager* entity_manager = bound_entity_manager(state);
    const EntityID entity = bound_entity(state);
    if (!entity_manager || !entity_manager->is_alive(entity) ||
        !entity_manager->has_component<RigidBodyComponent>(entity)) {
        luaL_error(state, "entity %u has no RigidBody", entity);
        return 0;
    }
    lua_pushboolean(state, PhysicsSystem::is_grounded(*entity_manager, entity));
    return 1;
}

int set_sprite_flip_x(lua_State* state) {
    EntityManager* entity_manager = bound_entity_manager(state);
    const EntityID entity = bound_entity(state);
    if (!entity_manager || !entity_manager->is_alive(entity) ||
        !entity_manager->has_component<SpriteComponent>(entity)) {
        luaL_error(state, "entity %u has no Sprite", entity);
        return 0;
    }
    entity_manager->get_component<SpriteComponent>(entity).flip_x = lua_toboolean(state, 1) != 0;
    return 0;
}

int play_animation(lua_State* state) {
    EntityManager* entity_manager = bound_entity_manager(state);
    const EntityID entity = bound_entity(state);
    if (!entity_manager || !entity_manager->is_alive(entity) ||
        !entity_manager->has_component<AnimationComponent>(entity)) {
        luaL_error(state, "entity %u has no Animation", entity);
        return 0;
    }
    lua_pushboolean(state, SpriteAnimationSystem::play_clip(
        *entity_manager,
        entity,
        luaL_checkstring(state, 1)
    ));
    return 1;
}

void push_bound_function(
    lua_State* state,
    EntityManager* entity_manager,
    EntityID entity,
    lua_CFunction function,
    const char* name
) {
    lua_pushlightuserdata(state, entity_manager);
    lua_pushinteger(state, entity);
    lua_pushcclosure(state, function, name, 2);
}

const ScriptPublicField* find_override(
    const ScriptComponent& component,
    const ScriptPublicField& definition
) {
    const auto found = std::find_if(
        component.public_fields.begin(), component.public_fields.end(),
        [&definition](const ScriptPublicField& field) {
            return field.name == definition.name && field.type == definition.type &&
                script_field_value_matches_type(field);
        }
    );
    return found == component.public_fields.end() ? nullptr : &*found;
}

} // namespace

ScriptRuntime::~ScriptRuntime() {
    shutdown();
}

ScriptRuntime::ScriptRuntime(ScriptRuntime&& other) noexcept
    : m_state(other.m_state),
      m_entity_manager(other.m_entity_manager),
      m_instances(std::move(other.m_instances)),
      m_statuses(std::move(other.m_statuses)) {
    other.m_state = nullptr;
    other.m_entity_manager = nullptr;
    other.m_instances.clear();
}

ScriptRuntime& ScriptRuntime::operator=(ScriptRuntime&& other) noexcept {
    if (this == &other) return *this;
    shutdown();
    m_state = other.m_state;
    m_entity_manager = other.m_entity_manager;
    m_instances = std::move(other.m_instances);
    m_statuses = std::move(other.m_statuses);
    other.m_state = nullptr;
    other.m_entity_manager = nullptr;
    other.m_instances.clear();
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
    const AssetDatabase& asset_database
) {
    shutdown();
    m_statuses.clear();
    m_entity_manager = &entity_manager;
    m_state = luaL_newstate();
    if (!m_state) {
        m_entity_manager = nullptr;
        return {};
    }
    luaL_openlibs(m_state);
    luaL_sandbox(m_state);

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
        if (compiled->second.success && create_instance(
                entity,
                component,
                compiled->second.bytecode,
                compiled->second.chunk_name,
                compiled->second.fields,
                error
            )) {
            ++result.started;
        } else {
            ++result.failed;
            set_error(entity, std::move(error));
        }
    });
    return result;
}

bool ScriptRuntime::create_instance(
    EntityID entity,
    const ScriptComponent& component,
    const std::string& bytecode,
    const std::string& chunk_name,
    const std::vector<ScriptPublicField>& field_definitions,
    std::string& error
) {
    Instance instance;
    instance.entity = entity;
    instance.script_handle = component.script_handle;
    instance.field_definitions = field_definitions;
    instance.authored_fields = component.public_fields;
    instance.thread = lua_newthread(m_state);
    instance.thread_ref = lua_ref(m_state, -1);
    lua_pop(m_state, 1);
    luaL_sandboxthread(instance.thread);
    if (!load_module(instance.thread, bytecode, chunk_name, error)) {
        release_instance(instance);
        return false;
    }
    instance.module_ref = lua_ref(instance.thread, -1);
    lua_pop(instance.thread, 1);

    lua_createtable(instance.thread, 0, 14);
    lua_pushinteger(instance.thread, entity);
    lua_setfield(instance.thread, -2, "entity");

    lua_createtable(instance.thread, 0, static_cast<int>(field_definitions.size()));
    for (const ScriptPublicField& definition : field_definitions) {
        const ScriptPublicField* value = find_override(component, definition);
        push_field_value(instance.thread, value ? *value : definition);
        lua_setfield(instance.thread, -2, definition.name.c_str());
    }
    lua_setfield(instance.thread, -2, "fields");

    push_bound_function(instance.thread, m_entity_manager, entity, get_position, "get_position");
    lua_setfield(instance.thread, -2, "get_position");
    push_bound_function(instance.thread, m_entity_manager, entity, set_position, "set_position");
    lua_setfield(instance.thread, -2, "set_position");
    push_bound_function(instance.thread, m_entity_manager, entity, get_axis, "get_axis");
    lua_setfield(instance.thread, -2, "get_axis");
    push_bound_function(instance.thread, m_entity_manager, entity, is_action_pressed, "is_action_pressed");
    lua_setfield(instance.thread, -2, "is_action_pressed");
    push_bound_function(instance.thread, m_entity_manager, entity, is_action_just_pressed, "is_action_just_pressed");
    lua_setfield(instance.thread, -2, "is_action_just_pressed");
    push_bound_function(instance.thread, m_entity_manager, entity, is_action_just_released, "is_action_just_released");
    lua_setfield(instance.thread, -2, "is_action_just_released");
    push_bound_function(instance.thread, m_entity_manager, entity, get_velocity, "get_velocity");
    lua_setfield(instance.thread, -2, "get_velocity");
    push_bound_function(instance.thread, m_entity_manager, entity, set_velocity, "set_velocity");
    lua_setfield(instance.thread, -2, "set_velocity");
    push_bound_function(instance.thread, m_entity_manager, entity, is_grounded, "is_grounded");
    lua_setfield(instance.thread, -2, "is_grounded");
    push_bound_function(instance.thread, m_entity_manager, entity, set_sprite_flip_x, "set_sprite_flip_x");
    lua_setfield(instance.thread, -2, "set_sprite_flip_x");
    push_bound_function(instance.thread, m_entity_manager, entity, play_animation, "play_animation");
    lua_setfield(instance.thread, -2, "play_animation");

    instance.context_ref = lua_ref(instance.thread, -1);
    lua_pop(instance.thread, 1);
    m_instances.push_back(instance);
    Instance& stored = m_instances.back();
    if (!call(stored, "on_create", 0.0f, false)) {
        error = m_statuses[entity].message;
        release_instance(stored);
        m_instances.pop_back();
        return false;
    }
    stored.created = true;
    m_statuses[entity] = {ScriptExecutionState::Running, {}};
    return true;
}

void ScriptRuntime::synchronize_public_fields(
    Instance& instance,
    const ScriptComponent& component
) {
    if (component.script_handle != instance.script_handle ||
        component.public_fields == instance.authored_fields) return;

    lua_State* state = instance.thread;
    lua_settop(state, 0);
    lua_getref(state, instance.context_ref);
    lua_getfield(state, -1, "fields");
    for (const ScriptPublicField& definition : instance.field_definitions) {
        const ScriptPublicField* value = find_override(component, definition);
        push_field_value(state, value ? *value : definition);
        lua_setfield(state, -2, definition.name.c_str());
    }
    lua_settop(state, 0);
    instance.authored_fields = component.public_fields;
}

bool ScriptRuntime::call(
    Instance& instance,
    const char* callback,
    float delta_time,
    bool pass_delta
) {
    lua_State* state = instance.thread;
    lua_settop(state, 0);
    lua_getref(state, instance.module_ref);
    lua_getfield(state, -1, callback);
    if (lua_isnil(state, -1)) {
        lua_settop(state, 0);
        return true;
    }
    if (!lua_isfunction(state, -1)) {
        set_error(instance.entity, std::string(callback) + " must be a function");
        lua_settop(state, 0);
        return false;
    }
    lua_remove(state, -2);
    lua_getref(state, instance.context_ref);
    int argument_count = 1;
    if (pass_delta) {
        lua_pushnumber(state, delta_time);
        ++argument_count;
    }
    if (lua_pcall(state, argument_count, 0, 0) != LUA_OK) {
        set_error(instance.entity, stack_error(state, std::string(callback) + " failed"));
        lua_settop(state, 0);
        return false;
    }
    lua_settop(state, 0);
    return true;
}

void ScriptRuntime::update(float delta_time) {
    if (!m_state || !m_entity_manager) return;
    for (auto& instance : m_instances) {
        const auto status_found = m_statuses.find(instance.entity);
        if (status_found != m_statuses.end() && status_found->second.state == ScriptExecutionState::Error) continue;
        if (!m_entity_manager->is_alive(instance.entity) ||
            !m_entity_manager->has_component<ScriptComponent>(instance.entity)) continue;
        const auto& component = m_entity_manager->get_component<ScriptComponent>(instance.entity);
        if (component.enabled) {
            synchronize_public_fields(instance, component);
            static_cast<void>(call(instance, "on_update", delta_time, true));
        }
    }
}

void ScriptRuntime::release_instance(Instance& instance) {
    if (!m_state) return;
    if (instance.context_ref != k_invalid_reference) lua_unref(m_state, instance.context_ref);
    if (instance.module_ref != k_invalid_reference) lua_unref(m_state, instance.module_ref);
    if (instance.thread_ref != k_invalid_reference) lua_unref(m_state, instance.thread_ref);
    instance = {};
}

void ScriptRuntime::shutdown() {
    if (m_state) {
        for (auto& instance : m_instances) {
            if (instance.created) static_cast<void>(call(instance, "on_destroy", 0.0f, false));
            release_instance(instance);
        }
        lua_close(m_state);
    }
    m_state = nullptr;
    m_entity_manager = nullptr;
    m_instances.clear();
}

void ScriptRuntime::set_error(EntityID entity, std::string message) {
    m_statuses[entity] = {ScriptExecutionState::Error, std::move(message)};
    ENGINE_LOG_ERROR("[SCRIPT] Entity {}: {}", entity, m_statuses[entity].message);
}

const ScriptExecutionStatus* ScriptRuntime::status(EntityID entity) const {
    const auto found = m_statuses.find(entity);
    return found == m_statuses.end() ? nullptr : &found->second;
}

} // namespace Engine
