#include "AnimatedSpriteDemoScene.hpp"
#include "../core/Input.hpp"
#include "../core/Log.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../ecs/RenderSystem.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../renderer/Renderer2D.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>

namespace Sandbox {

AnimatedSpriteDemoScene::AnimatedSpriteDemoScene(SDL_Renderer *renderer)
    : Engine::Scene("2D Animated Sprites & Asset Loader"),
      m_sdl_renderer(renderer), m_camera(1280.0f, 720.0f) {}

Engine::Texture *AnimatedSpriteDemoScene::generate_procedural_spritesheet() {
  // Generates a 6-column x 3-row SpriteSheet (frame size: 64x64 px -> texture
  // size: 384x192 px)
  const int frame_w = 64;
  const int frame_h = 64;
  const int cols = 6;
  const int rows = 3;
  const int tex_w = frame_w * cols;
  const int tex_h = frame_h * rows;

  std::vector<uint32_t> pixels(tex_w * tex_h, 0); // Transparent background

  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      int frame_idx = r * cols + c;
      int start_x = c * frame_w;
      int start_y = r * frame_h;

      // Base color depending on row: Row 0 = Idle (Blue/Cyan), Row 1 = Run
      // (Green/Yellow), Row 2 = Spin (Magenta/Orange)
      uint8_t base_r = (r == 2) ? 255 : ((r == 1) ? 100 : 40);
      uint8_t base_g = (r == 1) ? 220 : ((r == 2) ? 60 : 180);
      uint8_t base_b = (r == 0) ? 255 : ((r == 2) ? 220 : 60);

      // Draw animated character body inside frame
      for (int y = 0; y < frame_w; ++y) {
        for (int x = 0; x < frame_h; ++x) {
          float cx = x - frame_w * 0.5f;
          float cy = y - frame_h * 0.5f;

          // Bounce offset per frame
          float anim_phase = (float)c / (float)cols * 3.14159f * 2.0f;
          if (r == 1)
            cy += std::sin(anim_phase) * 6.0f; // Running bounce
          if (r == 2) {
            // Rotating offset
            float rot_x = cx * std::cos(anim_phase) - cy * std::sin(anim_phase);
            float rot_y = cx * std::sin(anim_phase) + cy * std::cos(anim_phase);
            cx = rot_x;
            cy = rot_y;
          }

          float dist_sq = cx * cx + cy * cy;

          // Outer body circle
          if (dist_sq <= 22.0f * 22.0f) {
            float dist = std::sqrt(dist_sq);
            float alpha_f = (dist > 20.5f) ? (22.0f - dist) / 1.5f : 1.0f;
            uint8_t a =
                static_cast<uint8_t>(255 * std::clamp(alpha_f, 0.0f, 1.0f));

            // Eye dot on right side
            if (std::abs(cx - 8.0f) < 4.0f && std::abs(cy - (-4.0f)) < 4.0f) {
              pixels[(start_y + y) * tex_w + (start_x + x)] =
                  (255 << 24) | (255 << 16) | (255 << 8) | a;
            } else {
              pixels[(start_y + y) * tex_w + (start_x + x)] =
                  (base_r << 24) | (base_g << 16) | (base_b << 8) | a;
            }
          }
        }
      }
    }
  }

  return new Engine::Texture(m_sdl_renderer, tex_w, tex_h, pixels.data());
}

void AnimatedSpriteDemoScene::on_detach() {
  ENGINE_LOG_INFO("[ANIMATED SPRITE DEMO] Detaching scene...");
  m_entity_manager.clear();
  Engine::PhysicsSystem::clear();
}

