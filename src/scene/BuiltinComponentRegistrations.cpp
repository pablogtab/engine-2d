#include "BuiltinComponentRegistrations.hpp"

#include "ComponentRegistry.hpp"
#include "../core/AssetDatabase.hpp"
#include "../core/AssetManager.hpp"
#include "../ecs/components/AnimationComponents.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/ParticleComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../ecs/components/ScriptingComponents.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../renderer/SpriteAnimationSystem.hpp"
#include "../tilemap/Tilemap.hpp"
#include "../tilemap/TilemapColliderSystem.hpp"
#include "../ui/UIComponentRegistrations.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Engine {
namespace {

using Json = nlohmann::json;

template<typename Component>
void register_or_throw(ComponentRegistry& registry, TypedComponentRegistration<Component> registration) {
    std::string error;
    if (!registry.register_component<Component>(std::move(registration), &error)) {
        throw std::runtime_error(error);
    }
}

const char* projection_to_string(CameraProjectionMode mode) {
    switch (mode) {
        case CameraProjectionMode::FixedHeight: return "fixed_height";
        case CameraProjectionMode::FixedWidth: return "fixed_width";
        case CameraProjectionMode::Expand: return "expand";
        case CameraProjectionMode::PixelPerfect: return "pixel_perfect";
    }
    return "fixed_height";
}

bool projection_from_string(std::string_view value, CameraProjectionMode& result) {
    if (value == "fixed_height") result = CameraProjectionMode::FixedHeight;
    else if (value == "fixed_width") result = CameraProjectionMode::FixedWidth;
    else if (value == "expand") result = CameraProjectionMode::Expand;
    else if (value == "pixel_perfect") result = CameraProjectionMode::PixelPerfect;
    else return false;
    return true;
}

const char* light_type_to_string(Light2DType type) {
    switch (type) {
        case Light2DType::Ambient: return "Ambient";
        case Light2DType::Point: return "Point";
    }
    return "Point";
}

bool light_type_from_string(std::string_view value, Light2DType& result) {
    if (value == "Ambient") result = Light2DType::Ambient;
    else if (value == "Point") result = Light2DType::Point;
    else return false;
    return true;
}

const char* light_blend_to_string(Light2DBlendMode mode) {
    switch (mode) {
        case Light2DBlendMode::Additive: return "Additive";
        case Light2DBlendMode::Multiply: return "Multiply";
        case Light2DBlendMode::Screen: return "Screen";
    }
    return "Additive";
}

bool light_blend_from_string(std::string_view value, Light2DBlendMode& result) {
    if (value == "Additive") result = Light2DBlendMode::Additive;
    else if (value == "Multiply") result = Light2DBlendMode::Multiply;
    else if (value == "Screen") result = Light2DBlendMode::Screen;
    else return false;
    return true;
}

const char* shadow_source_to_string(ShadowGeometrySource source) {
    switch (source) {
        case ShadowGeometrySource::Collider: return "Collider";
        case ShadowGeometrySource::StaticPolygon: return "StaticPolygon";
    }
    return "Collider";
}

bool shadow_source_from_string(std::string_view value, ShadowGeometrySource& result) {
    if (value == "Collider") result = ShadowGeometrySource::Collider;
    else if (value == "StaticPolygon") result = ShadowGeometrySource::StaticPolygon;
    else return false;
    return true;
}

const char* body_type_to_string(BodyType type) {
    switch (type) {
        case BodyType::Dynamic: return "Dynamic";
        case BodyType::Static: return "Static";
        case BodyType::Kinematic: return "Kinematic";
    }
    return "Static";
}

bool body_type_from_string(std::string_view value, BodyType& result) {
    if (value == "Dynamic") result = BodyType::Dynamic;
    else if (value == "Static") result = BodyType::Static;
    else if (value == "Kinematic") result = BodyType::Kinematic;
    else return false;
    return true;
}

const char* collider_type_to_string(ColliderShapeType type) {
    switch (type) {
        case ColliderShapeType::Box: return "Box";
        case ColliderShapeType::Circle: return "Circle";
        case ColliderShapeType::Polygon: return "Polygon";
    }
    return "Box";
}

bool collider_type_from_string(std::string_view value, ColliderShapeType& result) {
    if (value == "Box") result = ColliderShapeType::Box;
    else if (value == "Circle") result = ColliderShapeType::Circle;
    else if (value == "Polygon") result = ColliderShapeType::Polygon;
    else return false;
    return true;
}

const char* script_field_type_to_string(ScriptFieldType type) {
    switch (type) {
        case ScriptFieldType::Boolean: return "boolean";
        case ScriptFieldType::Number: return "number";
        case ScriptFieldType::String: return "string";
        case ScriptFieldType::Vector2: return "vector2";
        case ScriptFieldType::Entity: return "entity";
        case ScriptFieldType::Asset: return "asset";
    }
    return "number";
}

bool script_field_type_from_string(std::string_view value, ScriptFieldType& result) {
    if (value == "boolean") result = ScriptFieldType::Boolean;
    else if (value == "number") result = ScriptFieldType::Number;
    else if (value == "string") result = ScriptFieldType::String;
    else if (value == "vector2") result = ScriptFieldType::Vector2;
    else if (value == "entity") result = ScriptFieldType::Entity;
    else if (value == "asset") result = ScriptFieldType::Asset;
    else return false;
    return true;
}

Json serialize_script_field_value(const ScriptPublicField& field) {
    switch (field.type) {
        case ScriptFieldType::Boolean: return std::get<bool>(field.value);
        case ScriptFieldType::Number: return std::get<double>(field.value);
        case ScriptFieldType::String: return std::get<std::string>(field.value);
        case ScriptFieldType::Vector2: {
            const ScriptVector2 value = std::get<ScriptVector2>(field.value);
            return Json::array({value.x, value.y});
        }
        case ScriptFieldType::Entity: return std::get<EntityID>(field.value);
        case ScriptFieldType::Asset: {
            const AssetHandle value = std::get<AssetHandle>(field.value);
            return value.is_valid() ? Json(value.to_string()) : Json(nullptr);
        }
    }
    return nullptr;
}

bool deserialize_script_field_value(
    ScriptFieldType type,
    const Json& value,
    ScriptFieldValue& result,
    std::string& error
) {
    switch (type) {
        case ScriptFieldType::Boolean:
            if (!value.is_boolean()) { error = "Script boolean field value is invalid"; return false; }
            result = value.get<bool>();
            return true;
        case ScriptFieldType::Number:
            if (!value.is_number()) { error = "Script number field value is invalid"; return false; }
            result = value.get<double>();
            return true;
        case ScriptFieldType::String:
            if (!value.is_string()) { error = "Script string field value is invalid"; return false; }
            result = value.get<std::string>();
            return true;
        case ScriptFieldType::Vector2:
            if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) {
                error = "Script vector2 field value must contain two numbers";
                return false;
            }
            result = ScriptVector2{value[0].get<float>(), value[1].get<float>()};
            return true;
        case ScriptFieldType::Entity:
            if (!value.is_number_unsigned()) { error = "Script entity field value is invalid"; return false; }
            result = value.get<EntityID>();
            return true;
        case ScriptFieldType::Asset:
            if (value.is_null()) { result = AssetHandle{}; return true; }
            if (!value.is_string()) { error = "Script asset field value is invalid"; return false; }
            if (const auto handle = AssetHandle::from_string(value.get<std::string>()); handle) {
                result = *handle;
                return true;
            }
            error = "Script asset field handle is invalid";
            return false;
    }
    error = "Script field type is invalid";
    return false;
}

