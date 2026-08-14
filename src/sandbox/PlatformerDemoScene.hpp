#pragma once

#include "../events/EventDispatcher.hpp"
#include "../events/Event.hpp"
#include "../scene/Camera2D.hpp"
#include "../scene/Scene.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../renderer/Texture.hpp"
#include "../renderer/postprocess/PostProcessPipeline.hpp"
#include "../renderer/postprocess/PostProcessPasses.hpp"
#include "../renderer/postprocess/PostProcessProfile.hpp"
#include "../renderer/postprocess/PostProcessVolume.hpp"
#include "../renderer/postprocess/PostProcessTweens.hpp"
#include "../particles/ParticleSystem2D.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../renderer/SpriteSheet.hpp"
#include "../renderer/Animation2D.hpp"
#include <SDL3/SDL.h>
#include <memory>

namespace Sandbox {

class PlatformerDemoScene : public Engine::Scene {
public:
    explicit PlatformerDemoScene(SDL_Renderer* renderer);
    ~PlatformerDemoScene() override = default;

    void on_attach() override;
    void on_detach() override;
    void on_update(float delta_time) override;
    void on_render(Engine::Renderer2D& renderer) override;
    void on_resize(int width, int height) override;

private:
    SDL_Renderer* m_sdl_renderer{nullptr};
    Engine::Camera2D m_camera;
    Engine::EventDispatcher m_event_dispatcher;
    Engine::ParticleSystem2D m_particle_system{3000};
    Engine::PostProcessPipeline m_post_process_pipeline;
    Engine::PostProcessVolumeManager m_volume_manager;
    Engine::PostProcessProfile m_base_profile;
    Engine::PostProcessProfile m_current_profile;

    Engine::EntityID m_player_id{0};
    Engine::EntityID m_elevator_id{0};
    float m_elevator_time{0.0f};
    bool m_show_debug_colliders{false};
    bool m_is_grounded{false};
    float m_coyote_timer{0.0f};
    float m_jump_buffer_timer{0.0f};
    float m_launch_cooldown{0.0f};
    float m_drop_through_timer{0.0f};
    bool m_enable_laser{false};
    Engine::RaycastHit2D m_last_raycast_hit;
    glm::vec2 m_mouse_world_pos{0.0f, 0.0f};

    struct ShockwaveEffect {
        glm::vec2 center{0.0f, 0.0f};
        float current_radius{0.0f};
        float max_radius{240.0f};
        float lifetime{0.35f};
        float timer{0.0f};
    };
    std::vector<ShockwaveEffect> m_shockwaves;

    int m_score{0};
    int m_total_coins{0};

    // Procedural textures
    std::unique_ptr<Engine::Texture> m_player_texture;
    std::unique_ptr<Engine::Texture> m_block_texture;
    std::unique_ptr<Engine::Texture> m_wood_platform_texture;
    std::unique_ptr<Engine::Texture> m_stone_platform_texture;
    std::unique_ptr<Engine::Texture> m_sky_bg_texture;
    std::unique_ptr<Engine::Texture> m_coin_texture;
    std::unique_ptr<Engine::Texture> m_launch_pad_texture;
    std::unique_ptr<Engine::Texture> m_elevator_texture;
    std::unique_ptr<Engine::Texture> m_slope_texture;
    std::unique_ptr<Engine::Texture> m_wheel_texture;
    std::unique_ptr<Engine::Texture> m_wrecking_ball_texture;
    std::unique_ptr<Engine::Texture> m_circle_debug_texture;
    std::unique_ptr<Engine::Texture> m_campfire_logs_texture;
    std::unique_ptr<Engine::Texture> m_campfire_light_texture;
    std::unique_ptr<Engine::Texture> m_flame_core_texture;
    std::unique_ptr<Engine::Texture> m_knight_spritesheet_texture;
    std::unique_ptr<Engine::SpriteSheet> m_knight_spritesheet;

    Engine::Animation2D m_anim_idle;
    Engine::Animation2D m_anim_run;
    Engine::Animation2D m_anim_jump;
    Engine::Animation2D m_anim_fall;
    Engine::Animation2D* m_current_player_anim{nullptr};
    bool m_player_facing_right{true};

    glm::vec2 m_campfire_pos{-220.0f, -252.0f};
    float m_campfire_time{0.0f};
    float m_campfire_scale{0.65f};
    float m_target_zoom{0.76f};

    bool m_is_grappling{false};
    Engine::EntityID m_grapple_anchor_id{0};

    void setup_level();
    void create_platform(const glm::vec2& position, const glm::vec2& size, SDL_Color color);
    void create_one_way_platform(const glm::vec2& position, const glm::vec2& size, SDL_Color color);
    void create_slope(const glm::vec2& position, const glm::vec2& size, bool slope_up_right = true);
    void create_wrecking_ball(const glm::vec2& anchor_pos, const glm::vec2& ball_pos, float radius = 28.0f);
    void create_elevator(const glm::vec2& position, const glm::vec2& size);
    void create_coin(const glm::vec2& position);
    void create_launch_pad(const glm::vec2& position);

    void attach_grapple(const glm::vec2& target_point);
    void detach_grapple();
};

} // namespace Sandbox
