#pragma once

#include "../events/Event.hpp"
#include "../events/EventDispatcher.hpp"
#include "ColliderGeometry.hpp"
#include "PhysicsDebugFrame.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include "../ecs/EntityManager.hpp"
#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Engine {

// Forward declaration
class PhysicsSystem;

// Raycast hit result structure
struct RaycastHit2D {
    bool hit{false};
    EntityID entity_id{0};
    glm::vec2 point{0.0f, 0.0f};   // Impact position in world coordinates (pixels)
    glm::vec2 normal{0.0f, 0.0f};  // Surface normal vector at impact point
    float fraction{0.0f};          // Normalized fraction along the ray [0, 1]
    float distance{0.0f};          // Distance in pixels from ray origin to impact point
};

class RayCastClosestCallback : public b2RayCastCallback {
public:
    uint16_t mask_bits{CollisionCategory::ALL};
    bool ignore_sensors{true};
    EntityID ignore_entity{0};

    bool hit{false};
    EntityID hit_entity{0};
    b2Vec2 hit_point{0.0f, 0.0f};
    b2Vec2 hit_normal{0.0f, 0.0f};
    float hit_fraction{1.0f};

    float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override {
        if (ignore_sensors && fixture->IsSensor()) return -1.0f;

        b2Filter filter = fixture->GetFilterData();
        if ((filter.categoryBits & mask_bits) == 0) return -1.0f;

        EntityID id = static_cast<EntityID>(fixture->GetBody()->GetUserData().pointer);
        if (id == ignore_entity) return -1.0f;

        hit = true;
        hit_entity = id;
        hit_point = point;
        hit_normal = normal;
        hit_fraction = fraction;

        return fraction; // Clip ray to closest hit
    }
};

class RayCastAllCallback : public b2RayCastCallback {
public:
    uint16_t mask_bits{CollisionCategory::ALL};
    bool ignore_sensors{true};
    EntityID ignore_entity{0};
    glm::vec2 origin_px{0.0f, 0.0f};
    std::vector<RaycastHit2D> hits;

    float ReportFixture(b2Fixture* fixture, const b2Vec2& point, const b2Vec2& normal, float fraction) override {
        if (ignore_sensors && fixture->IsSensor()) return -1.0f;

        b2Filter filter = fixture->GetFilterData();
        if ((filter.categoryBits & mask_bits) == 0) return -1.0f;

        EntityID id = static_cast<EntityID>(fixture->GetBody()->GetUserData().pointer);
        if (id == ignore_entity) return -1.0f;

        glm::vec2 pt_px(point.x * 100.0f, point.y * 100.0f);
        float dist = glm::distance(origin_px, pt_px);

        hits.push_back({
            true,
            id,
            pt_px,
            glm::vec2(normal.x, normal.y),
            fraction,
            dist
        });

        return 1.0f; // Continue ray to find all hits
    }
};

// Contact listener for Box2D collision events
class PhysicsContactListener : public b2ContactListener {
public:
  explicit PhysicsContactListener(EventDispatcher &dispatcher)
      : m_dispatcher(dispatcher) {}

  void BeginContact(b2Contact *contact) override {
    b2Fixture *fixtureA = contact->GetFixtureA();
    b2Fixture *fixtureB = contact->GetFixtureB();

    EntityID idA =
        static_cast<EntityID>(fixtureA->GetBody()->GetUserData().pointer);
    EntityID idB =
        static_cast<EntityID>(fixtureB->GetBody()->GetUserData().pointer);

    b2WorldManifold worldManifold;
    contact->GetWorldManifold(&worldManifold);
    glm::vec2 normal(worldManifold.normal.x, worldManifold.normal.y);

    bool is_trigger = fixtureA->IsSensor() || fixtureB->IsSensor();

    CollisionEvent col_ev{idA, idB, normal, is_trigger};
    m_dispatcher.publish(col_ev);
  }

  void EndContact(b2Contact *contact) override {
    b2Fixture *fixtureA = contact->GetFixtureA();
    b2Fixture *fixtureB = contact->GetFixtureB();
    if (!fixtureA || !fixtureB) return;

    EntityID idA =
        static_cast<EntityID>(fixtureA->GetBody()->GetUserData().pointer);
    EntityID idB =
        static_cast<EntityID>(fixtureB->GetBody()->GetUserData().pointer);

    bool is_trigger = fixtureA->IsSensor() || fixtureB->IsSensor();

    CollisionEndEvent col_end{idA, idB, is_trigger};
    m_dispatcher.publish(col_end);
  }

  void PreSolve(b2Contact *contact, const b2Manifold *oldManifold) override;

private:
  EventDispatcher &m_dispatcher;
};

class PhysicsSystem {
public:
  static constexpr float k_p2m = 0.01f;  // Pixels to meters scale factor
  static constexpr float k_m2p = 100.0f; // Meters to pixels scale factor