void register_tag(ComponentRegistry& registry) {
    register_or_throw<TagComponent>(registry, {
        .display_name = "Tag",
        .serialized_key = "tag",
        .serialize = [](const TagComponent& tag, const ComponentSerializationContext&) {
            return Json(tag.tag);
        },
        .deserialize = [](TagComponent& tag, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_string()) {
                error = "Tag must be a string";
                return false;
            }
            tag.tag = value.get<std::string>();
            return true;
        },
        .validate = [](const TagComponent& tag) -> std::optional<std::string> {
            if (tag.tag.empty()) return "Tag cannot be empty";
            return std::nullopt;
        }
    });
}

void register_group(ComponentRegistry& registry) {
    register_or_throw<GroupComponent>(registry, {
        .display_name = "Group",
        .serialized_key = "group",
        .serialize = [](const GroupComponent&, const ComponentSerializationContext&) {
            return Json::object();
        },
        .deserialize = [](GroupComponent&, const Json&, const ComponentSerializationContext&, std::string&) {
            return true;
        }
    });
}

void register_render_effect(ComponentRegistry& registry) {
    register_or_throw<RenderEffectComponent>(registry, {
        .display_name = "Render Effect",
        .serialized_key = "render_effect",
        .serialize = [](const RenderEffectComponent& effect, const ComponentSerializationContext&) {
            Json value{{"enabled", effect.enabled}};
            if (effect.profile_handle.is_valid()) {
                value["profile_handle"] = effect.profile_handle.to_string();
            }
            return value;
        },
        .deserialize = [](
            RenderEffectComponent& effect,
            const Json& value,
            const ComponentSerializationContext&,
            std::string& error
        ) {
            if (!value.is_object()) {
                error = "Render Effect must be an object";
                return false;
            }
            effect = {};
            effect.enabled = value.value("enabled", true);
            if (!value.contains("profile_handle")) return true;
            if (!value["profile_handle"].is_string()) {
                error = "Render Effect profile_handle must be a string";
                return false;
            }
            const auto handle = AssetHandle::from_string(value["profile_handle"].get<std::string>());
            if (!handle || !handle->is_valid()) {
                error = "Render Effect profile_handle is invalid";
                return false;
            }
            effect.profile_handle = *handle;
            return true;
        }
    });
}

