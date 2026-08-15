#include "SceneSerializer.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Log.hpp"
#include "../ecs/Components.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Engine {

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

    // Iterate through all entities that have a TransformComponent
    em.for_each<TransformComponent>([&](EntityID id, TransformComponent& transform) {
        json entity_json;
        entity_json["id"] = id;

        // TagComponent
        if (em.has_component<TagComponent>(id)) {
            auto& tag = em.get_component<TagComponent>(id);
            entity_json["tag"] = tag.tag;
        } else {
            entity_json["tag"] = "Entity_" + std::to_string(id);
        }

        // TransformComponent
        json tf_json;
        tf_json["position"] = { transform.position.x, transform.position.y, transform.position.z };
        tf_json["scale"] = { transform.scale.x, transform.scale.y };
        tf_json["rotation"] = transform.rotation;
        tf_json["pivot"] = { transform.pivot.x, transform.pivot.y };
        tf_json["visible"] = transform.visible;
        entity_json["transform"] = tf_json;

        // SpriteComponent
        if (em.has_component<SpriteComponent>(id)) {
            auto& sprite = em.get_component<SpriteComponent>(id);
            json sp_json;
            sp_json["texture"] = sprite.texture ? sprite.texture->get_filepath() : "";
            sp_json["size"] = { sprite.size.x, sprite.size.y };
            sp_json["color"] = { sprite.color.r, sprite.color.g, sprite.color.b, sprite.color.a };
            sp_json["flip_x"] = sprite.flip_x;
            sp_json["flip_y"] = sprite.flip_y;
            sp_json["visible"] = sprite.visible;
            sp_json["use_src_rect"] = sprite.use_src_rect;
            if (sprite.use_src_rect) {
                sp_json["src_rect"] = { sprite.src_rect.x, sprite.src_rect.y, sprite.src_rect.w, sprite.src_rect.h };
            }
            entity_json["sprite"] = sp_json;
        }

        // RigidBodyComponent
        if (em.has_component<RigidBodyComponent>(id)) {
            auto& rb = em.get_component<RigidBodyComponent>(id);
            json rb_json;
            std::string type_str = "Static";
            if (rb.body_type == BodyType::Dynamic) type_str = "Dynamic";
            else if (rb.body_type == BodyType::Kinematic) type_str = "Kinematic";
            rb_json["type"] = type_str;
            rb_json["mass"] = rb.mass;
            rb_json["gravity_scale"] = rb.gravity_scale;
            rb_json["friction"] = rb.friction;
            rb_json["restitution"] = rb.restitution;
            rb_json["fixed_rotation"] = rb.fixed_rotation;
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

        if (root.contains("entities") && root["entities"].is_array()) {
            for (const auto& entity_json : root["entities"]) {
                EntityID id = em.create_entity();

                // TagComponent
                if (entity_json.contains("tag") && entity_json["tag"].is_string()) {
                    em.add_component<TagComponent>(id, TagComponent{ entity_json["tag"].get<std::string>() });
                }

                // TransformComponent
                TransformComponent tf;
                if (entity_json.contains("transform")) {
                    const auto& tf_j = entity_json["transform"];
                    if (tf_j.contains("position") && tf_j["position"].is_array() && tf_j["position"].size() >= 2) {
                        float z = (tf_j["position"].size() >= 3) ? tf_j["position"][2].get<float>() : 0.0f;
                        tf.position = glm::vec3(tf_j["position"][0].get<float>(), tf_j["position"][1].get<float>(), z);
                    }
                    if (tf_j.contains("scale") && tf_j["scale"].is_array() && tf_j["scale"].size() >= 2) {
                        tf.scale = glm::vec2(tf_j["scale"][0].get<float>(), tf_j["scale"][1].get<float>());
                    }
                    if (tf_j.contains("rotation")) {
                        tf.rotation = tf_j["rotation"].get<float>();
                    }
                    if (tf_j.contains("pivot") && tf_j["pivot"].is_array() && tf_j["pivot"].size() >= 2) {
                        tf.pivot = glm::vec2(tf_j["pivot"][0].get<float>(), tf_j["pivot"][1].get<float>());
                    }
                    if (tf_j.contains("visible")) {
                        tf.visible = tf_j["visible"].get<bool>();
                    }
                    tf.world_position = tf.position;
                    tf.world_scale = tf.scale;
                    tf.world_rotation = tf.rotation;
                    tf.world_pivot = tf.pivot;
                    tf.world_visible = tf.visible;
                }
                em.add_component<TransformComponent>(id, tf);

                // SpriteComponent
                if (entity_json.contains("sprite")) {
                    const auto& sp_j = entity_json["sprite"];
                    SpriteComponent sc;
                    if (sp_j.contains("texture") && sp_j["texture"].is_string()) {
                        std::string tex_path = sp_j["texture"].get<std::string>();
                        if (!tex_path.empty() && m_renderer) {
                            sc.texture = Texture::load_from_file(m_renderer, tex_path);
                        }
                    }
                    if (!sc.texture && m_renderer) {
                        sc.texture = Texture::create_solid(m_renderer, 8, 8, 255, 255, 255, 255);
                    }
                    if (sp_j.contains("size") && sp_j["size"].is_array() && sp_j["size"].size() >= 2) {
                        sc.size = glm::vec2(sp_j["size"][0].get<float>(), sp_j["size"][1].get<float>());
                    }
                    if (sp_j.contains("color") && sp_j["color"].is_array() && sp_j["color"].size() >= 4) {
                        sc.color = SDL_Color{
                            sp_j["color"][0].get<uint8_t>(),
                            sp_j["color"][1].get<uint8_t>(),
                            sp_j["color"][2].get<uint8_t>(),
                            sp_j["color"][3].get<uint8_t>()
                        };
                    }
                    if (sp_j.contains("flip_x")) sc.flip_x = sp_j["flip_x"].get<bool>();
                    if (sp_j.contains("flip_y")) sc.flip_y = sp_j["flip_y"].get<bool>();
                    if (sp_j.contains("visible")) sc.visible = sp_j["visible"].get<bool>();
                    if (sp_j.contains("use_src_rect")) sc.use_src_rect = sp_j["use_src_rect"].get<bool>();
                    if (sc.use_src_rect && sp_j.contains("src_rect") && sp_j["src_rect"].size() >= 4) {
                        sc.src_rect = SDL_FRect{
                            sp_j["src_rect"][0].get<float>(),
                            sp_j["src_rect"][1].get<float>(),
                            sp_j["src_rect"][2].get<float>(),
                            sp_j["src_rect"][3].get<float>()
                        };
                    }
                    em.add_component<SpriteComponent>(id, sc);
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
