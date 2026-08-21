#include "renderer/Renderer2D.hpp"
#include "renderer/Texture.hpp"
#include "renderer/material/FragmentShader.hpp"
#include "renderer/material/Material.hpp"
#include "renderer/material/SDLGPUMaterialBackend.hpp"
#include "scene/Camera2D.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

constexpr int SKIP_RETURN_CODE = 77;

struct Pixel {
    Uint8 red{0};
    Uint8 green{0};
    Uint8 blue{0};
    Uint8 alpha{0};
};

bool add_variant_if_present(
    Engine::FragmentShader& shader,
    Engine::ShaderBinaryFormat format,
    const std::filesystem::path& path,
    const char* entry_point
) {
    if (!std::filesystem::is_regular_file(path)) return false;
    return shader.add_variant_from_file(format, path.string(), entry_point);
}

Pixel read_center(SDL_Renderer* renderer, SDL_Texture* target, int width, int height) {
    Pixel result;
    if (!SDL_SetRenderTarget(renderer, target)) return result;
    const SDL_Rect sample_rect{width / 2, height / 2, 1, 1};
    SDL_Surface* sample = SDL_RenderReadPixels(renderer, &sample_rect);
    if (!sample) return result;
    SDL_ReadSurfacePixel(
        sample,
        0,
        0,
        &result.red,
        &result.green,
        &result.blue,
        &result.alpha
    );
    SDL_DestroySurface(sample);
    return result;
}