void register_light(ComponentRegistry& registry) {
    register_or_throw<Light2DComponent>(registry, {
        .display_name = "Light 2D",
        .serialized_key = "light_2d",
        .serialize = [](const Light2DComponent& light, const ComponentSerializationContext&) {
            return Json{
                {"type", light_type_to_string(light.type)},
                {"color", {light.color.r, light.color.g, light.color.b, light.color.a}},
                {"intensity", light.intensity},
                {"radius", light.radius},
                {"falloff", light.falloff},
                {"blend_mode", light_blend_to_string(light.blend_mode)},
                {"affected_layers", light.affected_layers},
                {"casts_shadows", light.casts_shadows},
                {"enabled", light.enabled}
            };
        },
        .deserialize = [](Light2DComponent& light, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object()) { error = "Light 2D must be an object"; return false; }
            const auto& color = value.at("color");
            if (!color.is_array() || color.size() != 4) {
                error = "Light 2D color must contain four channels";
                return false;
            }
            if (!light_type_from_string(value.at("type").get<std::string>(), light.type)) {
                error = "Light 2D type is invalid";
                return false;
            }
            if (!light_blend_from_string(value.at("blend_mode").get<std::string>(), light.blend_mode)) {
                error = "Light 2D blend mode is invalid";
                return false;
            }
            light.color = {
                color[0].get<std::uint8_t>(), color[1].get<std::uint8_t>(),
                color[2].get<std::uint8_t>(), color[3].get<std::uint8_t>()
            };
            light.intensity = value.at("intensity").get<float>();
            light.radius = value.at("radius").get<float>();
            light.falloff = value.value("falloff", 1.0f);
            light.affected_layers = value.at("affected_layers").get<RenderLayerMask>();
            light.casts_shadows = value.at("casts_shadows").get<bool>();
            light.enabled = value.at("enabled").get<bool>();
            return true;
        },
        .validate = [](const Light2DComponent& light) -> std::optional<std::string> {
            if (!std::isfinite(light.intensity) || light.intensity < 0.0f || light.intensity > 1.0f) {
                return "Light intensity must be between 0 and 1";
            }
            if (!std::isfinite(light.radius) || (light.type == Light2DType::Point && light.radius <= 0.0f)) {
                return "Point light radius must be positive";
            }
            if (!std::isfinite(light.falloff) || light.falloff < 0.1f || light.falloff > 8.0f) {
                return "Light falloff must be between 0.1 and 8";
            }
            if (light.affected_layers == 0) return "Light must affect at least one render layer";
            return std::nullopt;
        }
    });
}

void register_shadow_caster(ComponentRegistry& registry) {
    register_or_throw<ShadowCaster2DComponent>(registry, {
        .display_name = "Shadow Caster 2D",
        .serialized_key = "shadow_caster_2d",
        .serialize = [](const ShadowCaster2DComponent& caster, const ComponentSerializationContext&) {
            Json vertices = Json::array();
            for (const glm::vec2& vertex : caster.vertices) vertices.push_back({vertex.x, vertex.y});
            return Json{
                {"source", shadow_source_to_string(caster.source)},
                {"vertices", std::move(vertices)},
                {"affected_layers", caster.affected_layers},
                {"enabled", caster.enabled}
            };
        },
        .deserialize = [](ShadowCaster2DComponent& caster, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object()) { error = "Shadow Caster 2D must be an object"; return false; }
            if (!shadow_source_from_string(value.at("source").get<std::string>(), caster.source)) {
                error = "Shadow caster source is invalid";
                return false;
            }
            const auto& vertices = value.at("vertices");
            if (!vertices.is_array()) { error = "Shadow caster vertices must be an array"; return false; }
            caster.vertices.clear();
            caster.vertices.reserve(vertices.size());
            for (const auto& vertex : vertices) {
                if (!vertex.is_array() || vertex.size() != 2) {
                    error = "Shadow caster vertex must contain two coordinates";
                    return false;
                }
                caster.vertices.emplace_back(vertex[0].get<float>(), vertex[1].get<float>());
            }
            caster.affected_layers = value.at("affected_layers").get<RenderLayerMask>();
            caster.enabled = value.at("enabled").get<bool>();
            return true;
        },
        .validate = [](const ShadowCaster2DComponent& caster) -> std::optional<std::string> {
            if (caster.affected_layers == 0) return "Shadow caster must affect at least one render layer";
            if (caster.source == ShadowGeometrySource::StaticPolygon && caster.vertices.size() < 3) {
                return "Static shadow caster requires at least three vertices";
            }
            for (const glm::vec2& vertex : caster.vertices) {
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y)) {
                    return "Shadow caster vertices must be finite";
                }
            }
            return std::nullopt;
        }
    });
}

void register_prefab_link(ComponentRegistry& registry) {
    register_or_throw<PrefabLinkComponent>(registry, {
        .display_name = "Prefab Link",
        .serialized_key = "prefab_link",
        .serialize = [](const PrefabLinkComponent& link, const ComponentSerializationContext& context) {
            if (!context.asset_database) {
                throw std::runtime_error("Prefab Link serialization requires an AssetDatabase");
            }
            const AssetRecord* prefab = context.asset_database->find_by_path(link.prefab_asset_path);
            if (!prefab || prefab->status != AssetStatus::Ready || prefab->type != AssetType::Prefab) {
                throw std::runtime_error("Prefab Link references an unregistered prefab: " + link.prefab_asset_path);
            }
            Json j{{"asset_handle", prefab->handle.to_string()}};
            if (!link.base_prefab_path.empty()) {
                const AssetRecord* base = context.asset_database->find_by_path(link.base_prefab_path);
                if (!base || base->status != AssetStatus::Ready || base->type != AssetType::Prefab) {
                    throw std::runtime_error("Prefab Link references an unregistered base prefab: " + link.base_prefab_path);
                }
                j["base_asset_handle"] = base->handle.to_string();
            }
            j["source_local_id"] = link.source_local_id;
            j["is_root_instance"] = link.is_root_instance;
            return j;
        },
        .deserialize = [](PrefabLinkComponent& link, const Json& value, const ComponentSerializationContext& context, std::string& error) {
            if (!value.is_object()) {
                error = "Prefab link must be an object";
                return false;
            }
            if (!context.asset_database) {
                error = "Prefab link deserialization requires an AssetDatabase";
                return false;
            }
            const auto resolve = [&](const char* key, std::string& destination, bool required) {
                if (!value.contains(key)) {
                    if (required) error = std::string("Prefab link is missing ") + key;
                    return !required;
                }
                if (!value[key].is_string()) {
                    error = std::string(key) + " must be a string";
                    return false;
                }
                const auto handle = AssetHandle::from_string(value[key].get<std::string>());
                const AssetRecord* record = handle ? context.asset_database->find(*handle) : nullptr;
                if (!record || record->status != AssetStatus::Ready || record->type != AssetType::Prefab) {
                    error = std::string(key) + " does not resolve to a ready prefab";
                    return false;
                }
                destination = record->source_path;
                return true;
            };
            if (!resolve("asset_handle", link.prefab_asset_path, true) ||
                !resolve("base_asset_handle", link.base_prefab_path, false)) return false;
            link.source_local_id = value.value("source_local_id", EntityID{1});
            link.is_root_instance = value.value("is_root_instance", true);
            return true;
        }
    });
}


