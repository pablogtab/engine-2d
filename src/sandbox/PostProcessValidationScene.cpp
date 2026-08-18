#include "PostProcessValidationScene.hpp"

#include "../core/Input.hpp"
#include "../core/Log.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../renderer/postprocess/PostProcessPasses.hpp"
#include <SDL3/SDL.h>

namespace Sandbox {

namespace {

Engine::PostProcessProfile profile_for_mode(int mode) {
    switch (mode) {
        case 2: return Engine::PostProcessProfile::PresetRetroCRT();
        case 3: return Engine::PostProcessProfile::PresetUnderwater();
        case 4: return Engine::PostProcessProfile::PresetCinematic();
        case 5: return Engine::PostProcessProfile::PresetNightVision();
        case 6: return Engine::PostProcessProfile::PresetDreamyBloom();
        default: return Engine::PostProcessProfile::PresetDefault();
    }
}

} // namespace

PostProcessValidationScene::PostProcessValidationScene(SDL_Renderer* renderer)
    : Engine::Scene("Post-Processing Validation"),
      m_sdl_renderer(renderer),
      m_camera(1280.0f, 720.0f) {}

void PostProcessValidationScene::on_attach() {
    m_white_texture.reset(Engine::Texture::create_solid(m_sdl_renderer, 4, 4, 255, 255, 255, 255));
    m_grid_texture.reset(Engine::Texture::create_grid(m_sdl_renderer, 32, 105, 115, 135));
    m_font = Engine::Font::create_default(m_sdl_renderer, 24.0f);

    m_pipeline.init(
        m_sdl_renderer,
        m_view_configuration.output_width,
        m_view_configuration.output_height,
        m_view_configuration.reference_width,
        m_view_configuration.reference_height
    );
    m_pipeline.clear_passes();
    m_pipeline.add_pass(std::make_shared<Engine::VignettePass>());
    m_pipeline.add_pass(std::make_shared<Engine::ChromaticAberrationPass>());
    m_pipeline.add_pass(std::make_shared<Engine::BloomPass>());
    m_pipeline.add_pass(std::make_shared<Engine::GaussianBlurPass>());
    m_pipeline.add_pass(std::make_shared<Engine::ColorGradingPass>());
    m_pipeline.add_pass(std::make_shared<Engine::FilmGrainPass>());
    m_pipeline.add_pass(std::make_shared<Engine::ScanlinesCRTPass>());

    select_mode(5);
    ENGINE_LOG_INFO("[POST-PROCESS VALIDATION] Isolated scene active. Keys: 0 bypass, 1 Default, 2 CRT, 3 Underwater, 4 Cinematic, 5 NightVision, 6 DreamyBloom.");
}

void PostProcessValidationScene::on_detach() {
    m_pipeline.destroy_targets();
    m_white_texture.reset();
    m_grid_texture.reset();
    m_font.reset();
}

void PostProcessValidationScene::on_viewport_resize(
    Engine::RenderViewKind,
    const Engine::RenderViewConfiguration& configuration
) {
    m_view_configuration = configuration;
    m_camera.set_viewport_size(
        static_cast<float>(configuration.output_width),
        static_cast<float>(configuration.output_height)
    );
    m_camera.set_reference_size(
        static_cast<float>(configuration.reference_width),
        static_cast<float>(configuration.reference_height)
    );
    m_pipeline.resize(configuration.output_width, configuration.output_height);
}

void PostProcessValidationScene::select_mode(int mode) {
    m_mode = mode;
    m_post_process_enabled = mode != 0;
    if (m_post_process_enabled) {
        m_profile = profile_for_mode(mode);
        m_profile.apply_to_pipeline(m_pipeline);
        m_pipeline.request_frame_probe(m_profile.name);
        ENGINE_LOG_INFO(
            "[POST-PROCESS VALIDATION] Effective profile: '{}' | passes: {} | tint: ({}, {}, {}, {}) | vignette: {:.2f} | bloom: {:.2f} | CRT: {:.2f}",
            m_profile.name,
            m_pipeline.get_active_pass_count(),
            static_cast<int>(m_profile.color_tint.r),
            static_cast<int>(m_profile.color_tint.g),
            static_cast<int>(m_profile.color_tint.b),
            static_cast<int>(m_profile.color_tint.a),
            m_profile.vignette_intensity,
            m_profile.bloom_intensity,
            m_profile.crt_scanline_intensity
        );
    } else {
        ENGINE_LOG_INFO("[POST-PROCESS VALIDATION] Pipeline bypassed; expected image is neutral gray.");
    }
    m_log_next_render = true;
}

void PostProcessValidationScene::on_update(float delta_time) {
    int requested_mode = -1;
    if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_0)) requested_mode = 0;
    else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_1)) requested_mode = 1;
    else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_2)) requested_mode = 2;
    else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_3)) requested_mode = 3;
    else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_4)) requested_mode = 4;
    else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_5)) requested_mode = 5;
    else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_6)) requested_mode = 6;

    if (requested_mode >= 0 && requested_mode != m_mode) select_mode(requested_mode);
    m_pipeline.update(delta_time);
}