void AnimatedSpriteDemoScene::on_attach() {
  ENGINE_LOG_INFO(
      "[ANIMATED SPRITE DEMO] Attaching AnimatedSpriteDemoScene...");
  m_entity_manager.clear();

  // Reset Physics World & Gravity for Top-Down Navigation Demo (Zero Gravity)
  Engine::PhysicsSystem::clear();
  Engine::PhysicsSystem::set_gravity(glm::vec2(0.0f, 0.0f));

  // Attempt to load real PNG asset from assets/textures/ folder
  Engine::Texture *file_tex = Engine::Texture::load_from_file(
      m_sdl_renderer, "assets/textures/player.png");

  if (file_tex) {
    m_spritesheet_texture = std::shared_ptr<Engine::Texture>(file_tex);
    // Created a 4-col x 4-row PNG sprite sheet
    m_spritesheet = std::make_unique<Engine::SpriteSheet>(
        m_spritesheet_texture.get(), 4, 4);

    m_anim_idle.set_spritesheet(m_spritesheet.get());
    m_anim_idle.set_clip(0, 3, 6.0f, true); // Row 0

    m_anim_run.set_spritesheet(m_spritesheet.get());
    m_anim_run.set_clip(4, 7, 10.0f, true); // Row 1

    m_anim_spin.set_spritesheet(m_spritesheet.get());
    m_anim_spin.set_clip(8, 15, 14.0f, true); // Rows 2 & 3
  } else {
    // Fallback to procedural generator if PNG asset is missing
    m_spritesheet_texture =
        std::shared_ptr<Engine::Texture>(generate_procedural_spritesheet());
    m_spritesheet = std::make_unique<Engine::SpriteSheet>(
        m_spritesheet_texture.get(), 6, 3);

    m_anim_idle.set_spritesheet(m_spritesheet.get());
    m_anim_idle.set_clip(0, 5, 8.0f, true);

    m_anim_run.set_spritesheet(m_spritesheet.get());
    m_anim_run.set_clip(6, 11, 12.0f, true);

    m_anim_spin.set_spritesheet(m_spritesheet.get());
    m_anim_spin.set_clip(12, 17, 16.0f, true);
  }

  m_bg_texture = std::shared_ptr<Engine::Texture>(
      Engine::Texture::create_grid(m_sdl_renderer, 64, 40, 50, 70));
  m_tree_texture = std::shared_ptr<Engine::Texture>(
      Engine::Texture::create_circle(m_sdl_renderer, 64, 50, 160, 90));

  m_current_anim = &m_anim_idle;

  // 1. Register ECS Component Lifecycle Callbacks
  m_entity_manager.on_construct<Engine::TagComponent>([](Engine::EntityID id,
                                                         Engine::TagComponent
                                                             &tag) {
    ENGINE_LOG_INFO(
        "[ECS LIFECYCLE HOOK] Component constructed on Entity #{}: Tag='{}'",
        id, tag.tag);
  });

  // 2. Subscribe Event Listeners
  m_event_dispatcher.subscribe<Engine::CustomLogEvent>(
      [](const Engine::CustomLogEvent &ev) {
        ENGINE_LOG_INFO("[EVENT BUS] Subscriber received event: '{}'",
                        ev.message);
      });

  m_event_dispatcher.subscribe<Engine::CollisionEvent>(
      [this](const Engine::CollisionEvent &ev) {
        std::string tag_a =
            m_entity_manager.has_component<Engine::TagComponent>(ev.entity_a)
                ? m_entity_manager
                      .get_component<Engine::TagComponent>(ev.entity_a)
                      .tag
                : "Entity_" + std::to_string(ev.entity_a);
        std::string tag_b =
            m_entity_manager.has_component<Engine::TagComponent>(ev.entity_b)
                ? m_entity_manager
                      .get_component<Engine::TagComponent>(ev.entity_b)
                      .tag
                : "Entity_" + std::to_string(ev.entity_b);

        ENGINE_LOG_INFO("[PHYSICS EVENT BUS] Collision Detected: '{}' (#{}) "
                        "<---> '{}' (#{}) | Trigger Sensor: {}",
                        tag_a, ev.entity_a, tag_b, ev.entity_b,
                        ev.is_trigger ? "YES" : "NO");
      });

  // 3. Instantiate Environment Trees as ECS Entities with Circle Colliders
  Engine::EntityID tree1 = m_entity_manager.create_entity();
  m_entity_manager.add_component<Engine::TagComponent>(
      tree1, Engine::TagComponent{"Tree_Left"});
  m_entity_manager.add_component<Engine::TransformComponent>(
      tree1, Engine::TransformComponent{glm::vec3(-200.0f, -80.0f, 0.0f)});
  m_entity_manager.add_component<Engine::SpriteComponent>(
      tree1,
      Engine::SpriteComponent{m_tree_texture.get(), glm::vec2(100.0f, 120.0f)});
  m_entity_manager.add_component<Engine::ColliderComponent>(
      tree1,
      Engine::ColliderComponent::Circle(50.0f)); // Circular tree collider!
  m_entity_manager.add_component<Engine::RigidBodyComponent>(
      tree1, Engine::RigidBodyComponent::Static()); // Solid static tree

  Engine::EntityID tree2 = m_entity_manager.create_entity();
  m_entity_manager.add_component<Engine::TagComponent>(
      tree2, Engine::TagComponent{"Tree_Right"});
  m_entity_manager.add_component<Engine::TransformComponent>(
      tree2, Engine::TransformComponent{glm::vec3(180.0f, 100.0f, 0.0f)});
  m_entity_manager.add_component<Engine::SpriteComponent>(
      tree2,
      Engine::SpriteComponent{m_tree_texture.get(), glm::vec2(110.0f, 130.0f)});
  m_entity_manager.add_component<Engine::ColliderComponent>(
      tree2,
      Engine::ColliderComponent::Circle(55.0f)); // Circular tree collider!
  m_entity_manager.add_component<Engine::RigidBodyComponent>(
      tree2, Engine::RigidBodyComponent::Static());

  // 3.b Solid Box / Square Obstacles
  Engine::EntityID box1 = m_entity_manager.create_entity();
  m_entity_manager.add_component<Engine::TagComponent>(
      box1, Engine::TagComponent{"SolidSquare_Left"});
  m_entity_manager.add_component<Engine::TransformComponent>(
      box1, Engine::TransformComponent{glm::vec3(-120.0f, 120.0f, 0.0f)});
  m_entity_manager.add_component<Engine::SpriteComponent>(
      box1, Engine::SpriteComponent{m_bg_texture.get(),
                                    glm::vec2(90.0f, 90.0f),
                                    {200, 100, 80, 255}});
  m_entity_manager.add_component<Engine::ColliderComponent>(
      box1, Engine::ColliderComponent::Box(glm::vec2(90.0f, 90.0f)));
  m_entity_manager.add_component<Engine::RigidBodyComponent>(
      box1, Engine::RigidBodyComponent::Static()); // Solid static box

  Engine::EntityID box2 = m_entity_manager.create_entity();
  m_entity_manager.add_component<Engine::TagComponent>(
      box2, Engine::TagComponent{"SolidSquare_Right"});
  m_entity_manager.add_component<Engine::TransformComponent>(
      box2, Engine::TransformComponent{glm::vec3(220.0f, -120.0f, 0.0f)});
  m_entity_manager.add_component<Engine::SpriteComponent>(
      box2, Engine::SpriteComponent{m_bg_texture.get(),
                                    glm::vec2(110.0f, 80.0f),
                                    {80, 120, 200, 255}});
  m_entity_manager.add_component<Engine::ColliderComponent>(
      box2, Engine::ColliderComponent::Box(glm::vec2(110.0f, 80.0f)));
  m_entity_manager.add_component<Engine::RigidBodyComponent>(
      box2, Engine::RigidBodyComponent::Static()); // Solid static box

  // 4. Create Player Entity in ECS with Circle Collider (26px radius matching
  // circular character body)
  m_player_entity = m_entity_manager.create_entity();
  m_entity_manager.add_component<Engine::TagComponent>(
      m_player_entity, Engine::TagComponent{"HeroPlayer"});
  m_entity_manager.add_component<Engine::TransformComponent>(
      m_player_entity, Engine::TransformComponent{glm::vec3(0.0f, 0.0f, 0.0f)});
  m_entity_manager.add_component<Engine::SpriteComponent>(
      m_player_entity,
      Engine::SpriteComponent{nullptr, glm::vec2(64.0f, 64.0f)});
  m_entity_manager.add_component<Engine::ColliderComponent>(
      m_player_entity, Engine::ColliderComponent::Circle(26.0f));
  m_entity_manager.add_component<Engine::RigidBodyComponent>(
      m_player_entity,
      Engine::RigidBodyComponent::Dynamic(0.0f, 0.2f)); // Dynamic body

  // 5. Create Child Pet/Orb Entity attached to Player (Parent-Child Hierarchy!)
  m_child_orb = m_entity_manager.create_entity();
  m_entity_manager.add_component<Engine::TagComponent>(
      m_child_orb, Engine::TagComponent{"PlayerPetOrb"});
  m_entity_manager.add_component<Engine::TransformComponent>(
      m_child_orb, Engine::TransformComponent{glm::vec3(55.0f, 0.0f, 5.0f),
                                              glm::vec2(0.5f, 0.5f)});
  m_entity_manager.add_component<Engine::SpriteComponent>(
      m_child_orb, Engine::SpriteComponent{m_tree_texture.get(),
                                           glm::vec2(50.0f, 50.0f),
                                           {255, 215, 0, 255}});

  // Set Parent Relationship!
  m_entity_manager.set_parent(m_child_orb, m_player_entity);

  ENGINE_LOG_INFO("ECS Hierarchy initialized. Living entities: {}",
                  m_entity_manager.get_living_entity_count());

  std::cout << "\n======================================================="
            << std::endl;
  std::cout << " [ANIMATED SPRITE & WORLD-CLASS ECS DEMO ACTIVE]" << std::endl;
  std::cout << " - WASD / Arrow Keys : Move Character in 2.5D Space"
            << std::endl;
  std::cout << " - RIGHT CLICK       : Spawn New ECS Entity in 2.5D Space"
            << std::endl;
  std::cout << " - C KEY             : Clone Orb Entity via clone_entity API"
            << std::endl;
  std::cout << " - X KEY             : Bulk Purge Spawned Entities via "
               "destroy_matching API"
            << std::endl;
  std::cout << " - SPACE / SHIFT    : Trigger Spin & Dispatch ECS Event"
            << std::endl;
  std::cout << " - TAB               : Switch Test Scenes" << std::endl;
  std::cout << "=======================================================\n"
            << std::endl;
}