void register_camera(ComponentRegistry& registry) {
    register_or_throw<CameraComponent>(registry, {
        .display_name = "Camera 2D",
        .serialized_key = "camera",
        .serialize = [](const CameraComponent& camera, const ComponentSerializationContext&) {
            Json layer_effects = Json::array();
            for (const auto& effect : camera.post_process.layer_effects) {
                layer_effects.push_back({
                    {"effect_handle", effect.effect_handle.to_string()},
                    {"render_layer", static_cast<unsigned>(effect.render_layer)},
                    {"enabled", effect.enabled}
                });
            }
            return Json{
                {"is_primary", camera.is_primary},
                {"clear_color", {camera.clear_color.r, camera.clear_color.g, camera.clear_color.b, camera.clear_color.a}},
                {"zoom", camera.zoom},
                {"projection", projection_to_string(camera.projection)},
                {"follow_target", camera.follow_target},
                {"follow_offset", {camera.follow_offset.x, camera.follow_offset.y}},
                {"follow_speed", camera.follow_speed},
                {"enable_bounds", camera.enable_bounds},
                {"bounds", {camera.bounds.x, camera.bounds.y, camera.bounds.z, camera.bounds.w}},
                {"render_layer_mask", camera.render_layer_mask},
                {"post_process", {
                    {"enabled", camera.post_process.enabled},
                    {"global_profile_handle", camera.post_process.global_profile_handle.to_string()},
                    {"layer_effects", std::move(layer_effects)}
                }}
            };
        },
        .deserialize = [](CameraComponent& camera, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object()) { error = "Camera must be an object"; return false; }
            const auto& offset = value.at("follow_offset");
            const auto& bounds = value.at("bounds");
            const auto& clear_color = value.at("clear_color");
            if (!offset.is_array() || offset.size() != 2 || !bounds.is_array() || bounds.size() != 4 ||
                !clear_color.is_array() || clear_color.size() != 4) {
                error = "Camera vectors have invalid dimensions";
                return false;
            }
            if (!projection_from_string(value.at("projection").get<std::string>(), camera.projection)) {
                error = "Camera projection mode is invalid";
                return false;
            }
            camera.is_primary = value.at("is_primary").get<bool>();
            camera.clear_color = {
                clear_color[0].get<std::uint8_t>(), clear_color[1].get<std::uint8_t>(),
                clear_color[2].get<std::uint8_t>(), clear_color[3].get<std::uint8_t>()
            };
            camera.zoom = value.at("zoom").get<float>();
            camera.follow_target = value.at("follow_target").get<EntityID>();
            camera.follow_offset = {offset[0].get<float>(), offset[1].get<float>()};
            camera.follow_speed = value.at("follow_speed").get<float>();
            camera.enable_bounds = value.at("enable_bounds").get<bool>();
            camera.bounds = {bounds[0].get<float>(), bounds[1].get<float>(), bounds[2].get<float>(), bounds[3].get<float>()};
            camera.render_layer_mask = value.value("render_layer_mask", ALL_RENDER_LAYERS);
            if (value.contains("post_process") && value.at("post_process").is_object()) {
                const auto& pp = value.at("post_process");
                camera.post_process.enabled = pp.value("enabled", true);
                if (pp.contains("global_profile_handle") && pp.at("global_profile_handle").is_string()) {
                    camera.post_process.global_profile_handle = AssetHandle::from_string(pp.at("global_profile_handle").get<std::string>()).value_or(AssetHandle{});
                }
                if (pp.contains("layer_effects") && pp.at("layer_effects").is_array()) {
                    camera.post_process.layer_effects.clear();
                    for (const auto& item : pp.at("layer_effects")) {
                        if (!item.is_object()) continue;
                        CameraLayerEffectAssignment assignment;
                        if (item.contains("effect_handle") && item.at("effect_handle").is_string()) {
                            assignment.effect_handle = AssetHandle::from_string(item.at("effect_handle").get<std::string>()).value_or(AssetHandle{});
                        }
                        assignment.render_layer = static_cast<RenderLayer>(item.value("render_layer", 0u));
                        assignment.enabled = item.value("enabled", true);
                        camera.post_process.layer_effects.push_back(assignment);
                    }
                }
            }
            return true;
        },
        .finalize = [](EntityManager& em, EntityID, CameraComponent& camera, const ComponentFinalizeContext&, std::string& error) {
            if (camera.follow_target != 0 && !em.is_alive(camera.follow_target)) {
                error = "Camera follow target does not exist: " + std::to_string(camera.follow_target);
                return false;
            }
            return true;
        },
        .remap_references = [](CameraComponent& camera, const std::unordered_map<EntityID, EntityID>& old_to_new) {
            if (camera.follow_target != 0) {
                auto it = old_to_new.find(camera.follow_target);
                if (it != old_to_new.end()) {
                    camera.follow_target = it->second;
                } else {
                    camera.follow_target = 0;
                }
            }
        },
        .validate = [](const CameraComponent& camera) -> std::optional<std::string> {
            if (camera.zoom <= 0.0f) return "Zoom must be positive";
            if (camera.follow_speed < 0.0f) return "Follow speed cannot be negative";
            return std::nullopt;
        }
    });
}