  static void set_ignore_one_way(EntityID id, bool ignore) {
    if (ignore) {
      s_ignore_one_way.insert(id);
    } else {
      s_ignore_one_way.erase(id);
    }
  }

  static bool is_ignoring_one_way(EntityID id) {
    return s_ignore_one_way.contains(id);
  }

  static bool is_grounded(EntityManager &em, EntityID id) {
    auto it = s_b2_bodies.find(id);
    if (it == s_b2_bodies.end() || !it->second) return false;
    b2Body *body = it->second;

    for (b2ContactEdge *ce = body->GetContactList(); ce; ce = ce->next) {
      b2Contact *contact = ce->contact;
      if (!contact->IsTouching()) continue;
      if (contact->GetFixtureA()->IsSensor() || contact->GetFixtureB()->IsSensor()) continue;

      b2WorldManifold wm;
      contact->GetWorldManifold(&wm);

      // Normal points from fixtureA to fixtureB.
      // If fixtureA is our body and ground (fixtureB) is below us, normal points DOWN (wm.normal.y < -0.4f).
      // If fixtureB is our body and ground (fixtureA) is below us, normal points UP (wm.normal.y > 0.4f).
      if (contact->GetFixtureA()->GetBody() == body) {
        if (wm.normal.y < -0.4f) return true;
      } else {
        if (wm.normal.y > 0.4f) return true;
      }
    }
    return false;
  }

  static RaycastHit2D raycast(const glm::vec2 &origin, const glm::vec2 &destination,
                              uint16_t mask_bits = CollisionCategory::ALL,
                              bool ignore_sensors = true, EntityID ignore_entity = 0) {
    if (!s_b2_world) return {};

    b2Vec2 p1(origin.x * k_p2m, origin.y * k_p2m);
    b2Vec2 p2(destination.x * k_p2m, destination.y * k_p2m);

    if (b2DistanceSquared(p1, p2) <= 0.000001f) {
      return {};
    }

    RayCastClosestCallback callback;
    callback.mask_bits = mask_bits;
    callback.ignore_sensors = ignore_sensors;
    callback.ignore_entity = ignore_entity;

    s_b2_world->RayCast(&callback, p1, p2);

    if (callback.hit) {
      glm::vec2 pt(callback.hit_point.x * k_m2p, callback.hit_point.y * k_m2p);
      float dist = glm::distance(origin, pt);
      return {
          true,
          callback.hit_entity,
          pt,
          glm::vec2(callback.hit_normal.x, callback.hit_normal.y),
          callback.hit_fraction,
          dist
      };
    }
    return {};
  }

  // Cast a ray in a given direction with max distance
  static RaycastHit2D raycast_direction(const glm::vec2 &origin, const glm::vec2 &direction,
                                        float max_distance = 1000.0f,
                                        uint16_t mask_bits = CollisionCategory::ALL,
                                        bool ignore_sensors = true, EntityID ignore_entity = 0) {
    float len = glm::length(direction);
    if (len <= 0.000001f || max_distance <= 0.000001f) return {};
    glm::vec2 norm_dir = direction / len;
    return raycast(origin, origin + norm_dir * max_distance, mask_bits, ignore_sensors, ignore_entity);
  }

  // Cast a ray from origin to destination and return all hit results sorted closest to furthest
  static std::vector<RaycastHit2D> raycast_all(const glm::vec2 &origin, const glm::vec2 &destination,
                                               uint16_t mask_bits = CollisionCategory::ALL,
                                               bool ignore_sensors = true, EntityID ignore_entity = 0) {
    if (!s_b2_world) return {};

    RayCastAllCallback callback;
    callback.mask_bits = mask_bits;
    callback.ignore_sensors = ignore_sensors;
    callback.ignore_entity = ignore_entity;
    callback.origin_px = origin;

    b2Vec2 p1(origin.x * k_p2m, origin.y * k_p2m);
    b2Vec2 p2(destination.x * k_p2m, destination.y * k_p2m);

    s_b2_world->RayCast(&callback, p1, p2);

    std::sort(callback.hits.begin(), callback.hits.end(),
              [](const RaycastHit2D &a, const RaycastHit2D &b) {
                return a.fraction < b.fraction;
              });

    return callback.hits;
  }