bool matches_expected_grade(const Pixel& pixel) {
    return pixel.red >= 56 && pixel.red <= 72 &&
           pixel.green >= 247 && pixel.green <= 255 &&
           pixel.blue >= 120 && pixel.blue <= 136 &&
           pixel.alpha >= 247;
}

} // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cout << "GPURenderSmokeTests skipped: SDL video initialization failed: "
                  << SDL_GetError() << '\n';
        return SKIP_RETURN_CODE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Engine2D GPU smoke",
        64,
        64,
        SDL_WINDOW_HIDDEN
    );
    if (!window) {
        std::cout << "GPURenderSmokeTests skipped: hidden window unavailable: "
                  << SDL_GetError() << '\n';
        SDL_Quit();
        return SKIP_RETURN_CODE;
    }

    SDL_Renderer* sdl_renderer = SDL_CreateGPURenderer(nullptr, window);
    if (!sdl_renderer) {
        std::cout << "GPURenderSmokeTests skipped: SDL GPU renderer unavailable: "
                  << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return SKIP_RETURN_CODE;
    }

    int exit_code = 1;
    {
        auto shader = std::make_shared<Engine::FragmentShader>("GPU Smoke NightVision");
        shader->set_uniform_buffer_count(1);
        const std::filesystem::path source_dir{ENGINE_SOURCE_DIR};
        const std::filesystem::path output_dir{ENGINE_SHADER_OUTPUT_DIR};
        bool has_variant = add_variant_if_present(
            *shader,
            Engine::ShaderBinaryFormat::Msl,
            source_dir / "assets/shaders/night_vision.frag.msl",
            "main0"
        );
        has_variant = add_variant_if_present(
            *shader,
            Engine::ShaderBinaryFormat::SpirV,
            output_dir / "night_vision.frag.spv",
            "main"
        ) || has_variant;
        has_variant = add_variant_if_present(
            *shader,
            Engine::ShaderBinaryFormat::Dxil,
            output_dir / "night_vision.frag.dxil",
            "main"
        ) || has_variant;

        std::string support_error;
        if (!has_variant || !Engine::SDLGPUMaterialBackend::supports(
                sdl_renderer,
                *shader,
                &support_error
            )) {
            std::cout << "GPURenderSmokeTests skipped: "
                      << (support_error.empty() ? "no native shader variant is available" : support_error)
                      << '\n';
            exit_code = SKIP_RETURN_CODE;
        } else {
            auto material = std::make_shared<Engine::Material>("GPU Smoke Grade", shader);
            struct alignas(16) NightVisionUniforms {
                float tint[4];
                float shadow_tint[4];
            } uniforms{
                {0.25f, 1.0f, 0.5f, 1.0f},
                {0.0f, 0.0f, 0.0f, 0.0f}
            };
            material->set_uniform(0, uniforms);
            auto backend = std::make_shared<Engine::SDLGPUMaterialBackend>(sdl_renderer);
            if (!backend->is_available()) {
                std::cout << "GPURenderSmokeTests skipped: material backend unavailable\n";
                exit_code = SKIP_RETURN_CODE;
            } else {
                constexpr int width = 64;
                constexpr int height = 64;
                SDL_Texture* sprite_target = SDL_CreateTexture(
                    sdl_renderer,
                    SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,
                    width,
                    height
                );
                SDL_Texture* fullscreen_source = SDL_CreateTexture(
                    sdl_renderer,
                    SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,
                    width,
                    height
                );
                SDL_Texture* fullscreen_target = SDL_CreateTexture(
                    sdl_renderer,
                    SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,
                    width,
                    height
                );
                if (!sprite_target || !fullscreen_source || !fullscreen_target) {
                    std::cerr << "GPURenderSmokeTests failed: render target creation failed: "
                              << SDL_GetError() << '\n';
                } else {
                    std::unique_ptr<Engine::Texture> white(
                        Engine::Texture::create_solid(
                            sdl_renderer,
                            4,
                            4,
                            255,
                            255,
                            255,
                            255
                        )
                    );
                    if (!white) {
                        std::cerr << "GPURenderSmokeTests failed: source texture creation failed: "
                                  << SDL_GetError() << '\n';
                    } else {
                        Engine::Renderer2D renderer(sdl_renderer);
                        renderer.set_material_backend(backend);
                        renderer.set_custom_render_target(sprite_target);
                        SDL_SetRenderTarget(sdl_renderer, sprite_target);
                        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                        SDL_RenderClear(sdl_renderer);
                        Engine::Camera2D camera(width, height);
                        renderer.begin_scene(camera);
                        renderer.submit_sprite(
                            white.get(),
                            {0.0f, 0.0f, 0.0f},
                            {48.0f, 48.0f},
                            0.0f,
                            {255, 255, 255, 255},
                            nullptr,
                            {0.5f, 0.5f},
                            false,
                            false,
                            false,
                            0,
                            0,
                            material
                        );
                        renderer.end_scene();
                        const Pixel sprite_pixel = read_center(
                            sdl_renderer,
                            sprite_target,
                            width,
                            height
                        );

                        SDL_SetRenderTarget(sdl_renderer, fullscreen_source);
                        SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, 255);
                        SDL_RenderClear(sdl_renderer);
                        SDL_SetRenderTarget(sdl_renderer, fullscreen_target);
                        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                        SDL_RenderClear(sdl_renderer);
                        const bool fullscreen_drawn = backend->draw_fullscreen(
                            *material,
                            fullscreen_source,
                            {width, height}
                        );
                        const Pixel fullscreen_pixel = read_center(
                            sdl_renderer,
                            fullscreen_target,
                            width,
                            height
                        );

                        if (renderer.get_frame_metrics().stage(
                                Engine::RenderStage::SceneGeometry
                            ).material_switches != 1 ||
                            !fullscreen_drawn ||
                            !matches_expected_grade(sprite_pixel) ||
                            !matches_expected_grade(fullscreen_pixel)) {
                            std::cerr
                                << "GPURenderSmokeTests failed on " << backend->backend_name()
                                << ": sprite RGBA=(" << static_cast<int>(sprite_pixel.red) << ','
                                << static_cast<int>(sprite_pixel.green) << ','
                                << static_cast<int>(sprite_pixel.blue) << ','
                                << static_cast<int>(sprite_pixel.alpha) << ") fullscreen RGBA=("
                                << static_cast<int>(fullscreen_pixel.red) << ','
                                << static_cast<int>(fullscreen_pixel.green) << ','
                                << static_cast<int>(fullscreen_pixel.blue) << ','
                                << static_cast<int>(fullscreen_pixel.alpha) << ")\n";
                        } else {
                            std::cout << "GPURenderSmokeTests passed on " << backend->backend_name()
                                      << ": sprite and fullscreen probes matched\n";
                            exit_code = 0;
                        }
                    }
                }
                if (sprite_target) SDL_DestroyTexture(sprite_target);
                if (fullscreen_source) SDL_DestroyTexture(fullscreen_source);
                if (fullscreen_target) SDL_DestroyTexture(fullscreen_target);
            }
        }
    }

    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
