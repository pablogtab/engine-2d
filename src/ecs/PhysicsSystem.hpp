#pragma once

#include "../events/Event.hpp"
#include "../events/EventDispatcher.hpp"
#include "Components.hpp"
#include "EntityManager.hpp"
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

  // Cast a ray from origin to destination and return the closest hit result
  static RaycastHit2D raycast(const glm::vec2 &origin, const glm::vec2 &destination,
                              uint16_t mask_bits = CollisionCategory::ALL,
                              bool ignore_sensors = true, EntityID ignore_entity = 0) {
    if (!s_b2_world) return {};

    RayCastClosestCallback callback;
    callback.mask_bits = mask_bits;
    callback.ignore_sensors = ignore_sensors;
    callback.ignore_entity = ignore_entity;

    b2Vec2 p1(origin.x * k_p2m, origin.y * k_p2m);
    b2Vec2 p2(destination.x * k_p2m, destination.y * k_p2m);

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

  // Cast a ray along a direction vector with a maximum distance
  static RaycastHit2D raycast_direction(const glm::vec2 &origin, const glm::vec2 &direction,
                                        float max_distance,
                                        uint16_t mask_bits = CollisionCategory::ALL,
                                        bool ignore_sensors = true, EntityID ignore_entity = 0) {
    float len = glm::length(direction);
    glm::vec2 norm_dir = (len > 0.0001f) ? (direction / len) : glm::vec2(1.0f, 0.0f);
    glm::vec2 dest = origin + norm_dir * max_distance;
    return raycast(origin, dest, mask_bits, ignore_sensors, ignore_entity);
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

  static void clear() {
    s_b2_bodies.clear();
    s_ignore_one_way.clear();
    s_contact_listener.reset();
    s_b2_world.reset();
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

  static void update(EntityManager &entity_manager,
                     EventDispatcher &event_dispatcher, float delta_time) {
    if (!s_b2_world) {
      s_b2_world = std::make_unique<b2World>(
          b2Vec2(s_gravity.x * k_p2m, s_gravity.y * k_p2m));
      s_contact_listener =
          std::make_unique<PhysicsContactListener>(event_dispatcher);
      s_b2_world->SetContactListener(s_contact_listener.get());
    }

    // Clean up bodies for destroyed entities
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
            b2BodyDef bodyDef;
            if (rb.body_type == BodyType::Static) {
              bodyDef.type = b2_staticBody;
            } else if (rb.body_type == BodyType::Kinematic) {
              bodyDef.type = b2_kinematicBody;
            } else {
              bodyDef.type = b2_dynamicBody;
            }

            bodyDef.position.Set(transform.world_position.x * k_p2m,
                                 transform.world_position.y * k_p2m);
            bodyDef.angle = glm::radians(-transform.world_rotation);
            bodyDef.fixedRotation = rb.fixed_rotation;
            bodyDef.userData.pointer = static_cast<uintptr_t>(id);

            body = s_b2_world->CreateBody(&bodyDef);

            for (const auto &shape : collider.shapes) {
              b2FixtureDef fixtureDef;
              fixtureDef.isSensor = shape.is_trigger;
              fixtureDef.density = rb.mass;
              fixtureDef.friction = rb.friction;
              fixtureDef.restitution = rb.restitution;
              fixtureDef.filter.categoryBits = shape.category_bits;
              fixtureDef.filter.maskBits = shape.mask_bits;
              fixtureDef.userData.pointer = shape.is_one_way ? 1 : 0;

              if (shape.type == ColliderShapeType::Circle) {
                b2CircleShape circle;
                circle.m_p.Set(shape.offset.x * k_p2m, shape.offset.y * k_p2m);
                circle.m_radius =
                    (shape.radius * transform.world_scale.x) * k_p2m;
                fixtureDef.shape = &circle;
                body->CreateFixture(&fixtureDef);
              } else if (shape.type == ColliderShapeType::Box) {
                b2PolygonShape box;
                glm::vec2 half =
                    (shape.size * transform.world_scale) * 0.5f * k_p2m;
                box.SetAsBox(
                    half.x, half.y,
                    b2Vec2(shape.offset.x * k_p2m, shape.offset.y * k_p2m),
                    0.0f);
                fixtureDef.shape = &box;
                body->CreateFixture(&fixtureDef);
              } else if (shape.type == ColliderShapeType::Polygon) {
                b2PolygonShape polygon;
                std::vector<b2Vec2> b2_verts;
                b2_verts.reserve(shape.vertices.size());
                for (const auto &v : shape.vertices) {
                  b2_verts.emplace_back((v.x * transform.world_scale.x + shape.offset.x) * k_p2m,
                                        (v.y * transform.world_scale.y + shape.offset.y) * k_p2m);
                }
                if (b2_verts.size() >= 3) {
                  polygon.Set(b2_verts.data(), static_cast<int32>(b2_verts.size()));
                  fixtureDef.shape = &polygon;
                  body->CreateFixture(&fixtureDef);
                }
              }
            }

            s_b2_bodies[id] = body;
          } else {
            body = it->second;
          }

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
  static inline glm::vec2 s_gravity{0.0f, 0.0f};
  static inline std::unique_ptr<b2World> s_b2_world{nullptr};
  static inline std::unique_ptr<PhysicsContactListener> s_contact_listener{
      nullptr};
  static inline std::unordered_map<EntityID, b2Body *> s_b2_bodies;
  static inline std::unordered_set<EntityID> s_ignore_one_way;
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