  // Apply a 2D radial explosion impulse to all dynamic bodies within radius with distance falloff & occlusion
  static std::vector<EntityID> apply_explosion_force(
      EntityManager &em,
      const glm::vec2 &epicenter,
      float radius,
      float max_impulse,
      float upward_modifier = 0.25f,
      uint16_t mask_bits = CollisionCategory::ALL,
      bool check_occlusion = true) {
    if (!s_b2_world) return {};

    class ExplosionQueryCallback : public b2QueryCallback {
    public:
      std::vector<b2Body *> bodies;
      bool ReportFixture(b2Fixture *fixture) override {
        if (fixture->IsSensor()) return true;
        b2Body *body = fixture->GetBody();
        if (body->GetType() == b2_dynamicBody) {
          if (std::find(bodies.begin(), bodies.end(), body) == bodies.end()) {
            bodies.push_back(body);
          }
        }
        return true;
      }
    };

    float r_meters = radius * k_p2m;
    b2Vec2 epic_m(epicenter.x * k_p2m, epicenter.y * k_p2m);

    b2AABB aabb;
    aabb.lowerBound.Set(epic_m.x - r_meters, epic_m.y - r_meters);
    aabb.upperBound.Set(epic_m.x + r_meters, epic_m.y + r_meters);

    ExplosionQueryCallback query;
    s_b2_world->QueryAABB(&query, aabb);

    std::vector<EntityID> affected_entities;

    for (b2Body *body : query.bodies) {
      EntityID id = static_cast<EntityID>(body->GetUserData().pointer);
      if (id == 0 || !em.is_alive(id)) continue;

      b2Vec2 body_pos = body->GetWorldCenter();
      b2Vec2 delta = body_pos - epic_m;
      float dist = delta.Length();

      if (dist > r_meters || dist < 0.0001f) {
        if (dist < 0.0001f) {
          delta.Set(0.0f, 1.0f);
          dist = 0.0001f;
        } else {
          continue;
        }
      }

      // Check occlusion against solid static geometry
      if (check_occlusion) {
        RaycastHit2D hit = raycast(epicenter, glm::vec2(body_pos.x * k_m2p, body_pos.y * k_m2p),
                                   CollisionCategory::GROUND, true, id);
        if (hit.hit && hit.distance < (dist * k_m2p - 8.0f)) {
          // Blocked by solid wall
          continue;
        }
      }

      float falloff = 1.0f - (dist / r_meters);

      glm::vec2 dir(delta.x / dist, delta.y / dist);
      dir.y += upward_modifier;
      float dir_len = glm::length(dir);
      if (dir_len > 0.0001f) {
        dir /= dir_len;
      } else {
        dir = glm::vec2(0.0f, 1.0f);
      }

      float impulse_mag = max_impulse * falloff;
      body->ApplyLinearImpulseToCenter(b2Vec2(dir.x * impulse_mag * k_p2m, dir.y * impulse_mag * k_p2m), true);

      if (em.has_component<RigidBodyComponent>(id)) {
        auto &rb = em.get_component<RigidBodyComponent>(id);
        rb.velocity = glm::vec2(body->GetLinearVelocity().x * k_m2p, body->GetLinearVelocity().y * k_m2p);
      }

      affected_entities.push_back(id);
    }

    return affected_entities;
  }

  static void set_gravity(const glm::vec2 &gravity) {
    s_gravity = gravity;
    if (s_b2_world) {
      s_b2_world->SetGravity(b2Vec2(gravity.x * k_p2m, gravity.y * k_p2m));
    }
  }

  static const glm::vec2 &get_gravity() { return s_gravity; }

  static void set_debug_options(const PhysicsDebugOptions& options) { s_debug_options = options; }
  [[nodiscard]] static const PhysicsDebugOptions& debug_options() { return s_debug_options; }
  [[nodiscard]] static const PhysicsDebugFrame& debug_frame() { return s_debug_frame; }

  static void clear() {
    s_b2_bodies.clear();
    s_b2_joints.clear();
    s_ignore_one_way.clear();
    s_contact_listener.reset();
    s_b2_world.reset();
  }

  // Recreates one ECS-backed body and its fixtures from the current authored
  // components. Editor previews remain cheap; callers invoke this only when a
  // property gesture commits/cancels or when undo/redo applies a snapshot.
  static void rebuild_body(EntityManager &em, EntityID id) {
    if (!s_b2_world) return;

    for (auto it = s_b2_joints.begin(); it != s_b2_joints.end();) {
      const EntityID joint_entity = it->first;
      bool references_body = joint_entity == id;
      if (!references_body && em.is_alive(joint_entity) && em.has_component<JointComponent>(joint_entity)) {
        references_body = em.get_component<JointComponent>(joint_entity).target_entity == id;
      }
      if (references_body) {
        if (it->second) s_b2_world->DestroyJoint(it->second);
        it = s_b2_joints.erase(it);
      } else {
        ++it;
      }
    }

    auto body = s_b2_bodies.find(id);
    if (body != s_b2_bodies.end()) {
      if (body->second) s_b2_world->DestroyBody(body->second);
      s_b2_bodies.erase(body);
    }
    create_body(em, id);
  }

  static void rebuild_body_hierarchy(EntityManager &em, EntityID root) {
    if (!em.is_alive(root)) return;
    rebuild_body(em, root);
    if (!em.has_component<HierarchyComponent>(root)) return;
    const auto children = em.get_component<HierarchyComponent>(root).children;
    for (const EntityID child : children) rebuild_body_hierarchy(em, child);
  }

