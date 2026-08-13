#pragma once

#include "../scene/Scene.hpp"
#include "../scene/Camera2D.hpp"
#include "../renderer/Texture.hpp"
#include "../renderer/SpriteSheet.hpp"
#include "../renderer/Animation2D.hpp"
#include <memory>

namespace Sandbox {

class AnimatedSpriteDemoScene : public Engine::Scene {
public:
    explicit AnimatedSpriteDemoScene(SDL_Renderer* renderer);
    ~AnimatedSpriteDemoScene() override = default;

    void on_attach() override;
    void on_detach() override {}
    void on_update(float delta_time) override;
    void on_render(Engine::Renderer2D& renderer) override;
    void on_resize(int width, int height) override;

private:
    Engine::Texture* generate_procedural_spritesheet();

    SDL_Renderer* m_sdl_renderer{nullptr};
    Engine::Camera2D m_camera;

    std::shared_ptr<Engine::Texture> m_spritesheet_texture;
    std::shared_ptr<Engine::Texture> m_bg_texture;
    std::shared_ptr<Engine::Texture> m_tree_texture;
    std::unique_ptr<Engine::SpriteSheet> m_spritesheet;

    Engine::Animation2D m_anim_idle;
    Engine::Animation2D m_anim_run;
    Engine::Animation2D m_anim_spin;
    Engine::Animation2D* m_current_anim{nullptr};

    glm::vec3 m_player_pos{0.0f, 0.0f, 0.0f};
    bool m_facing_right{true};
    float m_spin_timer{0.0f};
    Engine::EntityID m_player_entity{0};
    Engine::EntityID m_child_orb{0};
    float m_orb_angle{0.0f};
};

} // namespace Sandbox