void register_animation(ComponentRegistry& registry) {
    register_or_throw<AnimationComponent>(registry, {
        .display_name = "Sprite Animator 2D",
        .serialized_key = "animation",
        .serialize = [](const AnimationComponent& animation, const ComponentSerializationContext&) {
            Json clips = Json::array();
            for (const auto& clip : animation.clips) {
                Json value{
                    {"name", clip.name}, {"start_frame", clip.start_frame},
                    {"frame_count", clip.frame_count}, {"fps", clip.fps}, {"loop", clip.loop}
                };
                if (!clip.custom_frames.empty()) {
                    value["custom_frames"] = Json::array();
                    for (const auto& frame : clip.custom_frames) {
                        value["custom_frames"].push_back({
                            {"x", frame.x}, {"y", frame.y}, {"w", frame.w}, {"h", frame.h}
                        });
                    }
                }
                clips.push_back(std::move(value));
            }
            return Json{
                {"columns", animation.columns}, {"rows", animation.rows},
                {"frame_width", animation.frame_width}, {"frame_height", animation.frame_height},
                {"spacing_x", animation.spacing_x}, {"spacing_y", animation.spacing_y},
                {"margin_x", animation.margin_x}, {"margin_y", animation.margin_y},
                {"default_clip_index", animation.default_clip_index},
                {"play_on_start", animation.play_on_start}, {"clips", std::move(clips)}
            };
        },
        .deserialize = [](AnimationComponent& animation, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object() || !value.at("clips").is_array()) {
                error = "Animation must contain a clips array";
                return false;
            }
            animation.columns = value.at("columns").get<int>();
            animation.rows = value.at("rows").get<int>();
            animation.frame_width = value.at("frame_width").get<float>();
            animation.frame_height = value.at("frame_height").get<float>();
            animation.spacing_x = value.at("spacing_x").get<float>();
            animation.spacing_y = value.at("spacing_y").get<float>();
            animation.margin_x = value.at("margin_x").get<float>();
            animation.margin_y = value.at("margin_y").get<float>();
            animation.default_clip_index = value.at("default_clip_index").get<int>();
            animation.play_on_start = value.at("play_on_start").get<bool>();
            for (const auto& clip_value : value.at("clips")) {
                AnimationClip clip(
                    clip_value.at("name").get<std::string>(),
                    clip_value.at("start_frame").get<int>(),
                    clip_value.at("frame_count").get<int>(),
                    clip_value.at("fps").get<float>(),
                    clip_value.at("loop").get<bool>()
                );
                if (clip_value.contains("custom_frames")) {
                    for (const auto& frame : clip_value.at("custom_frames")) {
                        clip.custom_frames.push_back({
                            frame.at("x").get<float>(), frame.at("y").get<float>(),
                            frame.at("w").get<float>(), frame.at("h").get<float>()
                        });
                    }
                }
                animation.clips.push_back(std::move(clip));
            }
            return true;
        },
        .finalize = [](EntityManager& em, EntityID entity, AnimationComponent&, const ComponentFinalizeContext&, std::string&) {
            SpriteAnimationSystem::reset_playback(em, entity);
            return true;
        },
        .after_remove = [](EntityManager& em, EntityID entity) {
            SpriteAnimationSystem::reset_playback(em, entity);
        },
        .validate = [](const AnimationComponent& animation) -> std::optional<std::string> {
            if (animation.columns <= 0 || animation.rows <= 0) return "Grid dimensions must be positive";
            if (animation.frame_width <= 0.0f || animation.frame_height <= 0.0f) return "Frame dimensions must be positive";
            if (!animation.clips.empty() &&
                (animation.default_clip_index < 0 || animation.default_clip_index >= static_cast<int>(animation.clips.size()))) {
                return "Default clip index is out of range";
            }
            return std::nullopt;
        }
    });
}