  static void set_position(EntityManager &em, EntityID id, const glm::vec2 &position) {
    if (em.has_component<TransformComponent>(id)) {
      auto &tx = em.get_component<TransformComponent>(id);
      tx.position.x = position.x;
      tx.position.y = position.y;
    }
    auto it = s_b2_bodies.find(id);
    if (it != s_b2_bodies.end() && it->second) {
      float current_angle = it->second->GetAngle();
      it->second->SetTransform(b2Vec2(position.x * k_p2m, position.y * k_p2m), current_angle);
    }
  }

  // Synchronizes only the Box2D body from the computed world transform.
  // Editor tools must use this path so child local coordinates remain intact.
  static void sync_body_transform(EntityManager &em, EntityID id) {
    if (!em.is_alive(id) || !em.has_component<TransformComponent>(id)) return;
    auto it = s_b2_bodies.find(id);
    if (it == s_b2_bodies.end() || !it->second) return;

    const auto &transform = em.get_component<TransformComponent>(id);
    it->second->SetTransform(
        b2Vec2(transform.world_position.x * k_p2m,
               transform.world_position.y * k_p2m),
        glm::radians(-transform.world_rotation));
  }

  static void sync_body_hierarchy(EntityManager &em, EntityID root) {
    if (!em.is_alive(root)) return;
    sync_body_transform(em, root);
    if (!em.has_component<HierarchyComponent>(root)) return;
    const auto children = em.get_component<HierarchyComponent>(root).children;
    for (const EntityID child : children) sync_body_hierarchy(em, child);
  }

  static void set_velocity(EntityManager &em, EntityID id, const glm::vec2 &velocity) {
    if (!em.has_component<RigidBodyComponent>(id)) return;
    auto &rb = em.get_component<RigidBodyComponent>(id);
    rb.velocity = velocity;
    auto it = s_b2_bodies.find(id);
    if (it != s_b2_bodies.end() && it->second) {
      it->second->SetLinearVelocity(b2Vec2(velocity.x * k_p2m, velocity.y * k_p2m));
    }
  }

  static void apply_impulse(EntityManager &em, EntityID id, const glm::vec2 &impulse) {
    if (!em.has_component<RigidBodyComponent>(id)) return;
    auto &rb = em.get_component<RigidBodyComponent>(id);
    float mag = glm::length(impulse);
    glm::vec2 dir = (mag > 0.0001f) ? (impulse / mag) : glm::vec2(0.0f, 1.0f);
    ActiveForce af;
    af.mode = ForceMode::Impulse;
    af.direction = dir;
    af.max_magnitude = mag;
    af.duration = 0.001f;
    rb.active_forces.push_back(af);
  }

  static void apply_force(EntityManager &em, EntityID id, const glm::vec2 &force, float duration) {
    if (!em.has_component<RigidBodyComponent>(id)) return;
    auto &rb = em.get_component<RigidBodyComponent>(id);
    float mag = glm::length(force);
    glm::vec2 dir = (mag > 0.0001f) ? (force / mag) : glm::vec2(0.0f, 1.0f);
    ActiveForce af;
    af.mode = ForceMode::ContinuousForce;
    af.direction = dir;
    af.max_magnitude = mag;
    af.duration = duration;
    af.curve = BezierCurve(EasingPreset::Linear);
    rb.active_forces.push_back(af);
  }

  static void apply_curve_force(EntityManager &em, EntityID id, const glm::vec2 &direction, float max_magnitude, float duration, const BezierCurve &curve) {
    if (!em.has_component<RigidBodyComponent>(id)) return;
    auto &rb = em.get_component<RigidBodyComponent>(id);
    ActiveForce af;
    af.mode = ForceMode::CurveOverTime;
    af.direction = (glm::length(direction) > 0.0001f) ? glm::normalize(direction) : glm::vec2(0.0f, 1.0f);
    af.max_magnitude = max_magnitude;
    af.duration = duration;
    af.curve = curve;
    rb.active_forces.push_back(af);
  }

  static void destroy_joint(EntityID id) {
    auto it = s_b2_joints.find(id);
    if (it != s_b2_joints.end()) {
      if (s_b2_world && it->second) {
        s_b2_world->DestroyJoint(it->second);
      }
      s_b2_joints.erase(it);
    }
  }

