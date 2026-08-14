#pragma once

#include "EntityManager.hpp"
#include "Components.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/Event.hpp"
#include "../scene/SpatialGrid.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Engine {

class PhysicsSystem {
public:
    static void set_gravity(const glm::vec2& gravity) { s_gravity = gravity; }
    static const glm::vec2& get_gravity() { return s_gravity; }

    // Execute 2D Physics Step: Integration, Spatial Hash Broadphase, Shape Intersection & Penetration Resolution
    static void update(EntityManager& entity_manager, EventDispatcher& event_dispatcher, float delta_time) {
        // Step 1: Velocity and Gravity Integration for Non-Static Bodies
        entity_manager.for_each<TransformComponent, RigidBodyComponent>(
            [delta_time](EntityID id, TransformComponent& transform, RigidBodyComponent& rb) {
                if (rb.is_static) return;

                // Apply Acceleration + Gravity
                rb.velocity += (rb.acceleration + s_gravity * rb.gravity_scale) * delta_time;
                transform.position += glm::vec3(rb.velocity * delta_time, 0.0f);
            }
        );

        // Step 2: Re-populate Spatial Hash Grid for Broadphase Collision Querying
        static SpatialGrid s_physics_grid(256.0f);
        s_physics_grid.clear();

        entity_manager.for_each<TransformComponent, ColliderComponent>(
            [](EntityID id, const TransformComponent& transform, const ColliderComponent& collider) {
                if (!transform.world_visible) return;

                glm::vec2 max_size{64.0f, 64.0f};
                for (const auto& shape : collider.shapes) {
                    if (shape.type == ColliderShapeType::Box) {
                        max_size = glm::max(max_size, shape.size);
                    } else if (shape.type == ColliderShapeType::Circle) {
                        max_size = glm::max(max_size, glm::vec2(shape.radius * 2.0f));
                    }
                }
                s_physics_grid.insert(id, transform.world_position, max_size * transform.world_scale);
            }
        );

        // Step 3: Narrowphase Intersection & Resolution
        entity_manager.for_each<TransformComponent, ColliderComponent>(
            [&entity_manager, &event_dispatcher](EntityID id1, TransformComponent& t1, ColliderComponent& c1) {
                if (!t1.world_visible) return;

                bool has_rb1 = entity_manager.has_component<RigidBodyComponent>(id1);
                auto* rb1 = has_rb1 ? &entity_manager.get_component<RigidBodyComponent>(id1) : nullptr;

                // Query broadphase candidate collision pairs from spatial grid
                SDL_FRect query_bounds{
                    t1.world_position.x - 200.0f,
                    t1.world_position.y - 200.0f,
                    400.0f, 400.0f
                };

                static std::vector<EntityID> candidates;
                candidates.clear();
                s_physics_grid.query_bounds(query_bounds, candidates);

                for (EntityID id2 : candidates) {
                    if (id1 >= id2 || !entity_manager.is_alive(id2)) continue;
                    if (!entity_manager.has_component<TransformComponent>(id2) || !entity_manager.has_component<ColliderComponent>(id2)) continue;

                    auto& t2 = entity_manager.get_component<TransformComponent>(id2);
                    auto& c2 = entity_manager.get_component<ColliderComponent>(id2);
                    if (!t2.world_visible) continue;

                    bool has_rb2 = entity_manager.has_component<RigidBodyComponent>(id2);
                    auto* rb2 = has_rb2 ? &entity_manager.get_component<RigidBodyComponent>(id2) : nullptr;

                    // Evaluate multi-shape composite colliders
                    for (const auto& s1 : c1.shapes) {
                        for (const auto& s2 : c2.shapes) {
                            glm::vec2 mtv{0.0f, 0.0f};
                            float penetration = 0.0f;

                            if (check_shape_intersection(t1, s1, t2, s2, mtv, penetration)) {
                                bool is_trigger_pair = s1.is_trigger || s2.is_trigger;

                                // Dispatch Collision Event
                                CollisionEvent col_ev{id1, id2, mtv, is_trigger_pair};
                                event_dispatcher.publish(col_ev);

                                // Resolve solid physical collision overlap (skip if trigger sensor)
                                if (!is_trigger_pair) {
                                    resolve_collision(t1, rb1, t2, rb2, mtv, penetration);
                                }
                            }
                        }
                    }
                }
            }
        );
    }

private:
    static inline glm::vec2 s_gravity{0.0f, -480.0f};