void register_rigidbody(ComponentRegistry& registry) {
    register_or_throw<RigidBodyComponent>(registry, {
        .display_name = "RigidBody 2D",
        .serialized_key = "rigidbody",
        .serialize = [](const RigidBodyComponent& body, const ComponentSerializationContext&) {
            return Json{
                {"type", body_type_to_string(body.body_type)}, {"fixed_rotation", body.fixed_rotation},
                {"gravity_scale", body.gravity_scale}, {"mass", body.mass},
                {"friction", body.friction}, {"restitution", body.restitution}
            };
        },
        .deserialize = [](RigidBodyComponent& body, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object()) { error = "RigidBody must be an object"; return false; }
            if (!body_type_from_string(value.at("type").get<std::string>(), body.body_type)) {
                error = "RigidBody type is invalid";
                return false;
            }
            body.fixed_rotation = value.at("fixed_rotation").get<bool>();
            body.gravity_scale = value.at("gravity_scale").get<float>();
            body.mass = value.at("mass").get<float>();
            body.friction = value.at("friction").get<float>();
            body.restitution = value.at("restitution").get<float>();
            return true;
        },
        .finalize = [](EntityManager& em, EntityID entity, RigidBodyComponent&, const ComponentFinalizeContext& context, std::string&) {
            const bool individual_editor_change =
                context.reason == ComponentFinalizeReason::EditorComponentChange;
            if (context.reason != ComponentFinalizeReason::DeserializationStaging &&
                (individual_editor_change || !em.has_component<ColliderComponent>(entity))) {
                PhysicsSystem::rebuild_body(em, entity);
            }
            return true;
        },
        .after_remove = [](EntityManager& em, EntityID entity) { PhysicsSystem::rebuild_body(em, entity); },
        .validate = [](const RigidBodyComponent& body) -> std::optional<std::string> {
            if (body.body_type == BodyType::Dynamic && body.mass <= 0.0f) return "Dynamic body mass must be positive";
            if (body.friction < 0.0f || body.restitution < 0.0f) return "Physics coefficients cannot be negative";
            return std::nullopt;
        }
    });
}

void register_collider(ComponentRegistry& registry) {
    register_or_throw<ColliderComponent>(registry, {
        .display_name = "Collider 2D",
        .serialized_key = "collider",
        .serialize = [](const ColliderComponent& collider, const ComponentSerializationContext&) {
            Json shapes = Json::array();
            for (const auto& shape : collider.shapes) {
                Json value{
                    {"type", collider_type_to_string(shape.type)},
                    {"offset", {shape.offset.x, shape.offset.y}},
                    {"size", {shape.size.x, shape.size.y}}, {"radius", shape.radius},
                    {"is_trigger", shape.is_trigger}, {"is_one_way", shape.is_one_way},
                    {"category_bits", shape.category_bits}, {"mask_bits", shape.mask_bits}
                };
                if (!shape.vertices.empty()) {
                    value["vertices"] = Json::array();
                    for (const auto& vertex : shape.vertices) value["vertices"].push_back({vertex.x, vertex.y});
                }
                shapes.push_back(std::move(value));
            }
            return shapes;
        },
        .deserialize = [](ColliderComponent& collider, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_array()) { error = "Collider must be an array"; return false; }
            for (const auto& shape_value : value) {
                ColliderShape shape;
                if (!collider_type_from_string(shape_value.at("type").get<std::string>(), shape.type)) {
                    error = "Collider shape type is invalid";
                    return false;
                }
                const auto& offset = shape_value.at("offset");
                const auto& size = shape_value.at("size");
                if (!offset.is_array() || offset.size() != 2 || !size.is_array() || size.size() != 2) {
                    error = "Collider vectors have invalid dimensions";
                    return false;
                }
                shape.offset = {offset[0].get<float>(), offset[1].get<float>()};
                shape.size = {size[0].get<float>(), size[1].get<float>()};
                shape.radius = shape_value.at("radius").get<float>();
                shape.is_trigger = shape_value.at("is_trigger").get<bool>();
                shape.is_one_way = shape_value.at("is_one_way").get<bool>();
                shape.category_bits = shape_value.at("category_bits").get<std::uint16_t>();
                shape.mask_bits = shape_value.at("mask_bits").get<std::uint16_t>();
                if (shape_value.contains("vertices")) {
                    for (const auto& vertex : shape_value.at("vertices")) {
                        if (!vertex.is_array() || vertex.size() != 2) { error = "Collider vertex is invalid"; return false; }
                        shape.vertices.emplace_back(vertex[0].get<float>(), vertex[1].get<float>());
                    }
                }
                collider.shapes.push_back(std::move(shape));
            }
            return true;
        },
        .finalize = [](EntityManager& em, EntityID entity, ColliderComponent&, const ComponentFinalizeContext& context, std::string&) {
            if (context.reason != ComponentFinalizeReason::DeserializationStaging) {
                PhysicsSystem::rebuild_body(em, entity);
            }
            return true;
        },
        .after_remove = [](EntityManager& em, EntityID entity) { PhysicsSystem::rebuild_body(em, entity); },
        .validate = [](const ColliderComponent& collider) -> std::optional<std::string> {
            for (const auto& shape : collider.shapes) {
                if (shape.type == ColliderShapeType::Box && (shape.size.x <= 0.0f || shape.size.y <= 0.0f)) return "Box size must be positive";
                if (shape.type == ColliderShapeType::Circle && shape.radius <= 0.0f) return "Circle radius must be positive";
                if (shape.type == ColliderShapeType::Polygon && shape.vertices.size() < 3) return "Polygon requires at least three vertices";
            }
            return std::nullopt;
        }
    });
}