  static void update(EntityManager &entity_manager,
                     EventDispatcher &event_dispatcher, float delta_time) {
    if (!s_b2_world) {
      s_b2_world = std::make_unique<b2World>(
          b2Vec2(s_gravity.x * k_p2m, s_gravity.y * k_p2m));
      s_contact_listener =
          std::make_unique<PhysicsContactListener>(event_dispatcher);
      s_b2_world->SetContactListener(s_contact_listener.get());
    }

    // 1. Clean up destroyed joints FIRST (before destroying bodies to prevent double-free dangling pointers)
    for (auto it = s_b2_joints.begin(); it != s_b2_joints.end();) {
      bool a_alive = entity_manager.is_alive(it->first) && entity_manager.has_component<JointComponent>(it->first);
      bool b_alive = false;
      if (a_alive) {
        const auto &jc = entity_manager.get_component<JointComponent>(it->first);
        b_alive = entity_manager.is_alive(jc.target_entity);
      }

      if (!a_alive || !b_alive) {
        if (s_b2_world && it->second) {
          s_b2_world->DestroyJoint(it->second);
        }
        it = s_b2_joints.erase(it);
      } else {
        ++it;
      }
    }

    // 2. Clean up bodies for destroyed entities AFTER joints have been safely freed
    for (auto it = s_b2_bodies.begin(); it != s_b2_bodies.end();) {
      if (!entity_manager.is_alive(it->first)) {
        s_b2_world->DestroyBody(it->second);
        it = s_b2_bodies.erase(it);
      } else {
        ++it;
      }
    }

    // Synchronize entities into Box2D simulation world
    entity_manager.for_each<TransformComponent, RigidBodyComponent,
                            ColliderComponent>(
        [&entity_manager, delta_time](EntityID id, TransformComponent &transform,
                          RigidBodyComponent &rb, ColliderComponent &collider) {
          if (!transform.world_visible)
            return;

          b2Body *body = nullptr;
          auto it = s_b2_bodies.find(id);

          if (it == s_b2_bodies.end()) {
            body = create_body(entity_manager, id);
          } else {
            body = it->second;
          }

          if (!body) return;

          if (rb.body_type == BodyType::Kinematic) {
            body->SetLinearVelocity(
                b2Vec2(rb.velocity.x * k_p2m, rb.velocity.y * k_p2m));
            body->SetAngularVelocity(glm::radians(-rb.angular_velocity));
          } else if (rb.body_type == BodyType::Dynamic) {
            // Process active force curve envelopes
            for (auto &force : rb.active_forces) {
              if (force.finished) continue;

              force.elapsed_time += delta_time;
              float progress = (force.duration > 0.0f) ? std::clamp(force.elapsed_time / force.duration, 0.0f, 1.0f) : 1.0f;
              float curve_factor = force.curve.evaluate(progress);

              if (force.mode == ForceMode::Impulse) {
                b2Vec2 impulse(force.direction.x * force.max_magnitude * k_p2m,
                               force.direction.y * force.max_magnitude * k_p2m);
                body->ApplyLinearImpulseToCenter(impulse, true);
                b2Vec2 updated_vel = body->GetLinearVelocity();
                rb.velocity = glm::vec2(updated_vel.x * k_m2p, updated_vel.y * k_m2p);
                force.finished = true;
              } else if (force.mode == ForceMode::VelocityChange) {
                rb.velocity = force.direction * (force.max_magnitude * curve_factor);
                if (progress >= 1.0f) force.finished = true;
              } else { // ContinuousForce or CurveOverTime
                b2Vec2 force_vec(force.direction.x * (force.max_magnitude * curve_factor) * k_p2m,
                                 force.direction.y * (force.max_magnitude * curve_factor) * k_p2m);
                body->ApplyForceToCenter(force_vec, true);
                b2Vec2 updated_vel = body->GetLinearVelocity();
                rb.velocity = glm::vec2(updated_vel.x * k_m2p, updated_vel.y * k_m2p);
                if (progress >= 1.0f) force.finished = true;
              }
            }

            // Purge finished forces
            std::erase_if(rb.active_forces, [](const ActiveForce &f) { return f.finished; });

            body->SetLinearVelocity(
                b2Vec2(rb.velocity.x * k_p2m, rb.velocity.y * k_p2m));
          }
        });

    // Synchronize and initialize active JointComponent constraints
    entity_manager.for_each<JointComponent>(
        [&entity_manager](EntityID id, JointComponent &joint) {
          if (s_b2_joints.find(id) != s_b2_joints.end()) return;

          auto itA = s_b2_bodies.find(id);
          auto itB = s_b2_bodies.find(joint.target_entity);

          if (itA == s_b2_bodies.end() || !itA->second ||
              itB == s_b2_bodies.end() || !itB->second) {
            return;
          }

          b2Body *bodyA = itA->second;
          b2Body *bodyB = itB->second;

          b2Vec2 anchorA_m(joint.local_anchor_a.x * k_p2m, joint.local_anchor_a.y * k_p2m);
          b2Vec2 anchorB_m(joint.local_anchor_b.x * k_p2m, joint.local_anchor_b.y * k_p2m);

          b2Joint *b2_joint = nullptr;

          if (joint.type == JointType::Distance || joint.type == JointType::Rope) {
            b2DistanceJointDef def;
            b2Vec2 worldA = bodyA->GetWorldPoint(anchorA_m);
            b2Vec2 worldB = bodyB->GetWorldPoint(anchorB_m);
            def.Initialize(bodyA, bodyB, worldA, worldB);
            def.collideConnected = joint.collide_connected;

            if (joint.type == JointType::Rope || joint.distance_config.max_length >= 0.0f) {
              float max_l = (joint.distance_config.max_length >= 0.0f) ? joint.distance_config.max_length : (worldB - worldA).Length() * k_m2p;
              def.minLength = joint.distance_config.min_length * k_p2m;
              def.maxLength = max_l * k_p2m;
            } else if (joint.distance_config.length >= 0.0f) {
              def.length = joint.distance_config.length * k_p2m;
            }
            if (joint.distance_config.stiffness > 0.0f) {
              b2LinearStiffness(def.stiffness, def.damping,
                                joint.distance_config.stiffness,
                                joint.distance_config.damping,
                                bodyA, bodyB);
            }
            b2_joint = s_b2_world->CreateJoint(&def);
          } else if (joint.type == JointType::Revolute) {
            b2RevoluteJointDef def;
            b2Vec2 worldAnchor = bodyA->GetWorldPoint(anchorA_m);
            def.Initialize(bodyA, bodyB, worldAnchor);
            def.localAnchorA = anchorA_m;
            def.localAnchorB = anchorB_m;
            def.collideConnected = joint.collide_connected;
            def.enableLimit = joint.revolute_config.enable_limit;
            def.lowerAngle = glm::radians(joint.revolute_config.lower_angle);
            def.upperAngle = glm::radians(joint.revolute_config.upper_angle);
            def.enableMotor = joint.revolute_config.enable_motor;
            def.motorSpeed = glm::radians(-joint.revolute_config.motor_speed);
            def.maxMotorTorque = joint.revolute_config.max_motor_torque;
            b2_joint = s_b2_world->CreateJoint(&def);
          } else if (joint.type == JointType::Prismatic) {
            b2PrismaticJointDef def;
            b2Vec2 worldAnchor = bodyA->GetWorldPoint(anchorA_m);
            b2Vec2 worldAxis(joint.prismatic_config.axis.x, -joint.prismatic_config.axis.y);
            def.Initialize(bodyA, bodyB, worldAnchor, worldAxis);
            def.localAnchorA = anchorA_m;
            def.localAnchorB = anchorB_m;
            def.collideConnected = joint.collide_connected;
            def.enableLimit = joint.prismatic_config.enable_limit;
            def.lowerTranslation = joint.prismatic_config.lower_translation * k_p2m;
            def.upperTranslation = joint.prismatic_config.upper_translation * k_p2m;
            def.enableMotor = joint.prismatic_config.enable_motor;
            def.motorSpeed = joint.prismatic_config.motor_speed * k_p2m;
            def.maxMotorForce = joint.prismatic_config.max_motor_force;
            b2_joint = s_b2_world->CreateJoint(&def);
          }

          if (b2_joint) {
            s_b2_joints[id] = b2_joint;
          }
        });

    // Step physics simulation world
    s_b2_world->Step(delta_time, 8, 3);

    // Synchronize positions and velocities back to ECS
    for (auto &[id, body] : s_b2_bodies) {
      if (!entity_manager.is_alive(id))
        continue;
      if (!entity_manager.has_component<TransformComponent>(id) ||
          !entity_manager.has_component<RigidBodyComponent>(id))
        continue;

      auto &transform = entity_manager.get_component<TransformComponent>(id);
      auto &rb = entity_manager.get_component<RigidBodyComponent>(id);

      if (rb.body_type != BodyType::Static) {
        b2Vec2 pos = body->GetPosition();
        b2Vec2 vel = body->GetLinearVelocity();

        transform.position.x = pos.x * k_m2p;
        transform.position.y = pos.y * k_m2p;
        rb.velocity = glm::vec2(vel.x * k_m2p, vel.y * k_m2p);

        if (!rb.fixed_rotation) {
          transform.rotation = -glm::degrees(body->GetAngle());
          rb.angular_velocity = -glm::degrees(body->GetAngularVelocity());
        }
      }
    }

    capture_debug_frame();
  }