void AnimatedSpriteDemoScene::on_resize(int width, int height) {
  m_camera.set_viewport_size(1280.0f, 720.0f);
}

void AnimatedSpriteDemoScene::on_update(float delta_time) {
  float move_speed = 300.0f;

  float move_x = Engine::Input::get_axis("Horizontal");
  float move_y = Engine::Input::get_axis("Vertical");

  // Arrow keys fallback / addition (+Y is UP, -Y is DOWN)
  if (Engine::Input::is_key_pressed(SDL_SCANCODE_LEFT))
    move_x -= 1.0f;
  if (Engine::Input::is_key_pressed(SDL_SCANCODE_RIGHT))
    move_x += 1.0f;
  if (Engine::Input::is_key_pressed(SDL_SCANCODE_UP))
    move_y += 1.0f;
  if (Engine::Input::is_key_pressed(SDL_SCANCODE_DOWN))
    move_y -= 1.0f;

  move_x = std::clamp(move_x, -1.0f, 1.0f);
  move_y = std::clamp(move_y, -1.0f, 1.0f);

  glm::vec2 target_velocity{move_x * move_speed, move_y * move_speed};
  bool is_moving = (target_velocity.x != 0.0f || target_velocity.y != 0.0f);
  if (target_velocity.x < 0.0f)
    m_facing_right = false;
  if (target_velocity.x > 0.0f)
    m_facing_right = true;

  if (m_entity_manager.has_component<Engine::RigidBodyComponent>(
          m_player_entity)) {
    auto &rb = m_entity_manager.get_component<Engine::RigidBodyComponent>(
        m_player_entity);
    rb.velocity = target_velocity;
  } else {
    m_player_pos.x += target_velocity.x * delta_time;
    m_player_pos.y += target_velocity.y * delta_time;
  }

  // L Key: Log exact player position coordinates
  if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_L)) {
    ENGINE_LOG_INFO("[PLAYER POSITION LOG] Exact Player Position: X={:.5f}, "
                    "Y={:.5f}, Z={:.1f}",
                    m_player_pos.x, m_player_pos.y, m_player_pos.z);
  }

  // G Key: Toggle Visual Debug Collider Overlays
  if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_G)) {
    m_show_debug_colliders = !m_show_debug_colliders;
    ENGINE_LOG_INFO("[DEBUG COLLIDERS OVERLAY] Visual Collider Wireframes: {}",
                    m_show_debug_colliders ? "ON (Red=Solid, Cyan=Trigger)"
                                           : "OFF");
  }

  // SPACE / Jump: Impulse physical jump via RigidBodyComponent velocity
  if (Engine::Input::is_action_just_pressed("Jump") ||
      Engine::Input::is_key_just_pressed(SDL_SCANCODE_SPACE)) {
    if (m_entity_manager.has_component<Engine::RigidBodyComponent>(
            m_player_entity)) {
      auto &rb = m_entity_manager.get_component<Engine::RigidBodyComponent>(
          m_player_entity);
      rb.velocity.y = 350.0f; // Impulse jump
      m_spin_timer = 0.5f;
      ENGINE_LOG_INFO(
          "[PHYSICS IMPULSE] Jump impulse applied! Velocity Y set to 350.0");
    }
  }

  // K Key: Test Squash & Stretch (Skew + Scale)
  if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_K)) {
    if (m_entity_manager.has_component<Engine::TransformComponent>(
            m_player_entity)) {
      auto &transform =
          m_entity_manager.get_component<Engine::TransformComponent>(
              m_player_entity);
      if (transform.skew.x == 0.0f) {
        transform.skew = glm::vec2(20.0f, 0.0f);
        transform.scale = glm::vec2(1.4f, 0.7f); // Squashed
        ENGINE_LOG_INFO("[TRANSFORM SQUASH & STRETCH] Applied Skew=(20°, 0°) "
                        "Scale=(1.4, 0.7)");
      } else {
        transform.skew = glm::vec2(0.0f, 0.0f);
        transform.scale = glm::vec2(1.0f, 1.0f); // Restored
        ENGINE_LOG_INFO("[TRANSFORM SQUASH & STRETCH] Restored Skew=(0°, 0°) "
                        "Scale=(1.0, 1.0)");
      }
    }
  }

  // P Key: Test Pivot Anchors (Center -> Feet -> Top-Left -> Center)
  if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_P)) {
    if (m_entity_manager.has_component<Engine::TransformComponent>(
            m_player_entity)) {
      auto &transform =
          m_entity_manager.get_component<Engine::TransformComponent>(
              m_player_entity);
      if (transform.pivot == glm::vec2(0.5f, 0.5f)) {
        transform.pivot = glm::vec2(0.5f, 0.0f); // Bottom / Feet
        ENGINE_LOG_INFO(
            "[TRANSFORM PIVOT] Pivot set to (0.5, 0.0) [Bottom / Feet]");
      } else if (transform.pivot == glm::vec2(0.5f, 0.0f)) {
        transform.pivot = glm::vec2(0.0f, 0.0f); // Top-Left
        ENGINE_LOG_INFO(
            "[TRANSFORM PIVOT] Pivot set to (0.0, 0.0) [Top-Left Corner]");
      } else {
        transform.pivot = glm::vec2(0.5f, 0.5f); // Reset to Center
        ENGINE_LOG_INFO("[TRANSFORM PIVOT] Pivot reset to (0.5, 0.5) [Center]");
      }
    }
  }

  // B Key: Spawn Composite Multi-Collider Entity (1 Body Box + 2 Wing Circle
  // Colliders)
  if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_B)) {
    glm::vec2 mouse_world = Engine::Input::get_mouse_world_position(m_camera);
    Engine::EntityID composite_boss = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(
        composite_boss, Engine::TagComponent{"CompositeBoss_" +
                                             std::to_string(composite_boss)});
    m_entity_manager.add_component<Engine::TransformComponent>(
        composite_boss, Engine::TransformComponent{
                            glm::vec3(mouse_world.x, mouse_world.y, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(
        composite_boss, Engine::SpriteComponent{m_tree_texture.get(),
                                                glm::vec2(120.0f, 80.0f)});

    // Composite Multi-Collider: Central Box + 2 Wing Circles
    m_entity_manager
        .add_component<Engine::ColliderComponent>(composite_boss,
                                                  Engine::ColliderComponent{})
        .add_box(glm::vec2(60.0f, 80.0f),
                 glm::vec2(0.0f, 0.0f)) // Central Solid Body
        .add_circle(25.0f, glm::vec2(-45.0f, 0.0f),
                    true /*Left Wing Trigger*/) // Left Wing Trigger
        .add_circle(25.0f, glm::vec2(45.0f, 0.0f),
                    true /*Right Wing Trigger*/); // Right Wing Trigger

    m_entity_manager.add_component<Engine::RigidBodyComponent>(
        composite_boss, Engine::RigidBodyComponent::Static()); // Solid static boss
    ENGINE_LOG_INFO("[COMPOSITE COLLIDER] Spawned Composite Boss #{} with 1 "
                    "Box + 2 Circle Trigger Wings at ({:.1f}, {:.1f})",
                    composite_boss, mouse_world.x, mouse_world.y);
  }

  // R Key: Full Scene Reset (Player position, transforms, velocity, camera, and
  // spawned entities)
  if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_R)) {
    m_player_pos = glm::vec3(0.0f, 0.0f, 0.0f);
    m_spin_timer = 0.0f;
    m_orb_angle = 0.0f;

    if (m_entity_manager.has_component<Engine::TransformComponent>(
            m_player_entity)) {
      auto &transform =
          m_entity_manager.get_component<Engine::TransformComponent>(
              m_player_entity);
      transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
      transform.rotation = 0.0f;
      transform.scale = glm::vec2(1.0f, 1.0f);
      transform.pivot = glm::vec2(0.5f, 0.5f);
      transform.skew = glm::vec2(0.0f, 0.0f);
      transform.visible = true;
    }

    if (m_entity_manager.has_component<Engine::RigidBodyComponent>(
            m_player_entity)) {
      auto &rb = m_entity_manager.get_component<Engine::RigidBodyComponent>(
          m_player_entity);
      rb.velocity = glm::vec2(0.0f, 0.0f);
      rb.acceleration = glm::vec2(0.0f, 0.0f);
    }

    // Purge all dynamically spawned trees, cloned orbs, and composite bosses
    m_entity_manager.destroy_matching<Engine::TagComponent>(
        [](const Engine::TagComponent &tag) {
          return tag.tag.starts_with("SpawnedTree_") ||
                 tag.tag.starts_with("PlayerPetOrb") ||
                 tag.tag.starts_with("CompositeBoss_");
        });

    // Re-create initial template orb child
    m_child_orb = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(
        m_child_orb, Engine::TagComponent{"PlayerPetOrb"});
    m_entity_manager.add_component<Engine::TransformComponent>(
        m_child_orb, Engine::TransformComponent{glm::vec3(55.0f, 0.0f, 5.0f),
                                                glm::vec2(0.5f, 0.5f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(
        m_child_orb, Engine::SpriteComponent{m_tree_texture.get(),
                                             glm::vec2(50.0f, 50.0f),
                                             {100, 220, 100, 255}});
    m_entity_manager.set_parent(m_child_orb, m_player_entity);

    m_camera.set_position(glm::vec2(0.0f, 0.0f));
    ENGINE_LOG_INFO("[SCENE RESET] Full Scene Reset triggered! Player "
                    "position, transforms, and entities restored to defaults.");
  }

  // Mouse Position LookAt targeting example
  if (Engine::Input::is_key_pressed(SDL_SCANCODE_T)) {
    glm::vec2 mouse_world = Engine::Input::get_mouse_world_position(m_camera);
    if (m_entity_manager.has_component<Engine::TransformComponent>(
            m_player_entity)) {
      auto &player_transform =
          m_entity_manager.get_component<Engine::TransformComponent>(
              m_player_entity);
      player_transform.look_at(mouse_world);
      glm::vec2 right_dir = player_transform.get_right();
      ENGINE_LOG_INFO("[TRANSFORM LOOK_AT] Facing mouse at ({:.1f}, {:.1f}) | "
                      "Angle: {:.1f}° | Facing Right Vector: ({:.2f}, {:.2f})",
                      mouse_world.x, mouse_world.y, player_transform.rotation,
                      right_dir.x, right_dir.y);
    }
  }

  // Right Click to Spawn New ECS Entity dynamically in 2.5D space
  if (Engine::Input::is_mouse_just_pressed(Engine::MouseButton::RIGHT)) {
    glm::vec2 mouse_world = Engine::Input::get_mouse_world_position(m_camera);

    Engine::EntityID new_tree = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(
        new_tree,
        Engine::TagComponent{"SpawnedTree_" + std::to_string(new_tree)});
    m_entity_manager.add_component<Engine::TransformComponent>(
        new_tree, Engine::TransformComponent{
                      glm::vec3(mouse_world.x, mouse_world.y, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(
        new_tree,
        Engine::SpriteComponent{m_tree_texture.get(), glm::vec2(60.0f, 75.0f)});

    m_event_dispatcher.publish(Engine::CustomLogEvent{
        "Dynamically spawned ECS Entity #" + std::to_string(new_tree) +
        " at World (" + std::to_string(static_cast<int>(mouse_world.x)) + ", " +
        std::to_string(static_cast<int>(mouse_world.y)) + ")"});
  }

  // C Key: Clone Orb Entity via Ergonomic clone_entity API
  if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_C)) {
    Engine::EntityID cloned_orb = m_entity_manager.clone_entity(m_child_orb);
    if (cloned_orb != 0) {
      auto &orb_trans =
          m_entity_manager.get_component<Engine::TransformComponent>(
              cloned_orb);
      orb_trans.position += glm::vec3(20.0f, -20.0f, 0.0f);
      ENGINE_LOG_INFO("[ECS API] Cloned entity #{} from template #{}",
                      cloned_orb, m_child_orb);
    }
  }

  // V Key: Toggle Visibility (Hiding parent automatically hides all
  // hierarchical children with 0 overhead!)
  if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_V)) {
    if (m_entity_manager.has_component<Engine::TransformComponent>(
            m_player_entity)) {
      auto &transform =
          m_entity_manager.get_component<Engine::TransformComponent>(
              m_player_entity);
      transform.visible = !transform.visible;
      ENGINE_LOG_INFO(
          "[ECS TOP-DOWN VISIBILITY] Player visibility toggled to: {}",
          transform.visible ? "VISIBLE" : "HIDDEN (All children auto-hidden)");
    }
  }

  // X Key: Bulk Purge Spawned Trees & Cloned Orbs via destroy_matching API
  if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_X)) {
    size_t count_before = m_entity_manager.get_living_entity_count();
    m_entity_manager.destroy_matching<Engine::TagComponent>(
        [](const Engine::TagComponent &tag) {
          return tag.tag.starts_with("SpawnedTree_") ||
                 tag.tag.starts_with("PlayerPetOrb");
        });
    size_t count_after = m_entity_manager.get_living_entity_count();
    ENGINE_LOG_INFO(
        "[ECS API] Purged {} spawned/cloned entities! Living entities: {}",
        count_before - count_after, count_after);
  }

  if (m_spin_timer > 0.0f) {
    m_spin_timer -= delta_time;
    m_current_anim = &m_anim_spin;
  } else if (is_moving) {
    m_current_anim = &m_anim_run;
  } else {
    m_current_anim = &m_anim_idle;
  }

  if (m_current_anim) {
    m_current_anim->update(delta_time);
  }

  // Continuously update all child orb entities' local positions to orbit around
  // the player
  m_orb_angle += 3.5f * delta_time;
  float orb_radius = 60.0f;
  int orb_index = 0;

  m_entity_manager.for_each<Engine::TagComponent, Engine::TransformComponent>(
      [this, orb_radius, &orb_index](Engine::EntityID id,
                                     const Engine::TagComponent &tag,
                                     Engine::TransformComponent &transform) {
        if (tag.tag.starts_with("PlayerPetOrb")) {
          float angle_offset =
              orb_index * 0.628f; // Stagger orbit angle per cloned orb
          transform.position.x =
              std::cos(m_orb_angle + angle_offset) * orb_radius;
          transform.position.y =
              std::sin(m_orb_angle + angle_offset) * orb_radius;
          orb_index++;
        }
      });

  // Update player SpriteComponent texture & frame rect from Animation2D
  if (m_entity_manager.has_component<Engine::SpriteComponent>(
          m_player_entity) &&
      m_current_anim) {
    auto &sprite = m_entity_manager.get_component<Engine::SpriteComponent>(
        m_player_entity);
    sprite.texture = m_current_anim->get_texture();
    sprite.src_rect = m_current_anim->get_current_frame_rect();
    sprite.use_src_rect = true;
    sprite.size = glm::vec2(64.0f, 64.0f);
  }

  // Execute 2D Physics Step (Gravity, Spatial Hash Broadphase, Collision
  // Resolution & Event Dispatching)
  Engine::PhysicsSystem::update(m_entity_manager, m_event_dispatcher,
                                delta_time);

  // Sync m_player_pos back from Physics/Transform System
  if (m_entity_manager.has_component<Engine::TransformComponent>(
          m_player_entity)) {
    const auto &trans =
        m_entity_manager.get_component<Engine::TransformComponent>(
            m_player_entity);
    m_player_pos = trans.position;
  }

  // Debug Log Player Movement
  static int s_log_counter = 0;
  if (is_moving && (++s_log_counter % 20 == 0)) {
    ENGINE_LOG_INFO("[TOP-DOWN MOVEMENT LOG] Moving Velocity=({:.1f}, {:.1f}) | Position=({:.1f}, {:.1f})",
                    target_velocity.x, target_velocity.y, m_player_pos.x, m_player_pos.y);
  }

  // Evaluate parent-child hierarchy matrix/vector transforms across all ECS
  // entities
  Engine::TransformSystem::update(m_entity_manager);

  // Camera smoothly follows player
  glm::vec2 current_cam = m_camera.get_position();
  glm::vec2 target_cam = glm::vec2(m_player_pos.x, m_player_pos.y);
  m_camera.set_position(current_cam +
                        (target_cam - current_cam) * 5.0f * delta_time);
}

void AnimatedSpriteDemoScene::on_render(Engine::Renderer2D &renderer) {
  renderer.begin_scene(m_camera);

  // Background Grid
  for (int x = -4; x <= 4; ++x) {
    for (int y = -3; y <= 3; ++y) {
      renderer.submit_sprite(m_bg_texture.get(),
                             glm::vec3(x * 160.0f, y * 160.0f, -40.0f),
                             glm::vec2(150.0f, 150.0f));
    }
  }

  // Automated 100% ECS Rendering System (Renders Player, Orbs, Trees, and
  // Obstacles seamlessly in 1 pass!)
  Engine::RenderSystem::render(m_entity_manager, renderer);

  // Visual Debug Collider Wireframe Overlays (Press G key to toggle)
  if (m_show_debug_colliders) {
    Engine::RenderSystem::render_debug_colliders(m_entity_manager, renderer,
                                                 m_tree_texture.get());
  }

  renderer.end_scene();
}

} // namespace Sandbox
