#include "SceneSerializer.hpp"
#include "ComponentRegistry.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Log.hpp"
#include "../ecs/Components.hpp"
#include "../tilemap/Tilemap.hpp"
#include "../tilemap/TilemapColliderSystem.hpp"
#include "../core/AssetManager.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Engine {

namespace {

const char* projection_to_string(CameraProjectionMode mode) {
    switch (mode) {
        case CameraProjectionMode::FixedHeight: return "fixed_height";
        case CameraProjectionMode::FixedWidth: return "fixed_width";
        case CameraProjectionMode::Expand: return "expand";
        case CameraProjectionMode::PixelPerfect: return "pixel_perfect";
    }
    return "fixed_height";
}

CameraProjectionMode projection_from_string(const std::string& value) {
    if (value == "fixed_width") return CameraProjectionMode::FixedWidth;
    if (value == "expand") return CameraProjectionMode::Expand;
    if (value == "pixel_perfect") return CameraProjectionMode::PixelPerfect;
    return CameraProjectionMode::FixedHeight;
}

} // namespace

SceneSerializer::SceneSerializer(Scene* scene, SDL_Renderer* renderer)
    : m_scene(scene), m_renderer(renderer) {}

std::string SceneSerializer::serialize_to_string() {
    if (!m_scene) {
        ENGINE_LOG_ERROR("[SERIALIZER] Cannot serialize: Scene pointer is null");
        return "";
    }

    json root;
    root["scene_name"] = m_scene->get_name();
    root["version"] = 1;

    json entities_array = json::array();
    EntityManager& em = m_scene->get_entity_manager();
    const ComponentSerializationContext component_context{m_renderer, &AssetManager::get()};
    std::string component_error;
    bool component_serialization_failed = false;

    em.for_each<TransformComponent>([&](EntityID id, const TransformComponent& transform) {
        (void)transform;
        if (component_serialization_failed) return;
        if (!em.is_alive(id)) return;

        // Tilemap colliders are derived runtime data and are rebuilt after load.
        if (em.has_component<TilemapGeneratedColliderComponent>(id)) return;

        json entity_json;
        entity_json["id"] = id;

        // TagComponent
        if (em.has_component<TagComponent>(id)) {
            auto& tag = em.get_component<TagComponent>(id);
            entity_json["tag"] = tag.tag;
        } else {
            entity_json["tag"] = "Entity_" + std::to_string(id);
        }

        if (!default_component_registry().serialize_entity(
                em, id, entity_json, component_context, component_error
            )) {
            component_serialization_failed = true;
            return;
        }

        // Store only the authoritative parent relation. The children list is
        // derived by EntityManager::set_parent() while loading, which avoids
        // duplicated or stale hierarchy data in scene files and snapshots.
        if (em.has_component<HierarchyComponent>(id)) {
            const auto& hierarchy = em.get_component<HierarchyComponent>(id);
            entity_json["hierarchy"] = {{"parent", hierarchy.parent}};
        }

        // TilemapComponent
        if (em.has_component<TilemapComponent>(id)) {
            auto& tm = em.get_component<TilemapComponent>(id);
            json tm_json;
            tm_json["tile_size"] = tm.tile_size;
            tm_json["tile_pixel_size"] = tm.tile_pixel_size;
            tm_json["tileset_path"] = tm.tileset_path;
            tm_json["generate_colliders"] = tm.generate_colliders;
            tm_json["is_trigger"] = tm.is_trigger;
            tm_json["is_one_way"] = tm.is_one_way;
            tm_json["category_bits"] = tm.category_bits;
            tm_json["mask_bits"] = tm.mask_bits;
            tm_json["friction"] = tm.friction;
            tm_json["restitution"] = tm.restitution;

            json tiles_arr = json::array();
            for (const auto& [key, tile_id] : tm.tiles) {
                int x = 0, y = 0;
                TilemapComponent::parse_key(key, x, y);
                tiles_arr.push_back({ {"x", x}, {"y", y}, {"id", tile_id} });
            }
            tm_json["tiles"] = tiles_arr;
            entity_json["tilemap"] = tm_json;
        }

        // CameraComponent
        if (em.has_component<CameraComponent>(id)) {
            auto& cam = em.get_component<CameraComponent>(id);
            json cam_json;
            cam_json["is_primary"] = cam.is_primary;
            cam_json["zoom"] = cam.zoom;
            cam_json["projection"] = projection_to_string(cam.projection);
            cam_json["follow_target"] = cam.follow_target;
            cam_json["follow_offset"] = {cam.follow_offset.x, cam.follow_offset.y};
            cam_json["follow_speed"] = cam.follow_speed;
            cam_json["enable_bounds"] = cam.enable_bounds;
            cam_json["bounds"] = { cam.bounds.x, cam.bounds.y, cam.bounds.z, cam.bounds.w };
            entity_json["camera"] = cam_json;
        }

        // AnimationComponent
        if (em.has_component<AnimationComponent>(id)) {
            auto& anim = em.get_component<AnimationComponent>(id);
            json anim_json;
            anim_json["columns"] = anim.columns;
            anim_json["rows"] = anim.rows;
            anim_json["frame_width"] = anim.frame_width;
            anim_json["frame_height"] = anim.frame_height;
            anim_json["spacing_x"] = anim.spacing_x;
            anim_json["spacing_y"] = anim.spacing_y;
            anim_json["margin_x"] = anim.margin_x;
            anim_json["margin_y"] = anim.margin_y;
            anim_json["default_clip_index"] = anim.default_clip_index;
            anim_json["play_on_start"] = anim.play_on_start;

            json clips_arr = json::array();
            for (const auto& clip : anim.clips) {
                json c_j;
                c_j["name"] = clip.name;
                c_j["start_frame"] = clip.start_frame;
                c_j["frame_count"] = clip.frame_count;
                c_j["fps"] = clip.fps;
                c_j["loop"] = clip.loop;

                if (!clip.custom_frames.empty()) {
                    json frames_arr = json::array();
                    for (const auto& f : clip.custom_frames) {
                        frames_arr.push_back({ {"x", f.x}, {"y", f.y}, {"w", f.w}, {"h", f.h} });
                    }
                    c_j["custom_frames"] = frames_arr;
                }

                clips_arr.push_back(c_j);
            }
            anim_json["clips"] = clips_arr;
            entity_json["animation"] = anim_json;
        }

        // RigidBodyComponent
        if (em.has_component<RigidBodyComponent>(id)) {
            auto& rb = em.get_component<RigidBodyComponent>(id);
            json rb_json;
            rb_json["type"] = (rb.body_type == BodyType::Dynamic) ? "Dynamic" : (rb.body_type == BodyType::Kinematic ? "Kinematic" : "Static");
            rb_json["fixed_rotation"] = rb.fixed_rotation;
            rb_json["gravity_scale"] = rb.gravity_scale;
            rb_json["mass"] = rb.mass;
            rb_json["friction"] = rb.friction;
            rb_json["restitution"] = rb.restitution;
            entity_json["rigidbody"] = rb_json;
        }

        // ColliderComponent
        if (em.has_component<ColliderComponent>(id)) {
            auto& col = em.get_component<ColliderComponent>(id);
            json shapes_arr = json::array();
            for (const auto& shape : col.shapes) {
                json sh_json;
                sh_json["type"] = (shape.type == ColliderShapeType::Circle) ? "Circle" : (shape.type == ColliderShapeType::Polygon ? "Polygon" : "Box");
                sh_json["offset"] = { shape.offset.x, shape.offset.y };
                sh_json["size"] = { shape.size.x, shape.size.y };
                sh_json["radius"] = shape.radius;
                sh_json["is_trigger"] = shape.is_trigger;
                sh_json["is_one_way"] = shape.is_one_way;
                sh_json["category_bits"] = shape.category_bits;
                sh_json["mask_bits"] = shape.mask_bits;
                if (!shape.vertices.empty()) {
                    json verts = json::array();
                    for (const auto& v : shape.vertices) {
                        verts.push_back({ v.x, v.y });
                    }
                    sh_json["vertices"] = verts;
                }
                shapes_arr.push_back(sh_json);
            }
            entity_json["collider"] = shapes_arr;
        }

        entities_array.push_back(entity_json);
    });

    if (component_serialization_failed) {
        ENGINE_LOG_ERROR("[SERIALIZER] {}", component_error);
        return "";
    }

    root["entities"] = entities_array;
    return root.dump(4);
}