  static void set_angular_velocity(EntityManager &em, EntityID id, float degrees_per_sec) {
    if (!em.has_component<RigidBodyComponent>(id)) return;
    auto &rb = em.get_component<RigidBodyComponent>(id);
    rb.angular_velocity = degrees_per_sec;
    auto it = s_b2_bodies.find(id);
    if (it != s_b2_bodies.end() && it->second) {
      it->second->SetAngularVelocity(glm::radians(degrees_per_sec));
    }
  }

  static void apply_angular_impulse(EntityManager &em, EntityID id, float impulse) {
    if (!em.has_component<RigidBodyComponent>(id)) return;
    auto &rb = em.get_component<RigidBodyComponent>(id);
    auto it = s_b2_bodies.find(id);
    if (it != s_b2_bodies.end() && it->second) {
      it->second->ApplyAngularImpulse(impulse * k_p2m, true);
      rb.angular_velocity = glm::degrees(it->second->GetAngularVelocity());
    }
  }

private:
  static void capture_debug_frame() {
    if (!s_debug_options.enabled() || !s_b2_world) return;
    PhysicsDebugFrame frame;
    frame.revision = ++s_debug_revision;
    for (const auto& [id, body] : s_b2_bodies) {
      if (!body) continue;
      const b2Vec2 pos = body->GetPosition();
      const b2Vec2 com = body->GetWorldCenter();
      const b2Vec2 velocity = body->GetLinearVelocity();
      frame.bodies.push_back({id, static_cast<int>(body->GetType()),
          {pos.x * k_m2p, pos.y * k_m2p}, {com.x * k_m2p, com.y * k_m2p},
          {velocity.x * k_m2p, velocity.y * k_m2p}, -glm::degrees(body->GetAngularVelocity())});
      for (b2Fixture* fixture = body->GetFixtureList(); fixture; fixture = fixture->GetNext()) {
        const b2Filter filter = fixture->GetFilterData();
        if (s_debug_options.category_filter && !(filter.categoryBits & s_debug_options.category_filter)) continue;
        if (s_debug_options.mask_filter && !(filter.maskBits & s_debug_options.mask_filter)) continue;
        PhysicsDebugCollider collider;
        collider.entity = id; collider.trigger = fixture->IsSensor();
        collider.one_way = fixture->GetUserData().pointer == 1;
        collider.category = filter.categoryBits; collider.mask = filter.maskBits;
        const b2Shape* shape = fixture->GetShape();
        if (shape->GetType() == b2Shape::e_circle) {
          const auto* circle = static_cast<const b2CircleShape*>(shape);
          collider.type = PhysicsDebugShapeType::Circle;
          const b2Vec2 center = body->GetWorldPoint(circle->m_p);
          collider.points.push_back({center.x * k_m2p, center.y * k_m2p});
          collider.radius = circle->m_radius * k_m2p;
        } else if (shape->GetType() == b2Shape::e_polygon) {
          const auto* polygon = static_cast<const b2PolygonShape*>(shape);
          collider.type = PhysicsDebugShapeType::Polygon;
          for (int i = 0; i < polygon->m_count; ++i) {
            const b2Vec2 point = body->GetWorldPoint(polygon->m_vertices[i]);
            collider.points.push_back({point.x * k_m2p, point.y * k_m2p});
          }
        }
        frame.colliders.push_back(std::move(collider));
      }
    }
    if (s_debug_options.contacts || s_debug_options.normals) {
      for (const b2Contact* contact = s_b2_world->GetContactList(); contact; contact = contact->GetNext()) {
        if (!contact->IsTouching()) continue;
        b2WorldManifold manifold; contact->GetWorldManifold(&manifold);
        const b2Fixture* a = contact->GetFixtureA(); const b2Fixture* b = contact->GetFixtureB();
        frame.contacts.push_back({
            static_cast<EntityID>(const_cast<b2Body*>(a->GetBody())->GetUserData().pointer),
            static_cast<EntityID>(const_cast<b2Body*>(b->GetBody())->GetUserData().pointer),
            {manifold.points[0].x * k_m2p, manifold.points[0].y * k_m2p},
            {manifold.normal.x, manifold.normal.y}, a->IsSensor() || b->IsSensor()
        });
      }
    }
    s_debug_frame = std::move(frame);
  }

