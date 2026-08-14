#pragma once

#include "../core/Log.hpp"
#include "../events/Event.hpp"
#include "../events/EventDispatcher.hpp"
#include "../scene/SpatialGrid.hpp"
#include "Components.hpp"
#include "EntityManager.hpp"
#include "../physics/cute_c2.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

class PhysicsSystem {
public:
  static void set_gravity(const glm::vec2 &gravity) { s_gravity = gravity; }
  static const glm::vec2 &get_gravity() { return s_gravity; }

  // Execute 2D Physics Step: Kinematic Pre-Movement Sweep & Axis-Separated
  // Collision Clamping
  static void update(EntityManager &entity_manager,
                     EventDispatcher &event_dispatcher, float delta_time) {
    // Step 1: Populate Spatial Hash Grid for Broadphase Collision Querying
    static SpatialGrid s_physics_grid(256.0f);
    s_physics_grid.clear();

    entity_manager.for_each<TransformComponent, ColliderComponent>(
        [](EntityID id, const TransformComponent &transform,
           const ColliderComponent &collider) {
          if (!transform.world_visible)
            return;

          glm::vec2 max_size{64.0f, 64.0f};
          for (const auto &shape : collider.shapes) {
            if (shape.type == ColliderShapeType::Box) {
              max_size = glm::max(max_size, shape.size);
            } else if (shape.type == ColliderShapeType::Circle) {
              max_size = glm::max(max_size, glm::vec2(shape.radius * 2.0f));
            }
          }
          s_physics_grid.insert(id, transform.world_position,
                                max_size * transform.world_scale);
        });

    // Step 2: Godot 4 & Unity Official Iterative move_and_slide Vector Logic for Dynamic Bodies
    entity_manager.for_each<TransformComponent, RigidBodyComponent, ColliderComponent>(
        [&entity_manager, delta_time](EntityID id1, TransformComponent &t1, RigidBodyComponent &rb1, ColliderComponent &c1) {
          if (rb1.is_static) return;

          // Apply Acceleration & Gravity
          rb1.velocity += (rb1.acceleration + s_gravity * rb1.gravity_scale) * delta_time;

          glm::vec2 movement = rb1.velocity * delta_time;
          if (glm::length(movement) < 0.0001f) return;

          constexpr int MAX_SLIDES = 4;
          for (int slide_iter = 0; slide_iter < MAX_SLIDES; ++slide_iter) {
            if (glm::length(movement) < 0.0001f) break;

            // Proposed position step
            TransformComponent test_step = t1;
            test_step.position += glm::vec3(movement, 0.0f);
            test_step.world_position += glm::vec3(movement, 0.0f);

            glm::vec2 best_normal{0.0f, 0.0f};
            float max_penetration = 0.0f;
            bool collided = false;

            entity_manager.for_each<TransformComponent, ColliderComponent>(
                [&t1, &c1, &test_step, id1, &collided, &best_normal, &max_penetration](
                    EntityID id2, const TransformComponent &t2, const ColliderComponent &c2) {
                  if (id1 == id2 || !t2.world_visible) return;
                  for (const auto &s1 : c1.shapes) {
                    for (const auto &s2 : c2.shapes) {
                      if (s1.is_trigger || s2.is_trigger) continue;

                      glm::vec2 mtv; float pen;
                      if (check_shape_intersection(test_step, s1, t2, s2, mtv, pen)) {
                        collided = true;
                        if (pen > max_penetration) {
                          max_penetration = pen;
                          best_normal = mtv; // Normal pointing away from t2 obstacle towards t1
                        }
                      }
                    }
                  }
                }
            );

            if (!collided) {
              // Clear path! Apply movement fully
              t1.position += glm::vec3(movement, 0.0f);
              break;
            }

            // Collision encountered!
            float n_len = glm::length(best_normal);
            if (n_len < 0.0001f) break;

            glm::vec2 unit_N = best_normal / n_len;

            // Project remaining movement along wall surface plane (Godot/Unity slide)
            float dot = glm::dot(movement, unit_N);
            if (dot < 0.0f) {
              movement = movement - unit_N * dot;
              rb1.velocity = rb1.velocity - unit_N * glm::dot(rb1.velocity, unit_N);
              t1.position += glm::vec3(movement, 0.0f);
            }
            break;
          }
        }
    );

    // Step 3: Trigger Sensors & Overlap Event Dispatching
    entity_manager.for_each<TransformComponent, ColliderComponent>(
        [&entity_manager, &event_dispatcher](
            EntityID id1, TransformComponent &t1, ColliderComponent &c1) {
          if (!t1.world_visible)
            return;

          SDL_FRect query_bounds{t1.world_position.x - 200.0f,
                                 t1.world_position.y - 200.0f, 400.0f, 400.0f};

          static std::vector<EntityID> candidates;
          candidates.clear();
          s_physics_grid.query_bounds(query_bounds, candidates);

          for (EntityID id2 : candidates) {
            if (id1 >= id2 || !entity_manager.is_alive(id2))
              continue;
            if (!entity_manager.has_component<TransformComponent>(id2) ||
                !entity_manager.has_component<ColliderComponent>(id2))
              continue;

            auto &t2 = entity_manager.get_component<TransformComponent>(id2);
            auto &c2 = entity_manager.get_component<ColliderComponent>(id2);
            if (!t2.world_visible)
              continue;

            for (const auto &s1 : c1.shapes) {
              for (const auto &s2 : c2.shapes) {
                glm::vec2 mtv{0.0f, 0.0f};
                float penetration = 0.0f;

                if (check_shape_intersection(t1, s1, t2, s2, mtv,
                                             penetration)) {
                  bool is_trigger_pair = s1.is_trigger || s2.is_trigger;
                  CollisionEvent col_ev{id1, id2, mtv, is_trigger_pair};
                  event_dispatcher.publish(col_ev);
                }
              }
            }
          }
        });
  }

private:
  static inline glm::vec2 s_gravity{0.0f, -480.0f};
  static constexpr float s_collision_epsilon = 0.0001f;