void register_tilemap(ComponentRegistry& registry) {
    register_or_throw<TilemapComponent>(registry, {
        .display_name = "Tilemap 2D",
        .serialized_key = "tilemap",
        .serialize = [](const TilemapComponent& tilemap, const ComponentSerializationContext&) {
            std::vector<std::pair<std::int64_t, std::uint16_t>> sorted_tiles(tilemap.tiles.begin(), tilemap.tiles.end());
            std::sort(sorted_tiles.begin(), sorted_tiles.end());
            Json tiles = Json::array();
            for (const auto& [key, tile_id] : sorted_tiles) {
                int x = 0;
                int y = 0;
                TilemapComponent::parse_key(key, x, y);
                tiles.push_back({{"x", x}, {"y", y}, {"id", tile_id}});
            }
            return Json{
                {"tile_size", tilemap.tile_size}, {"tile_pixel_size", tilemap.tile_pixel_size},
                {"tileset_path", tilemap.tileset_path}, {"generate_colliders", tilemap.generate_colliders},
                {"is_trigger", tilemap.is_trigger}, {"is_one_way", tilemap.is_one_way},
                {"category_bits", tilemap.category_bits}, {"mask_bits", tilemap.mask_bits},
                {"friction", tilemap.friction}, {"restitution", tilemap.restitution},
                {"render_layer", tilemap.render_layer},
                {"tiles", std::move(tiles)}
            };
        },
        .deserialize = [](TilemapComponent& tilemap, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object() || !value.at("tiles").is_array()) {
                error = "Tilemap must contain a tiles array";
                return false;
            }
            tilemap.tile_size = value.at("tile_size").get<float>();
            tilemap.tile_pixel_size = value.at("tile_pixel_size").get<int>();
            tilemap.tileset_path = value.at("tileset_path").get<std::string>();
            tilemap.generate_colliders = value.at("generate_colliders").get<bool>();
            tilemap.is_trigger = value.at("is_trigger").get<bool>();
            tilemap.is_one_way = value.at("is_one_way").get<bool>();
            tilemap.category_bits = value.at("category_bits").get<std::uint16_t>();
            tilemap.mask_bits = value.at("mask_bits").get<std::uint16_t>();
            tilemap.friction = value.at("friction").get<float>();
            tilemap.restitution = value.at("restitution").get<float>();
            tilemap.render_layer = value.value("render_layer", DEFAULT_RENDER_LAYER);
            for (const auto& tile : value.at("tiles")) {
                tilemap.set_tile(tile.at("x").get<int>(), tile.at("y").get<int>(), tile.at("id").get<std::uint16_t>());
            }
            return true;
        },
        .finalize = [](EntityManager& em, EntityID entity, TilemapComponent& tilemap, const ComponentFinalizeContext& context, std::string&) {
            if (context.serialization.renderer && context.serialization.asset_manager && !tilemap.tileset_path.empty()) {
                tilemap.tileset_texture = context.serialization.asset_manager->get_texture(
                    context.serialization.renderer,
                    tilemap.tileset_path
                );
            }
            if (context.reason != ComponentFinalizeReason::DeserializationCommit) {
                TilemapColliderSystem::build_composite_colliders(em, entity);
            }
            return true;
        },
        .after_remove = [](EntityManager& em, EntityID entity) {
            TilemapColliderSystem::clear_composite_colliders(em, entity);
        },
        .validate = [](const TilemapComponent& tilemap) -> std::optional<std::string> {
            if (tilemap.tile_size <= 0.0f || tilemap.tile_pixel_size <= 0) return "Tile dimensions must be positive";
            return std::nullopt;
        }
    });
}

void register_script(ComponentRegistry& registry) {
    register_or_throw<ScriptComponent>(registry, {
        .display_name = "Script",
        .serialized_key = "script",
        .serialize = [](const ScriptComponent& script, const ComponentSerializationContext&) {
            Json fields = Json::array();
            for (const ScriptPublicField& field : script.public_fields) {
                if (!script_field_value_matches_type(field)) {
                    throw std::runtime_error("Script field type does not match its stored value: " + field.name);
                }
                fields.push_back({
                    {"name", field.name},
                    {"type", script_field_type_to_string(field.type)},
                    {"value", serialize_script_field_value(field)}
                });
            }
            Json value{{"enabled", script.enabled}, {"public_fields", std::move(fields)}};
            if (script.script_handle.is_valid()) value["script_handle"] = script.script_handle.to_string();
            return value;
        },
        .deserialize = [](ScriptComponent& script, const Json& value, const ComponentSerializationContext&, std::string& error) {
            if (!value.is_object() || !value.contains("public_fields") || !value["public_fields"].is_array()) {
                error = "Script must contain a public_fields array";
                return false;
            }
            script = {};
            script.enabled = value.value("enabled", true);
            if (value.contains("script_handle")) {
                if (!value["script_handle"].is_string()) {
                    error = "Script script_handle must be a string";
                    return false;
                }
                const auto handle = AssetHandle::from_string(value["script_handle"].get<std::string>());
                if (!handle) { error = "Script script_handle is invalid"; return false; }
                script.script_handle = *handle;
            }
            for (const Json& field_value : value["public_fields"]) {
                if (!field_value.is_object() || !field_value.contains("name") ||
                    !field_value["name"].is_string() || !field_value.contains("type") ||
                    !field_value["type"].is_string() || !field_value.contains("value")) {
                    error = "Script public field is invalid";
                    return false;
                }
                ScriptPublicField field;
                field.name = field_value["name"].get<std::string>();
                if (field.name.empty() || !script_field_type_from_string(
                        field_value["type"].get<std::string>(), field.type
                    ) || !deserialize_script_field_value(
                        field.type, field_value["value"], field.value, error
                    )) return false;
                const bool duplicate = std::any_of(
                    script.public_fields.begin(), script.public_fields.end(),
                    [&field](const ScriptPublicField& existing) { return existing.name == field.name; }
                );
                if (duplicate) { error = "Script public field names must be unique"; return false; }
                script.public_fields.push_back(std::move(field));
            }
            return true;
        },
        .remap_references = [](ScriptComponent& script, const std::unordered_map<EntityID, EntityID>& old_to_new) {
            for (ScriptPublicField& field : script.public_fields) {
                if (field.type != ScriptFieldType::Entity || !std::holds_alternative<EntityID>(field.value)) continue;
                const EntityID old_id = std::get<EntityID>(field.value);
                const auto replacement = old_to_new.find(old_id);
                if (replacement != old_to_new.end()) field.value = replacement->second;
            }
        },
        .validate = [](const ScriptComponent& script) -> std::optional<std::string> {
            if (!script.script_handle.is_valid()) return "Script asset is not assigned";
            std::vector<std::string> names;
            for (const ScriptPublicField& field : script.public_fields) {
                if (field.name.empty()) return "Script public field name cannot be empty";
                if (!script_field_value_matches_type(field)) return "Script public field type does not match its value";
                if (std::find(names.begin(), names.end(), field.name) != names.end()) {
                    return "Script public field names must be unique";
                }
                names.push_back(field.name);
            }
            return std::nullopt;
        }
    });
}