  static b2Body *create_body(EntityManager &em, EntityID id) {
    if (!s_b2_world || !em.is_alive(id) ||
        !em.has_component<TransformComponent>(id) ||
        !em.has_component<RigidBodyComponent>(id) ||
        !em.has_component<ColliderComponent>(id)) {
      return nullptr;
    }

    auto &transform = em.get_component<TransformComponent>(id);
    auto &rb = em.get_component<RigidBodyComponent>(id);
    auto &collider = em.get_component<ColliderComponent>(id);
    if (!transform.world_visible) return nullptr;

    b2BodyDef body_def;
    body_def.type = rb.body_type == BodyType::Static
        ? b2_staticBody
        : rb.body_type == BodyType::Kinematic ? b2_kinematicBody : b2_dynamicBody;
    body_def.position.Set(transform.world_position.x * k_p2m,
                         transform.world_position.y * k_p2m);
    body_def.angle = glm::radians(-transform.world_rotation);
    body_def.fixedRotation = rb.fixed_rotation;
    body_def.gravityScale = rb.gravity_scale;
    body_def.linearVelocity.Set(rb.velocity.x * k_p2m, rb.velocity.y * k_p2m);
    body_def.angularVelocity = glm::radians(-rb.angular_velocity);
    body_def.userData.pointer = static_cast<uintptr_t>(id);

    b2Body *body = s_b2_world->CreateBody(&body_def);
    for (const auto &shape : collider.shapes) {
      b2FixtureDef fixture_def;
      fixture_def.isSensor = shape.is_trigger;
      fixture_def.density = rb.mass;
      fixture_def.friction = rb.friction;
      fixture_def.restitution = rb.restitution;
      fixture_def.filter.categoryBits = shape.category_bits;
      fixture_def.filter.maskBits = shape.mask_bits;
      fixture_def.userData.pointer = shape.is_one_way ? 1 : 0;

      if (shape.type == ColliderShapeType::Circle) {
        b2CircleShape circle;
        const glm::vec2 offset = collider_scaled_offset(shape, transform);
        circle.m_p.Set(offset.x * k_p2m, offset.y * k_p2m);
        circle.m_radius = collider_scaled_circle_radius(shape, transform) * k_p2m;
        fixture_def.shape = &circle;
        body->CreateFixture(&fixture_def);
      } else if (shape.type == ColliderShapeType::Box) {
        b2PolygonShape box;
        const glm::vec2 half = collider_scaled_box_half_extent(shape, transform) * k_p2m;
        const glm::vec2 offset = collider_scaled_offset(shape, transform) * k_p2m;
        box.SetAsBox(half.x, half.y,
                     b2Vec2(offset.x, offset.y),
                     0.0f);
        fixture_def.shape = &box;
        body->CreateFixture(&fixture_def);
      } else if (shape.type == ColliderShapeType::Polygon && shape.vertices.size() >= 3) {
        b2PolygonShape polygon;
        std::vector<b2Vec2> vertices;
        const auto scaled_vertices = collider_scaled_polygon_vertices(shape, transform);
        vertices.reserve(scaled_vertices.size());
        for (const auto &vertex : scaled_vertices) {
          vertices.emplace_back(vertex.x * k_p2m, vertex.y * k_p2m);
        }
        polygon.Set(vertices.data(), static_cast<int32>(vertices.size()));
        fixture_def.shape = &polygon;
        body->CreateFixture(&fixture_def);
      }
    }

    s_b2_bodies[id] = body;
    return body;
  }