bool SceneSerializer::serialize(const std::string& filepath) {
    std::string json_text = serialize_to_string();
    if (json_text.empty()) return false;

    if (!FileSystem::write_text(filepath, json_text)) {
        ENGINE_LOG_ERROR("[SERIALIZER] Failed to write scene to '{}'", filepath);
        return false;
    }

    ENGINE_LOG_INFO("[SERIALIZER] Successfully saved scene '{}' to '{}'", m_scene->get_name(), filepath);
    return true;
}

bool SceneSerializer::deserialize_from_string(const std::string& json_text) {
    if (!m_scene) {
        ENGINE_LOG_ERROR("[SERIALIZER] Cannot deserialize: Scene pointer is null");
        return false;
    }

    if (json_text.empty()) {
        ENGINE_LOG_ERROR("[SERIALIZER] JSON text is empty");
        return false;
    }

    try {
        json root = json::parse(json_text);
        EntityManager& em = m_scene->get_entity_manager();
        em.clear();
        const ComponentSerializationContext component_context{m_renderer, &AssetManager::get()};
        std::vector<EntityID> tilemaps_to_rebuild;
        std::vector<std::pair<EntityID, EntityID>> hierarchy_links;

        if (root.contains("entities") && root["entities"].is_array()) {
            for (const auto& entity_json : root["entities"]) {
                const EntityID serialized_id = entity_json.value("id", EntityID{0});
                const EntityID id = serialized_id != 0
                    ? em.create_entity_with_id(serialized_id)
                    : em.create_entity();
                if (id == 0) {
                    ENGINE_LOG_ERROR("[SERIALIZER] Duplicate or invalid entity ID {}", serialized_id);
                    return false;
                }

                // TagComponent
                if (entity_json.contains("tag") && entity_json["tag"].is_string()) {
                    em.add_component<TagComponent>(id, TagComponent{ entity_json["tag"].get<std::string>() });
                }

                std::string component_error;
                if (!default_component_registry().deserialize_entity(
                        em, id, entity_json, component_context, component_error
                    )) {
                    ENGINE_LOG_ERROR("[SERIALIZER] Entity {}: {}", id, component_error);
                    return false;
                }

                // Entity references may point forward in the JSON array, so
                // create every entity before resolving any parent relation.
                if (entity_json.contains("hierarchy") && entity_json["hierarchy"].is_object()) {
                    const auto& hierarchy_json = entity_json["hierarchy"];
                    em.add_component<HierarchyComponent>(id, HierarchyComponent{});
                    hierarchy_links.emplace_back(id, hierarchy_json.value("parent", EntityID{0}));
                }

                // TilemapComponent
                if (entity_json.contains("tilemap") && entity_json["tilemap"].is_object()) {
                    const auto& tm_j = entity_json["tilemap"];
                    TilemapComponent tm;
                    if (tm_j.contains("tile_size")) tm.tile_size = tm_j["tile_size"].get<float>();
                    if (tm_j.contains("tile_pixel_size")) tm.tile_pixel_size = tm_j["tile_pixel_size"].get<int>();
                    if (tm_j.contains("tileset_path")) {
                        tm.tileset_path = tm_j["tileset_path"].get<std::string>();
                        if (m_renderer && !tm.tileset_path.empty()) {
                            tm.tileset_texture = Texture::load_from_file(m_renderer, tm.tileset_path);
                        }
                    }
                    if (tm_j.contains("generate_colliders")) tm.generate_colliders = tm_j["generate_colliders"].get<bool>();
                    if (tm_j.contains("is_trigger")) tm.is_trigger = tm_j["is_trigger"].get<bool>();
                    if (tm_j.contains("is_one_way")) tm.is_one_way = tm_j["is_one_way"].get<bool>();
                    if (tm_j.contains("category_bits")) tm.category_bits = tm_j["category_bits"].get<uint16_t>();
                    if (tm_j.contains("mask_bits")) tm.mask_bits = tm_j["mask_bits"].get<uint16_t>();
                    if (tm_j.contains("friction")) tm.friction = tm_j["friction"].get<float>();
                    if (tm_j.contains("restitution")) tm.restitution = tm_j["restitution"].get<float>();

                    if (tm_j.contains("tiles") && tm_j["tiles"].is_array()) {
                        for (const auto& t_item : tm_j["tiles"]) {
                            if (t_item.contains("x") && t_item.contains("y") && t_item.contains("id")) {
                                int tx = t_item["x"].get<int>();
                                int ty = t_item["y"].get<int>();
                                uint16_t tid = t_item["id"].get<uint16_t>();
                                tm.set_tile(tx, ty, tid);
                            }
                        }
                    }
                    em.add_component<TilemapComponent>(id, tm);
                    tilemaps_to_rebuild.push_back(id);
                }

                // CameraComponent
                if (entity_json.contains("camera") && entity_json["camera"].is_object()) {
                    const auto& c_j = entity_json["camera"];
                    CameraComponent cam;
                    if (c_j.contains("is_primary")) cam.is_primary = c_j["is_primary"].get<bool>();
                    if (c_j.contains("zoom")) cam.zoom = c_j["zoom"].get<float>();
                    if (c_j.contains("projection")) cam.projection = projection_from_string(c_j["projection"].get<std::string>());
                    if (c_j.contains("follow_target")) cam.follow_target = c_j["follow_target"].get<EntityID>();
                    if (c_j.contains("follow_offset") && c_j["follow_offset"].size() >= 2) {
                        cam.follow_offset = glm::vec2(c_j["follow_offset"][0].get<float>(), c_j["follow_offset"][1].get<float>());
                    }
                    if (c_j.contains("follow_speed")) cam.follow_speed = c_j["follow_speed"].get<float>();
                    if (c_j.contains("enable_bounds")) cam.enable_bounds = c_j["enable_bounds"].get<bool>();
                    if (c_j.contains("bounds") && c_j["bounds"].size() >= 4) {
                        cam.bounds = glm::vec4(c_j["bounds"][0].get<float>(), c_j["bounds"][1].get<float>(), c_j["bounds"][2].get<float>(), c_j["bounds"][3].get<float>());
                    }
                    em.add_component<CameraComponent>(id, cam);
                }

                // AnimationComponent
                if (entity_json.contains("animation") && entity_json["animation"].is_object()) {
                    const auto& a_j = entity_json["animation"];
                    AnimationComponent anim;
                    if (a_j.contains("columns")) anim.columns = a_j["columns"].get<int>();
                    if (a_j.contains("rows")) anim.rows = a_j["rows"].get<int>();
                    if (a_j.contains("frame_width")) anim.frame_width = a_j["frame_width"].get<float>();
                    if (a_j.contains("frame_height")) anim.frame_height = a_j["frame_height"].get<float>();
                    if (a_j.contains("spacing_x")) anim.spacing_x = a_j["spacing_x"].get<float>();
                    if (a_j.contains("spacing_y")) anim.spacing_y = a_j["spacing_y"].get<float>();
                    if (a_j.contains("margin_x")) anim.margin_x = a_j["margin_x"].get<float>();
                    if (a_j.contains("margin_y")) anim.margin_y = a_j["margin_y"].get<float>();
                    if (a_j.contains("default_clip_index")) anim.default_clip_index = a_j["default_clip_index"].get<int>();
                    if (a_j.contains("play_on_start")) anim.play_on_start = a_j["play_on_start"].get<bool>();

                    if (a_j.contains("clips") && a_j["clips"].is_array()) {
                        for (const auto& c_j : a_j["clips"]) {
                            std::string name = c_j.value("name", "Default");
                            int start = c_j.value("start_frame", 0);
                            int count = c_j.value("frame_count", 1);
                            float fps = c_j.value("fps", 10.0f);
                            bool loop = c_j.value("loop", true);
                            
                            AnimationClip clip(name, start, count, fps, loop);
                            if (c_j.contains("custom_frames") && c_j["custom_frames"].is_array()) {
                                for (const auto& f_j : c_j["custom_frames"]) {
                                    clip.custom_frames.push_back(SDL_FRect{
                                        f_j.value("x", 0.0f),
                                        f_j.value("y", 0.0f),
                                        f_j.value("w", 0.0f),
                                        f_j.value("h", 0.0f)
                                    });
                                }
                            }
                            anim.clips.push_back(clip);
                        }
                    }
                    em.add_component<AnimationComponent>(id, anim);
                }

                // RigidBodyComponent
                if (entity_json.contains("rigidbody")) {
                    const auto& rb_j = entity_json["rigidbody"];
                    RigidBodyComponent rb;
                    if (rb_j.contains("type") && rb_j["type"].is_string()) {
                        std::string t = rb_j["type"].get<std::string>();
                        if (t == "Dynamic") rb.body_type = BodyType::Dynamic;
                        else if (t == "Kinematic") rb.body_type = BodyType::Kinematic;
                        else rb.body_type = BodyType::Static;
                    }
                    if (rb_j.contains("mass")) rb.mass = rb_j["mass"].get<float>();
                    if (rb_j.contains("gravity_scale")) rb.gravity_scale = rb_j["gravity_scale"].get<float>();
                    if (rb_j.contains("friction")) rb.friction = rb_j["friction"].get<float>();
                    if (rb_j.contains("restitution")) rb.restitution = rb_j["restitution"].get<float>();
                    if (rb_j.contains("fixed_rotation")) rb.fixed_rotation = rb_j["fixed_rotation"].get<bool>();
                    em.add_component<RigidBodyComponent>(id, rb);
                }

                // ColliderComponent
                if (entity_json.contains("collider") && entity_json["collider"].is_array()) {
                    ColliderComponent cc;
                    for (const auto& sh_j : entity_json["collider"]) {
                        ColliderShape shape;
                        if (sh_j.contains("type") && sh_j["type"].is_string()) {
                            std::string t = sh_j["type"].get<std::string>();
                            if (t == "Circle") shape.type = ColliderShapeType::Circle;
                            else if (t == "Polygon") shape.type = ColliderShapeType::Polygon;
                            else shape.type = ColliderShapeType::Box;
                        }
                        if (sh_j.contains("offset") && sh_j["offset"].size() >= 2) {
                            shape.offset = glm::vec2(sh_j["offset"][0].get<float>(), sh_j["offset"][1].get<float>());
                        }
                        if (sh_j.contains("size") && sh_j["size"].size() >= 2) {
                            shape.size = glm::vec2(sh_j["size"][0].get<float>(), sh_j["size"][1].get<float>());
                        }
                        if (sh_j.contains("radius")) shape.radius = sh_j["radius"].get<float>();
                        if (sh_j.contains("is_trigger")) shape.is_trigger = sh_j["is_trigger"].get<bool>();
                        if (sh_j.contains("is_one_way")) shape.is_one_way = sh_j["is_one_way"].get<bool>();
                        if (sh_j.contains("category_bits")) shape.category_bits = sh_j["category_bits"].get<uint16_t>();
                        if (sh_j.contains("mask_bits")) shape.mask_bits = sh_j["mask_bits"].get<uint16_t>();
                        if (sh_j.contains("vertices") && sh_j["vertices"].is_array()) {
                            for (const auto& v_j : sh_j["vertices"]) {
                                if (v_j.size() >= 2) {
                                    shape.vertices.emplace_back(v_j[0].get<float>(), v_j[1].get<float>());
                                }
                            }
                        }
                        cc.shapes.push_back(shape);
                    }
                    em.add_component<ColliderComponent>(id, cc);
                }
            }

            // Rebuild children from the authoritative parent links only after
            // every serialized entity exists. This is also what makes the
            // Play/Stop snapshot restore entities destroyed during simulation
            // without losing their authoring hierarchy.
            for (const auto& [child, parent] : hierarchy_links) {
                if (parent == 0) continue;
                if (!em.is_alive(parent)) {
                    ENGINE_LOG_WARN(
                        "[SERIALIZER] Entity #{} references missing parent #{}; restored as root",
                        child,
                        parent
                    );
                    continue;
                }
                em.set_parent(child, parent);
            }

            // Generated entities are created only after every serialized ID
            // and hierarchy link has been restored, preventing collisions with
            // stable references.
            for (EntityID tilemap : tilemaps_to_rebuild) {
                TilemapColliderSystem::build_composite_colliders(em, tilemap);
            }
        }

        return true;
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[SERIALIZER] Exception parsing scene JSON: {}", e.what());
        return false;
    }
}

bool SceneSerializer::deserialize(const std::string& filepath) {
    std::string json_text = FileSystem::read_text(filepath);
    if (json_text.empty()) {
        ENGINE_LOG_ERROR("[SERIALIZER] Scene file not found or empty: '{}'", filepath);
        return false;
    }

    bool success = deserialize_from_string(json_text);
    if (success) {
        ENGINE_LOG_INFO("[SERIALIZER] Successfully loaded scene '{}' from '{}'", m_scene->get_name(), filepath);
    }
    return success;
}

} // namespace Engine