void register_particle_emitter(ComponentRegistry& registry) {
    register_or_throw<ParticleEmitter2DComponent>(registry, {
        .display_name = "Particle Emitter 2D",
        .serialized_key = "particle_emitter_2d",
        .schema_version = 1,
        .required = false,
        .serialize = [](const ParticleEmitter2DComponent& emitter, const ComponentSerializationContext&) {
            Json json = Json::object();
            if (emitter.effect_handle.is_valid()) {
                json["effect_handle"] = emitter.effect_handle.to_string();
            }
            json["active"] = emitter.active;
            json["play_on_awake"] = emitter.play_on_awake;
            json["local_space"] = emitter.local_space;
            if (emitter.emit_rate_override.has_value()) {
                json["emit_rate_override"] = *emitter.emit_rate_override;
            }
            if (emitter.burst_count_override.has_value()) {
                json["burst_count_override"] = *emitter.burst_count_override;
            }
            return json;
        },
        .deserialize = [](
            ParticleEmitter2DComponent& emitter,
            const Json& json,
            const ComponentSerializationContext&,
            std::string& error
        ) {
            if (!json.is_object()) {
                error = "Particle emitter component must be an object";
                return false;
            }
            emitter = ParticleEmitter2DComponent{};
            if (json.contains("effect_handle")) {
                if (!json["effect_handle"].is_string()) {
                    error = "Particle emitter effect_handle must be a string";
                    return false;
                }
                const auto handle = AssetHandle::from_string(json["effect_handle"].get<std::string>());
                if (!handle) {
                    error = "Particle emitter effect_handle is invalid";
                    return false;
                }
                emitter.effect_handle = *handle;
            }
            try {
                emitter.active = json.value("active", true);
                emitter.play_on_awake = json.value("play_on_awake", true);
                emitter.local_space = json.value("local_space", false);
                if (json.contains("emit_rate_override") && !json["emit_rate_override"].is_null()) {
                    emitter.emit_rate_override = json["emit_rate_override"].get<float>();
                    if (!std::isfinite(*emitter.emit_rate_override) || *emitter.emit_rate_override < 0.0f) {
                        error = "Particle emitter rate override must be finite and non-negative";
                        return false;
                    }
                }
                if (json.contains("burst_count_override") && !json["burst_count_override"].is_null()) {
                    emitter.burst_count_override = json["burst_count_override"].get<int>();
                    if (*emitter.burst_count_override < 0) {
                        error = "Particle emitter burst override must be non-negative";
                        return false;
                    }
                }
            } catch (const Json::exception&) {
                error = "Particle emitter contains a field with an invalid type";
                return false;
            }
            return true;
        },
        .validate = [](const ParticleEmitter2DComponent& emitter) -> std::optional<std::string> {
            if (!emitter.effect_handle.is_valid()) return "Particle effect asset is not assigned";
            if (emitter.emit_rate_override &&
                (!std::isfinite(*emitter.emit_rate_override) || *emitter.emit_rate_override < 0.0f)) {
                return "Particle emitter rate override must be finite and non-negative";
            }
            if (emitter.burst_count_override && *emitter.burst_count_override < 0) {
                return "Particle emitter burst override must be non-negative";
            }
            return std::nullopt;
        }
    });
}

} // namespace

void register_additional_builtin_components(ComponentRegistry& registry) {
    register_tag(registry);
    register_group(registry);
    register_render_effect(registry);
    register_light(registry);
    register_shadow_caster(registry);
    register_prefab_link(registry);
    register_camera(registry);
    register_animation(registry);
    register_rigidbody(registry);
    register_collider(registry);
    register_tilemap(registry);
    register_script(registry);
    register_particle_emitter(registry);
    register_ui_components(registry);
}

} // namespace Engine
