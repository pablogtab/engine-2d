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

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace Engine {

struct ScriptBindingContext {
    EntityManager* entity_manager{nullptr};
    EntityID entity{0};
    const AssetDatabase* asset_database{nullptr};
    ScriptRuntimeServices* services{nullptr};
    std::function<void(EntityID, std::string, std::string)> emit_event;
};

ScriptRuntime::Instance::~Instance() = default;
ScriptRuntime::Instance::Instance(Instance&&) noexcept = default;
ScriptRuntime::Instance& ScriptRuntime::Instance::operator=(Instance&&) noexcept = default;

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

ScriptBindingContext* bound_context(lua_State* state) {
    return static_cast<ScriptBindingContext*>(lua_touserdata(state, lua_upvalueindex(1)));
}

EntityManager* bound_entity_manager(lua_State* state) {
    const ScriptBindingContext* context = bound_context(state);
    return context ? context->entity_manager : nullptr;
}

EntityID bound_entity(lua_State* state) {
    const ScriptBindingContext* context = bound_context(state);
    return context ? context->entity : 0;
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

std::optional<AssetHandle> checked_asset_handle(lua_State* state, int index) {
    if (!lua_isstring(state, index)) return std::nullopt;
    return AssetHandle::from_string(lua_tostring(state, index));
}

const AssetRecord* ready_asset(lua_State* state, int index, AssetType expected) {
    ScriptBindingContext* context = bound_context(state);
    const auto handle = checked_asset_handle(state, index);
    if (!context || !context->asset_database || !handle) return nullptr;
    const AssetRecord* record = context->asset_database->find(*handle);
    if (!record || record->status != AssetStatus::Ready) return nullptr;
    if (expected != AssetType::Unknown && record->type != expected) return nullptr;
    return record;
}

int push_service_result(lua_State* state, bool success, const std::string& error) {
    lua_pushboolean(state, success);
    if (!success) {
        lua_pushstring(state, error.c_str());
        return 2;
    }
    return 1;
}

int raise_script_error(lua_State* state, const char* message) {
    luaL_error(state, "%s", message);
    return 0;
}

bool valid_volume(float value) {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

std::optional<std::uint64_t> source_signature(
    const AssetDatabase& database,
    AssetHandle handle
) {
    const AssetRecord* record = database.find(handle);
    if (!record) return std::nullopt;
    const std::filesystem::path path = database.project_root() / record->source_path;
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return std::nullopt;
    const std::uint64_t size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;
    const auto ticks = static_cast<std::uint64_t>(modified.time_since_epoch().count());
    return ticks ^ (size + 0x9e3779b97f4a7c15ull + (ticks << 6u) + (ticks >> 2u));
}

std::uint64_t monotonic_milliseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count());
}

int play_sound(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    const AssetRecord* record = ready_asset(state, 1, AssetType::Audio);
    const float volume = static_cast<float>(luaL_optnumber(state, 2, 1.0));
    const float pitch = static_cast<float>(luaL_optnumber(state, 3, 1.0));
    if (!record) return push_service_result(state, false, "audio asset is missing or unavailable");
    if (!valid_volume(volume) || !std::isfinite(pitch) || pitch <= 0.0f) {
        return push_service_result(state, false, "volume must be within 0..1 and pitch must be positive");
    }
    if (!context->services || !context->services->play_sound) {
        return push_service_result(state, false, "audio service is unavailable");
    }
    std::string error;
    return push_service_result(state, context->services->play_sound(
        context->asset_database->resolve(record->handle), volume, pitch, error
    ), error);
}

int play_music(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    const AssetRecord* record = ready_asset(state, 1, AssetType::Audio);
    const float volume = static_cast<float>(luaL_optnumber(state, 2, 0.5));
    const bool loop = lua_gettop(state) < 3 || lua_toboolean(state, 3) != 0;
    if (!record) return push_service_result(state, false, "audio asset is missing or unavailable");
    if (!valid_volume(volume)) return push_service_result(state, false, "volume must be within 0..1");
    if (!context->services || !context->services->play_music) {
        return push_service_result(state, false, "audio service is unavailable");
    }
    std::string error;
    return push_service_result(state, context->services->play_music(
        context->asset_database->resolve(record->handle), volume, loop, error
    ), error);
}

int stop_music(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    if (!context || !context->services || !context->services->stop_music) {
        return raise_script_error(state, "audio service is unavailable");
    }
    context->services->stop_music();
    return 0;
}

int pause_music(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    if (!context || !context->services || !context->services->pause_music) {
        return raise_script_error(state, "audio service is unavailable");
    }
    context->services->pause_music();
    return 0;
}

int resume_music(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    if (!context || !context->services || !context->services->resume_music) {
        return raise_script_error(state, "audio service is unavailable");
    }
    context->services->resume_music();
    return 0;
}

int set_volume(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    const std::string_view channel = luaL_checkstring(state, 1);
    const float volume = static_cast<float>(luaL_checknumber(state, 2));
    if (!valid_volume(volume)) return raise_script_error(state, "volume must be within 0..1");
    if (!context || !context->services) return raise_script_error(state, "audio service is unavailable");
    if (channel == "master") {
        if (!context->services->set_master_volume) return raise_script_error(state, "audio service is unavailable");
        context->services->set_master_volume(volume);
    } else if (channel == "music") {
        if (!context->services->set_music_volume) return raise_script_error(state, "audio service is unavailable");
        context->services->set_music_volume(volume);
    } else if (channel == "sfx") {
        if (!context->services->set_sfx_volume) return raise_script_error(state, "audio service is unavailable");
        context->services->set_sfx_volume(volume);
    } else {
        return raise_script_error(state, "audio channel must be 'master', 'music', or 'sfx'");
    }
    return 0;
}

int get_volume(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    const std::string_view channel = luaL_checkstring(state, 1);
    if (!context || !context->services) return raise_script_error(state, "audio service is unavailable");
    if (channel == "master") {
        if (!context->services->get_master_volume) return raise_script_error(state, "audio service is unavailable");
        lua_pushnumber(state, context->services->get_master_volume());
    } else if (channel == "music") {
        if (!context->services->get_music_volume) return raise_script_error(state, "audio service is unavailable");
        lua_pushnumber(state, context->services->get_music_volume());
    } else if (channel == "sfx") {
        if (!context->services->get_sfx_volume) return raise_script_error(state, "audio service is unavailable");
        lua_pushnumber(state, context->services->get_sfx_volume());
    } else {
        return raise_script_error(state, "audio channel must be 'master', 'music', or 'sfx'");
    }
    return 1;
}

int find_asset(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    const AssetRecord* record = context && context->asset_database
        ? context->asset_database->find_by_path(luaL_checkstring(state, 1)) : nullptr;
    if (!record || record->status != AssetStatus::Ready) lua_pushnil(state);
    else lua_pushstring(state, record->handle.to_string().c_str());
    return 1;
}

int asset_exists(lua_State* state) {
    lua_pushboolean(state, ready_asset(state, 1, AssetType::Unknown) != nullptr);
    return 1;
}

int asset_path(lua_State* state) {
    const AssetRecord* record = ready_asset(state, 1, AssetType::Unknown);
    if (record) lua_pushstring(state, record->source_path.c_str());
    else lua_pushnil(state);
    return 1;
}

int asset_type(lua_State* state) {
    const AssetRecord* record = ready_asset(state, 1, AssetType::Unknown);
    if (record) lua_pushstring(state, AssetDatabase::type_name(record->type));
    else lua_pushnil(state);
    return 1;
}

const char* window_mode_name(WindowMode mode) {
    switch (mode) {
        case WindowMode::Windowed: return "windowed";
        case WindowMode::BorderlessFullscreen: return "borderless_fullscreen";
        case WindowMode::ExclusiveFullscreen: return "exclusive_fullscreen";
    }
    return "windowed";
}

std::optional<WindowMode> window_mode_from_name(std::string_view name) {
    if (name == "windowed") return WindowMode::Windowed;
    if (name == "borderless_fullscreen") return WindowMode::BorderlessFullscreen;
    if (name == "exclusive_fullscreen") return WindowMode::ExclusiveFullscreen;
    return std::nullopt;
}

int get_display_info(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    if (!context || !context->services || !context->services->query_display) {
        return raise_script_error(state, "display service is unavailable");
    }
    const ScriptDisplayInfo info = context->services->query_display();
    lua_createtable(state, 0, 9);
    lua_pushinteger(state, info.pixel_width); lua_setfield(state, -2, "width");
    lua_pushinteger(state, info.pixel_height); lua_setfield(state, -2, "height");
    lua_pushnumber(state, info.dpi_scale); lua_setfield(state, -2, "dpi_scale");
    lua_pushinteger(state, info.reference_width); lua_setfield(state, -2, "reference_width");
    lua_pushinteger(state, info.reference_height); lua_setfield(state, -2, "reference_height");
    lua_pushstring(state, window_mode_name(info.window_mode)); lua_setfield(state, -2, "window_mode");
    lua_pushboolean(state, info.vsync); lua_setfield(state, -2, "vsync");
    lua_createtable(state, 0, 4);
    lua_pushnumber(state, info.safe_area.left); lua_setfield(state, -2, "left");
    lua_pushnumber(state, info.safe_area.top); lua_setfield(state, -2, "top");
    lua_pushnumber(state, info.safe_area.right); lua_setfield(state, -2, "right");
    lua_pushnumber(state, info.safe_area.bottom); lua_setfield(state, -2, "bottom");
    lua_setfield(state, -2, "safe_area");
    return 1;
}

int set_window_mode(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    const auto mode = window_mode_from_name(luaL_checkstring(state, 1));
    if (!mode) return push_service_result(state, false, "invalid window mode");
    if (!context || !context->services || !context->services->apply_display) {
        return push_service_result(state, false, "display service is unavailable");
    }
    std::string error;
    return push_service_result(state, context->services->apply_display({.window_mode = mode}, error), error);
}

int set_vsync(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    if (!context || !context->services || !context->services->apply_display) {
        return push_service_result(state, false, "display service is unavailable");
    }
    std::string error;
    return push_service_result(state, context->services->apply_display(
        {.vsync = lua_toboolean(state, 1) != 0}, error
    ), error);
}

int load_scene(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    const AssetRecord* record = ready_asset(state, 1, AssetType::Scene);
    if (!record) return push_service_result(state, false, "scene asset is missing or unavailable");
    if (!context->services || !context->services->request_scene_load) {
        return push_service_result(state, false, "scene loading service is unavailable");
    }
    std::string error;
    return push_service_result(state, context->services->request_scene_load(record->source_path, error), error);
}

int get_scene_name(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    if (!context || !context->services || !context->services->current_scene_name) {
        return raise_script_error(state, "scene service is unavailable");
    }
    lua_pushstring(state, context->services->current_scene_name().c_str());
    return 1;
}

int emit_event(lua_State* state) {
    ScriptBindingContext* context = bound_context(state);
    if (!context || !context->emit_event) return raise_script_error(state, "event service is unavailable");
    const std::string name = luaL_checkstring(state, 1);
    if (name.empty()) return raise_script_error(state, "event name cannot be empty");
    const std::string payload = luaL_optstring(state, 2, "");
    context->emit_event(context->entity, name, payload);
    return 0;
}

void push_bound_function(
    lua_State* state,
    ScriptBindingContext* context,
    lua_CFunction function,
    const char* name
) {
    lua_pushlightuserdata(state, context);
    lua_pushcclosure(state, function, name, 1);
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

ScriptRuntime::ScriptRuntime(ScriptRuntime&& other) noexcept {
    *this = std::move(other);
}

ScriptRuntime& ScriptRuntime::operator=(ScriptRuntime&& other) noexcept {
    if (this == &other) return *this;
    shutdown();

    EventDispatcher* dispatcher = other.m_event_dispatcher;
    other.unsubscribe_from_events();
    m_state = other.m_state;
    m_entity_manager = other.m_entity_manager;
    m_asset_database = other.m_asset_database;
    m_event_dispatcher = dispatcher;
    m_services = std::move(other.m_services);
    m_instances = std::move(other.m_instances);
    m_statuses = std::move(other.m_statuses);
    m_source_signatures = std::move(other.m_source_signatures);
    m_reload_errors = std::move(other.m_reload_errors);
    m_next_hot_reload_poll_ms = other.m_next_hot_reload_poll_ms;
    m_pending_events = std::move(other.m_pending_events);

    other.m_state = nullptr;
    other.m_entity_manager = nullptr;
    other.m_asset_database = nullptr;
    other.m_services = {};
    other.m_instances.clear();
    other.m_statuses.clear();
    other.m_source_signatures.clear();
    other.m_reload_errors.clear();
    other.m_next_hot_reload_poll_ms = 0;
    other.m_pending_events.clear();

    for (Instance& instance : m_instances) {
        instance.binding_context->services = &m_services;
        instance.binding_context->emit_event = [this](EntityID source, std::string name, std::string payload) {
            queue_custom_event(source, std::move(name), std::move(payload));
        };
    }
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
    m_state = luaL_newstate();
    if (!m_state) {
        m_entity_manager = nullptr;
        m_asset_database = nullptr;
        m_event_dispatcher = nullptr;
        return {};
    }
    luaL_openlibs(m_state);
    luaL_sandbox(m_state);
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
    if (m_services.hot_reload_enabled) {
        for (const Instance& instance : m_instances) {
            m_source_signatures.insert_or_assign(
                instance.script_handle,
                source_signature(asset_database, instance.script_handle)
            );
        }
    }
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
    instance.binding_context = std::make_unique<ScriptBindingContext>();
    instance.binding_context->entity_manager = m_entity_manager;
    instance.binding_context->entity = entity;
    instance.binding_context->asset_database = m_asset_database;
    instance.binding_context->services = &m_services;
    instance.binding_context->emit_event = [this](EntityID source, std::string name, std::string payload) {
        queue_custom_event(source, std::move(name), std::move(payload));
    };
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

    lua_createtable(instance.thread, 0, 32);
    lua_pushinteger(instance.thread, entity);
    lua_setfield(instance.thread, -2, "entity");

    lua_createtable(instance.thread, 0, static_cast<int>(field_definitions.size()));
    for (const ScriptPublicField& definition : field_definitions) {
        const ScriptPublicField* value = find_override(component, definition);
        push_field_value(instance.thread, value ? *value : definition);
        lua_setfield(instance.thread, -2, definition.name.c_str());
    }
    lua_setfield(instance.thread, -2, "fields");

    ScriptBindingContext* binding = instance.binding_context.get();
    push_bound_function(instance.thread, binding, get_position, "get_position");
    lua_setfield(instance.thread, -2, "get_position");
    push_bound_function(instance.thread, binding, set_position, "set_position");
    lua_setfield(instance.thread, -2, "set_position");
    push_bound_function(instance.thread, binding, get_axis, "get_axis");
    lua_setfield(instance.thread, -2, "get_axis");
    push_bound_function(instance.thread, binding, is_action_pressed, "is_action_pressed");
    lua_setfield(instance.thread, -2, "is_action_pressed");
    push_bound_function(instance.thread, binding, is_action_just_pressed, "is_action_just_pressed");
    lua_setfield(instance.thread, -2, "is_action_just_pressed");
    push_bound_function(instance.thread, binding, is_action_just_released, "is_action_just_released");
    lua_setfield(instance.thread, -2, "is_action_just_released");
    push_bound_function(instance.thread, binding, get_velocity, "get_velocity");
    lua_setfield(instance.thread, -2, "get_velocity");
    push_bound_function(instance.thread, binding, set_velocity, "set_velocity");
    lua_setfield(instance.thread, -2, "set_velocity");
    push_bound_function(instance.thread, binding, is_grounded, "is_grounded");
    lua_setfield(instance.thread, -2, "is_grounded");
    push_bound_function(instance.thread, binding, set_sprite_flip_x, "set_sprite_flip_x");
    lua_setfield(instance.thread, -2, "set_sprite_flip_x");
    push_bound_function(instance.thread, binding, play_animation, "play_animation");
    lua_setfield(instance.thread, -2, "play_animation");
    push_bound_function(instance.thread, binding, play_sound, "play_sound");
    lua_setfield(instance.thread, -2, "play_sound");
    push_bound_function(instance.thread, binding, play_music, "play_music");
    lua_setfield(instance.thread, -2, "play_music");
    push_bound_function(instance.thread, binding, stop_music, "stop_music");
    lua_setfield(instance.thread, -2, "stop_music");
    push_bound_function(instance.thread, binding, pause_music, "pause_music");
    lua_setfield(instance.thread, -2, "pause_music");
    push_bound_function(instance.thread, binding, resume_music, "resume_music");
    lua_setfield(instance.thread, -2, "resume_music");
    push_bound_function(instance.thread, binding, set_volume, "set_volume");
    lua_setfield(instance.thread, -2, "set_volume");
    push_bound_function(instance.thread, binding, get_volume, "get_volume");
    lua_setfield(instance.thread, -2, "get_volume");
    push_bound_function(instance.thread, binding, find_asset, "find_asset");
    lua_setfield(instance.thread, -2, "find_asset");
    push_bound_function(instance.thread, binding, asset_exists, "asset_exists");
    lua_setfield(instance.thread, -2, "asset_exists");
    push_bound_function(instance.thread, binding, asset_path, "asset_path");
    lua_setfield(instance.thread, -2, "asset_path");
    push_bound_function(instance.thread, binding, asset_type, "asset_type");
    lua_setfield(instance.thread, -2, "asset_type");
    push_bound_function(instance.thread, binding, get_display_info, "get_display_info");
    lua_setfield(instance.thread, -2, "get_display_info");
    push_bound_function(instance.thread, binding, set_window_mode, "set_window_mode");
    lua_setfield(instance.thread, -2, "set_window_mode");
    push_bound_function(instance.thread, binding, set_vsync, "set_vsync");
    lua_setfield(instance.thread, -2, "set_vsync");
    push_bound_function(instance.thread, binding, load_scene, "load_scene");
    lua_setfield(instance.thread, -2, "load_scene");
    push_bound_function(instance.thread, binding, get_scene_name, "get_scene_name");
    lua_setfield(instance.thread, -2, "get_scene_name");
    push_bound_function(instance.thread, binding, emit_event, "emit_event");
    lua_setfield(instance.thread, -2, "emit_event");

    instance.context_ref = lua_ref(instance.thread, -1);
    lua_pop(instance.thread, 1);
    m_instances.push_back(std::move(instance));
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

bool ScriptRuntime::call_event(Instance& instance, const PendingEvent& event) {
    const char* callback = nullptr;
    switch (event.type) {
        case PendingEventType::CollisionEnter: callback = "on_collision_enter"; break;
        case PendingEventType::CollisionExit: callback = "on_collision_exit"; break;
        case PendingEventType::ButtonClicked: callback = "on_ui_button_clicked"; break;
        case PendingEventType::FocusChanged: callback = "on_ui_focus_changed"; break;
        case PendingEventType::Custom: callback = "on_event"; break;
    }

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
    switch (event.type) {
        case PendingEventType::CollisionEnter:
            lua_pushinteger(state, event.entity_b);
            lua_pushnumber(state, event.normal_x);
            lua_pushnumber(state, event.normal_y);
            lua_pushboolean(state, event.is_trigger);
            argument_count += 4;
            break;
        case PendingEventType::CollisionExit:
            lua_pushinteger(state, event.entity_b);
            lua_pushboolean(state, event.is_trigger);
            argument_count += 2;
            break;
        case PendingEventType::ButtonClicked:
            lua_pushinteger(state, event.entity_a);
            ++argument_count;
            break;
        case PendingEventType::FocusChanged:
            lua_pushinteger(state, event.entity_a);
            lua_pushinteger(state, event.entity_b);
            argument_count += 2;
            break;
        case PendingEventType::Custom:
            lua_pushstring(state, event.name.c_str());
            lua_pushstring(state, event.payload.c_str());
            lua_pushinteger(state, event.entity_a);
            argument_count += 3;
            break;
    }
    if (lua_pcall(state, argument_count, 0, 0) != LUA_OK) {
        set_error(instance.entity, stack_error(state, std::string(callback) + " failed"));
        lua_settop(state, 0);
        return false;
    }
    lua_settop(state, 0);
    return true;
}

void ScriptRuntime::subscribe_to_events() {
    if (!m_event_dispatcher) return;
    m_event_subscriptions.push_back(m_event_dispatcher->subscribe<CollisionEvent>(
        [this](const CollisionEvent& event) {
            m_pending_events.push_back({
                .type = PendingEventType::CollisionEnter,
                .entity_a = event.entity_a,
                .entity_b = event.entity_b,
                .normal_x = event.contact_normal.x,
                .normal_y = event.contact_normal.y,
                .is_trigger = event.is_trigger
            });
        }
    ));
    m_event_subscriptions.push_back(m_event_dispatcher->subscribe<CollisionEndEvent>(
        [this](const CollisionEndEvent& event) {
            m_pending_events.push_back({
                .type = PendingEventType::CollisionExit,
                .entity_a = event.entity_a,
                .entity_b = event.entity_b,
                .is_trigger = event.is_trigger
            });
        }
    ));
    m_event_subscriptions.push_back(m_event_dispatcher->subscribe<UIButtonClickedEvent>(
        [this](const UIButtonClickedEvent& event) {
            m_pending_events.push_back({
                .type = PendingEventType::ButtonClicked,
                .entity_a = event.entity
            });
        }
    ));
    m_event_subscriptions.push_back(m_event_dispatcher->subscribe<UIFocusChangedEvent>(
        [this](const UIFocusChangedEvent& event) {
            m_pending_events.push_back({
                .type = PendingEventType::FocusChanged,
                .entity_a = event.previous_entity,
                .entity_b = event.focused_entity
            });
        }
    ));
}

void ScriptRuntime::unsubscribe_from_events() {
    if (m_event_dispatcher) {
        for (const std::uint64_t subscription : m_event_subscriptions) {
            static_cast<void>(m_event_dispatcher->unsubscribe(subscription));
        }
    }
    m_event_subscriptions.clear();
    m_event_dispatcher = nullptr;
}

void ScriptRuntime::queue_custom_event(EntityID source, std::string name, std::string payload) {
    m_pending_events.push_back({
        .type = PendingEventType::Custom,
        .entity_a = source,
        .name = std::move(name),
        .payload = std::move(payload)
    });
}

void ScriptRuntime::dispatch_events() {
    if (!m_state || !m_entity_manager || m_pending_events.empty()) return;
    std::vector<PendingEvent> events;
    events.swap(m_pending_events);
    for (const PendingEvent& event : events) {
        for (Instance& instance : m_instances) {
            const auto status_found = m_statuses.find(instance.entity);
            if (status_found != m_statuses.end() &&
                status_found->second.state == ScriptExecutionState::Error) continue;
            if (!m_entity_manager->is_alive(instance.entity) ||
                !m_entity_manager->has_component<ScriptComponent>(instance.entity) ||
                !m_entity_manager->get_component<ScriptComponent>(instance.entity).enabled) continue;

            PendingEvent delivered = event;
            if (event.type == PendingEventType::CollisionEnter ||
                event.type == PendingEventType::CollisionExit) {
                if (instance.entity == event.entity_a) {
                    delivered.entity_b = event.entity_b;
                } else if (instance.entity == event.entity_b) {
                    delivered.entity_b = event.entity_a;
                    delivered.normal_x = -event.normal_x;
                    delivered.normal_y = -event.normal_y;
                } else {
                    continue;
                }
            }
            static_cast<void>(call_event(instance, delivered));
        }
    }
}

ScriptHotReloadResult ScriptRuntime::reload_changed_scripts(bool force) {
    ScriptHotReloadResult result;
    if (!m_state || !m_entity_manager || !m_asset_database || !m_services.hot_reload_enabled) {
        return result;
    }

    if (!force) {
        const std::uint64_t now = monotonic_milliseconds();
        if (now < m_next_hot_reload_poll_ms) return result;
        m_next_hot_reload_poll_ms = now + 250;
    }

    std::vector<AssetHandle> handles;
    handles.reserve(m_instances.size());
    std::unordered_set<AssetHandle> seen_handles;
    for (const Instance& instance : m_instances) {
        if (seen_handles.insert(instance.script_handle).second) {
            handles.push_back(instance.script_handle);
        }
    }

    for (const AssetHandle handle : handles) {
        const std::optional<std::uint64_t> signature = source_signature(*m_asset_database, handle);
        const auto observed = m_source_signatures.find(handle);
        if (!force && observed != m_source_signatures.end() && observed->second == signature) continue;
        m_source_signatures.insert_or_assign(handle, signature);

        std::vector<std::size_t> matching_instances;
        for (std::size_t index = 0; index < m_instances.size(); ++index) {
            if (m_instances[index].script_handle == handle) matching_instances.push_back(index);
        }
        if (matching_instances.empty()) continue;

        const CompiledAsset compiled = compile_asset(handle, *m_asset_database);
        if (!compiled.success) {
            result.failed += matching_instances.size();
            m_reload_errors.insert_or_assign(handle, compiled.error);
            ENGINE_LOG_ERROR(
                "[SCRIPT RELOAD] Kept previous instances for asset {}: {}",
                handle.to_string(),
                compiled.error
            );
            continue;
        }

        std::string reload_error_message;
        for (const std::size_t index : matching_instances) {
            if (index >= m_instances.size()) continue;
            const EntityID entity = m_instances[index].entity;
            if (!m_entity_manager->is_alive(entity) ||
                !m_entity_manager->has_component<ScriptComponent>(entity)) continue;

            const ScriptComponent& component =
                m_entity_manager->get_component<ScriptComponent>(entity);
            const auto previous_status = m_statuses.find(entity);
            const std::optional<ScriptExecutionStatus> saved_status = previous_status == m_statuses.end()
                ? std::nullopt : std::optional<ScriptExecutionStatus>{previous_status->second};
            std::string instance_error;
            if (!create_instance(
                    entity,
                    component,
                    compiled.bytecode,
                    compiled.chunk_name,
                    compiled.fields,
                    instance_error
                )) {
                if (saved_status) m_statuses.insert_or_assign(entity, *saved_status);
                else m_statuses.erase(entity);
                ++result.failed;
                if (reload_error_message.empty()) {
                    reload_error_message = "entity " + std::to_string(entity) + ": " + instance_error;
                }
                continue;
            }

            Instance replacement = std::move(m_instances.back());
            m_instances.pop_back();
            Instance previous = std::move(m_instances[index]);
            m_instances[index] = std::move(replacement);
            if (previous.created) static_cast<void>(call(previous, "on_destroy", 0.0f, false));
            release_instance(previous);
            m_statuses.insert_or_assign(entity, ScriptExecutionStatus{});
            ++result.reloaded;
        }

        if (reload_error_message.empty()) {
            m_reload_errors.erase(handle);
            ENGINE_LOG_INFO(
                "[SCRIPT RELOAD] Reloaded asset {} for {} instances",
                handle.to_string(),
                matching_instances.size()
            );
        } else {
            m_reload_errors.insert_or_assign(handle, reload_error_message);
            ENGINE_LOG_ERROR(
                "[SCRIPT RELOAD] Some instances kept their previous version for asset {}: {}",
                handle.to_string(),
                reload_error_message
            );
        }
    }
    return result;
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
    unsubscribe_from_events();
    if (m_state) {
        for (auto& instance : m_instances) {
            if (instance.created) static_cast<void>(call(instance, "on_destroy", 0.0f, false));
            release_instance(instance);
        }
        lua_close(m_state);
    }
    m_state = nullptr;
    m_entity_manager = nullptr;
    m_asset_database = nullptr;
    m_services = {};
    m_instances.clear();
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