  // Unified Collision Shape Intersection using cute_c2 physics engine: out_mtv ALWAYS points FROM t2 TOWARDS t1
  static bool check_shape_intersection(const TransformComponent &t1,
                                       const ColliderShape &s1,
                                       const TransformComponent &t2,
                                       const ColliderShape &s2,
                                       glm::vec2 &out_mtv,
                                       float &out_penetration) {
    glm::vec2 pos1 = glm::vec2(t1.world_position) + s1.offset * t1.world_scale;
    glm::vec2 pos2 = glm::vec2(t2.world_position) + s2.offset * t2.world_scale;

    c2Manifold m;
    m.count = 0;

    if (s1.type == ColliderShapeType::Circle && s2.type == ColliderShapeType::Circle) {
      c2Circle cA{c2V(pos1.x, pos1.y), s1.radius * t1.world_scale.x};
      c2Circle cB{c2V(pos2.x, pos2.y), s2.radius * t2.world_scale.x};
      c2CircletoCircleManifold(cA, cB, &m);
    } else if (s1.type == ColliderShapeType::Box && s2.type == ColliderShapeType::Box) {
      glm::vec2 h1 = s1.size * t1.world_scale * 0.5f;
      glm::vec2 h2 = s2.size * t2.world_scale * 0.5f;
      c2AABB bA{c2V(pos1.x - h1.x, pos1.y - h1.y), c2V(pos1.x + h1.x, pos1.y + h1.y)};
      c2AABB bB{c2V(pos2.x - h2.x, pos2.y - h2.y), c2V(pos2.x + h2.x, pos2.y + h2.y)};
      c2AABBtoAABBManifold(bA, bB, &m);
    } else if (s1.type == ColliderShapeType::Box && s2.type == ColliderShapeType::Circle) {
      glm::vec2 h1 = s1.size * t1.world_scale * 0.5f;
      c2AABB bA{c2V(pos1.x - h1.x, pos1.y - h1.y), c2V(pos1.x + h1.x, pos1.y + h1.y)};
      c2Circle cB{c2V(pos2.x, pos2.y), s2.radius * t2.world_scale.x};
      c2CircletoAABBManifold(cB, bA, &m);
      m.n.x = -m.n.x;
      m.n.y = -m.n.y;
    } else if (s1.type == ColliderShapeType::Circle && s2.type == ColliderShapeType::Box) {
      c2Circle cA{c2V(pos1.x, pos1.y), s1.radius * t1.world_scale.x};
      glm::vec2 h2 = s2.size * t2.world_scale * 0.5f;
      c2AABB bB{c2V(pos2.x - h2.x, pos2.y - h2.y), c2V(pos2.x + h2.x, pos2.y + h2.y)};
      c2CircletoAABBManifold(cA, bB, &m);
    }

    if (m.count > 0 && m.depths[0] > 0.0001f) {
      out_penetration = m.depths[0];
      out_mtv = glm::vec2(m.n.x, m.n.y);
      return true;
    }

    return false;
  }
};

} // namespace Engine