void PostProcessValidationScene::on_render(Engine::Renderer2D& renderer) {
    if (m_log_next_render) {
        m_log_next_render = false;
        ENGINE_LOG_INFO(
            "[POST-PROCESS VALIDATION] Rendering {}x{} to {} | pipeline: {} | active passes: {}",
            m_pipeline.get_width(),
            m_pipeline.get_height(),
            renderer.get_custom_render_target() ? "Viewport texture" : "window framebuffer",
            m_post_process_enabled ? "enabled" : "bypassed",
            m_post_process_enabled ? m_pipeline.get_active_pass_count() : 0
        );
    }
    renderer.set_post_process_pipeline(m_post_process_enabled ? &m_pipeline : nullptr);
    renderer.begin_scene(m_camera);

    // A neutral full-camera plate makes full-frame grading immediately visible.
    renderer.submit_sprite(
        m_white_texture.get(),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec2(1500.0f, 900.0f),
        0.0f,
        SDL_Color{95, 105, 125, 255}
    );

    renderer.submit_sprite(
        m_grid_texture.get(),
        glm::vec3(0.0f, 0.0f, -0.5f),
        glm::vec2(1080.0f, 520.0f),
        0.0f,
        SDL_Color{150, 160, 180, 255}
    );

    constexpr float swatch_y = 0.0f;
    constexpr glm::vec2 swatch_size{160.0f, 240.0f};
    renderer.submit_sprite(m_white_texture.get(), glm::vec3(-440.0f, swatch_y, 0.0f), swatch_size, 0.0f, SDL_Color{255, 70, 70, 255});
    renderer.submit_sprite(m_white_texture.get(), glm::vec3(-220.0f, swatch_y, 0.0f), swatch_size, 0.0f, SDL_Color{70, 255, 90, 255});
    // The probe samples the center of this white swatch. Its logged output must
    // match the visible color if the processed texture reaches presentation.
    renderer.submit_sprite(m_white_texture.get(), glm::vec3(0.0f, swatch_y, 0.0f), swatch_size, 0.0f, SDL_Color{255, 255, 255, 255});
    renderer.submit_sprite(m_white_texture.get(), glm::vec3(220.0f, swatch_y, 0.0f), swatch_size, 0.0f, SDL_Color{70, 120, 255, 255});
    renderer.submit_sprite(m_white_texture.get(), glm::vec3(440.0f, swatch_y, 0.0f), swatch_size, 0.0f, SDL_Color{255, 220, 60, 255});

    if (m_font) {
        const std::string mode_name = m_post_process_enabled ? m_profile.name : "BYPASS";
        renderer.submit_screen_text(
            *m_font,
            "POST-PROCESS VALIDATION | EFFECTIVE: " + mode_name,
            glm::vec2(28.0f, 26.0f),
            0.70f,
            SDL_Color{255, 255, 255, 255}
        );
        renderer.submit_screen_text(
            *m_font,
            "0 Bypass | 1 Default | 2 CRT | 3 Underwater | 4 Cinematic | 5 NightVision | 6 Dreamy",
            glm::vec2(28.0f, 58.0f),
            0.52f,
            SDL_Color{220, 225, 235, 255}
        );
        renderer.submit_screen_text(
            *m_font,
            "NightVision must turn the neutral plate and white swatch visibly green.",
            glm::vec2(28.0f, static_cast<float>(m_view_configuration.output_height) - 42.0f),
            0.58f,
            SDL_Color{255, 255, 255, 255}
        );
    }

    renderer.end_scene();
}

} // namespace Sandbox
