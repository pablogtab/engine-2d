#pragma once

#include "../events/Event.hpp"
#include "../events/EventDispatcher.hpp"
#include "Components.hpp"
#include "EntityManager.hpp"
#include <box2d/box2d.h>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

namespace Engine {

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

private:
  EventDispatcher &m_dispatcher;
};

class PhysicsSystem {
public:
  static constexpr float k_p2m = 0.01f;  // Pixels to meters scale factor
  static constexpr float k_m2p = 100.0f; // Meters to pixels scale factor

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

  static void set_gravity(const glm::vec2 &gravity) {
    s_gravity = gravity;
    if (s_b2_world) {
      s_b2_world->SetGravity(b2Vec2(gravity.x * k_p2m, gravity.y * k_p2m));
    }
  }

  static const glm::vec2 &get_gravity() { return s_gravity; }

  static void clear() {
    s_b2_bodies.clear();
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
};

} // namespace Engine