  static inline glm::vec2 s_gravity{0.0f, 0.0f};
  static inline std::unique_ptr<b2World> s_b2_world{nullptr};
  static inline std::unique_ptr<PhysicsContactListener> s_contact_listener{
      nullptr};
  static inline std::unordered_map<EntityID, b2Body *> s_b2_bodies;
  static inline std::unordered_map<EntityID, b2Joint *> s_b2_joints;
  static inline std::unordered_set<EntityID> s_ignore_one_way;
  static inline PhysicsDebugOptions s_debug_options{};
  static inline PhysicsDebugFrame s_debug_frame{};
  static inline std::uint64_t s_debug_revision{0};
};

inline void PhysicsContactListener::PreSolve(b2Contact *contact, const b2Manifold *oldManifold) {
  b2Fixture *fixtureA = contact->GetFixtureA();
  b2Fixture *fixtureB = contact->GetFixtureB();
  if (!fixtureA || !fixtureB) return;

  bool is_one_way_A = (fixtureA->GetUserData().pointer == 1);
  bool is_one_way_B = (fixtureB->GetUserData().pointer == 1);

  if (!is_one_way_A && !is_one_way_B) return;

  b2Fixture *platform_fixture = is_one_way_A ? fixtureA : fixtureB;
  b2Fixture *other_fixture    = is_one_way_A ? fixtureB : fixtureA;

  b2Body *other_body = other_fixture->GetBody();
  if (other_body->GetType() != b2_dynamicBody) return;

  EntityID other_id = static_cast<EntityID>(other_body->GetUserData().pointer);
  if (PhysicsSystem::is_ignoring_one_way(other_id)) {
    contact->SetEnabled(false);
    return;
  }

  b2AABB platform_aabb;
  platform_fixture->GetShape()->ComputeAABB(&platform_aabb, platform_fixture->GetBody()->GetTransform(), 0);
  float platform_top_y = platform_aabb.upperBound.y;

  b2AABB other_aabb;
  other_fixture->GetShape()->ComputeAABB(&other_aabb, other_body->GetTransform(), 0);
  float other_bottom_y = other_aabb.lowerBound.y;

  float other_vel_y = other_body->GetLinearVelocity().y;

  // Pass through if body's feet are below the platform top (with 0.05m tolerance) OR moving upward (+Y)
  if (other_bottom_y < platform_top_y - 0.05f || other_vel_y > 0.05f) {
    contact->SetEnabled(false);
  }
}

} // namespace Engine