    static bool check_shape_intersection(
        const TransformComponent& t1, const ColliderShape& s1,
        const TransformComponent& t2, const ColliderShape& s2,
        glm::vec2& out_mtv, float& out_penetration
    ) {
        glm::vec2 pos1 = glm::vec2(t1.world_position) + s1.offset * t1.world_scale;
        glm::vec2 pos2 = glm::vec2(t2.world_position) + s2.offset * t2.world_scale;

        // Box vs Box (AABB)
        if (s1.type == ColliderShapeType::Box && s2.type == ColliderShapeType::Box) {
            glm::vec2 half1 = (s1.size * t1.world_scale) * 0.5f;
            glm::vec2 half2 = (s2.size * t2.world_scale) * 0.5f;

            glm::vec2 delta = pos2 - pos1;
            glm::vec2 overlap = (half1 + half2) - glm::abs(delta);

            if (overlap.x > 0.0f && overlap.y > 0.0f) {
                if (overlap.x < overlap.y) {
                    out_penetration = overlap.x;
                    out_mtv = glm::vec2((delta.x < 0.0f) ? 1.0f : -1.0f, 0.0f);
                } else {
                    out_penetration = overlap.y;
                    out_mtv = glm::vec2(0.0f, (delta.y < 0.0f) ? 1.0f : -1.0f);
                }
                return true;
            }
        }
        // Circle vs Circle
        else if (s1.type == ColliderShapeType::Circle && s2.type == ColliderShapeType::Circle) {
            float r1 = s1.radius * t1.world_scale.x;
            float r2 = s2.radius * t2.world_scale.x;

            glm::vec2 delta = pos2 - pos1;
            float dist_sq = delta.x * delta.x + delta.y * delta.y;
            float combined_r = r1 + r2;

            if (dist_sq <= combined_r * combined_r && dist_sq > 0.0001f) {
                float dist = std::sqrt(dist_sq);
                out_penetration = combined_r - dist;
                out_mtv = -delta / dist;
                return true;
            }
        }
        // Box vs Circle
        else if (s1.type == ColliderShapeType::Box && s2.type == ColliderShapeType::Circle) {
            glm::vec2 half1 = (s1.size * t1.world_scale) * 0.5f;
            float r2 = s2.radius * t2.world_scale.x;

            glm::vec2 closest = glm::clamp(pos2, pos1 - half1, pos1 + half1);
            glm::vec2 delta = pos2 - closest;
            float dist_sq = delta.x * delta.x + delta.y * delta.y;

            if (dist_sq <= r2 * r2 && dist_sq > 0.0001f) {
                float dist = std::sqrt(dist_sq);
                out_penetration = r2 - dist;
                out_mtv = delta / dist;
                return true;
            }
        }
        // Circle vs Box
        else if (s1.type == ColliderShapeType::Circle && s2.type == ColliderShapeType::Box) {
            bool hit = check_shape_intersection(t2, s2, t1, s1, out_mtv, out_penetration);
            out_mtv = -out_mtv;
            return hit;
        }

        return false;
    }

    static void resolve_collision(
        TransformComponent& t1, RigidBodyComponent* rb1,
        TransformComponent& t2, RigidBodyComponent* rb2,
        const glm::vec2& mtv, float penetration
    ) {
        bool static1 = !rb1 || rb1->is_static;
        bool static2 = !rb2 || rb2->is_static;

        if (static1 && static2) return;

        glm::vec2 push = mtv * penetration;

        if (!static1 && static2) {
            t1.position += glm::vec3(push, 0.0f);
            if (rb1) {
                if (glm::dot(rb1->velocity, mtv) < 0.0f) {
                    rb1->velocity -= mtv * glm::dot(rb1->velocity, mtv) * (1.0f + rb1->restitution);
                }
            }
        } else if (static1 && !static2) {
            t2.position -= glm::vec3(push, 0.0f);
            if (rb2) {
                if (glm::dot(rb2->velocity, -mtv) < 0.0f) {
                    rb2->velocity -= (-mtv) * glm::dot(rb2->velocity, -mtv) * (1.0f + rb2->restitution);
                }
            }
        } else {
            t1.position += glm::vec3(push * 0.5f, 0.0f);
            t2.position -= glm::vec3(push * 0.5f, 0.0f);
        }
    }
};

} // namespace Engine
