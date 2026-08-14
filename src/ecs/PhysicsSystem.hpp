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

private:
  EventDispatcher &m_dispatcher;
};

class PhysicsSystem {
public:
  static constexpr float k_p2m = 0.01f;  // Pixels to meters scale factor
  static constexpr float k_m2p = 100.0f; // Meters to pixels scale factor

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
        [&entity_manager](EntityID id, TransformComponent &transform,
                          RigidBodyComponent &rb, ColliderComponent &collider) {
          if (!transform.world_visible)
            return;

          b2Body *body = nullptr;
          auto it = s_b2_bodies.find(id);

          if (it == s_b2_bodies.end()) {
            b2BodyDef bodyDef;
            bodyDef.type = rb.is_static ? b2_staticBody : b2_dynamicBody;
            bodyDef.position.Set(transform.world_position.x * k_p2m,
                                 transform.world_position.y * k_p2m);
            bodyDef.angle = glm::radians(transform.world_rotation);
            bodyDef.fixedRotation = true;
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

          if (!rb.is_static) {
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

      if (!rb.is_static) {
        b2Vec2 pos = body->GetPosition();
        b2Vec2 vel = body->GetLinearVelocity();

        transform.position.x = pos.x * k_m2p;
        transform.position.y = pos.y * k_m2p;
        rb.velocity = glm::vec2(vel.x * k_m2p, vel.y * k_m2p);
      }
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
