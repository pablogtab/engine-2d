#pragma once

#include "../events/EventDispatcher.hpp"
#include "../events/Event.hpp"
#include "../scene/Camera2D.hpp"
#include "../scene/Scene.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../renderer/Texture.hpp"
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

private:
    SDL_Renderer* m_sdl_renderer{nullptr};
    Engine::Camera2D m_camera;
    Engine::EventDispatcher m_event_dispatcher;

    Engine::EntityID m_player_id{0};
    Engine::EntityID m_elevator_id{0};
    float m_elevator_time{0.0f};
    bool m_show_debug_colliders{true};
    bool m_is_grounded{false};
    float m_coyote_timer{0.0f};
    float m_jump_buffer_timer{0.0f};
    float m_launch_cooldown{0.0f};
    int m_score{0};
    int m_total_coins{0};

    // Procedural textures
    std::unique_ptr<Engine::Texture> m_player_texture;
    std::unique_ptr<Engine::Texture> m_block_texture;
    std::unique_ptr<Engine::Texture> m_coin_texture;
    std::unique_ptr<Engine::Texture> m_launch_pad_texture;
    std::unique_ptr<Engine::Texture> m_elevator_texture;
    std::unique_ptr<Engine::Texture> m_slope_texture;
    std::unique_ptr<Engine::Texture> m_wheel_texture;
    std::unique_ptr<Engine::Texture> m_circle_debug_texture;

    void setup_level();
    void create_platform(const glm::vec2& position, const glm::vec2& size, SDL_Color color);
    void create_slope(const glm::vec2& position, const glm::vec2& size, bool slope_up_right = true);
    void create_elevator(const glm::vec2& position, const glm::vec2& size);
    void create_coin(const glm::vec2& position);
    void create_launch_pad(const glm::vec2& position);
};

} // namespace Sandbox
