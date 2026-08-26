#include "core/AssetDatabase.hpp"
#include "core/AssetManager.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/UIComponents.hpp"
#include "renderer/BakedLighting2DSystem.hpp"
#include "renderer/Renderer2D.hpp"
#include "renderer/Lighting2DSystem.hpp"
#include "renderer/Texture.hpp"
#include "renderer/material/FragmentShader.hpp"
#include "renderer/material/Material.hpp"
#include "renderer/material/SDLGPUMaterialBackend.hpp"
#include "scene/Camera2D.hpp"
#include "ui/CanvasUISystem.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>
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

Pixel read_pixel(SDL_Renderer* renderer, SDL_Texture* target, int x, int y) {
    Pixel result;
    if (!SDL_SetRenderTarget(renderer, target)) return result;
    const SDL_Rect sample_rect{x, y, 1, 1};
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

Pixel read_center(SDL_Renderer* renderer, SDL_Texture* target, int width, int height) {
    return read_pixel(renderer, target, width / 2, height / 2);
}

bool matches_expected_grade(const Pixel& pixel) {
    return pixel.red >= 56 && pixel.red <= 72 &&
           pixel.green >= 247 && pixel.green <= 255 &&
           pixel.blue >= 120 && pixel.blue <= 136 &&
           pixel.alpha >= 247;
}

bool contains_expected_grade(SDL_Renderer* renderer, SDL_Texture* target) {
    if (!SDL_SetRenderTarget(renderer, target)) return false;
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (!surface) return false;
    bool found = false;
    for (int y = 0; y < surface->h && !found; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            Pixel pixel;
            SDL_ReadSurfacePixel(
                surface,
                x,
                y,
                &pixel.red,
                &pixel.green,
                &pixel.blue,
                &pixel.alpha
            );
            if (matches_expected_grade(pixel)) {
                found = true;
                break;
            }
        }
    }
    SDL_DestroySurface(surface);
    return found;
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
            source_dir / "tests/fixtures/shaders/night_vision.frag.msl",
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
                SDL_Texture* lighting_target = SDL_CreateTexture(
                    sdl_renderer,
                    SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,
                    width,
                    height
                );
                SDL_Texture* baked_target = SDL_CreateTexture(
                    sdl_renderer,
                    SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,
                    width,
                    height
                );
                SDL_Texture* canvas_target = SDL_CreateTexture(
                    sdl_renderer,
                    SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,
                    width,
                    height
                );
                SDL_Texture* canvas_text_target = SDL_CreateTexture(
                    sdl_renderer,
                    SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,
                    width,
                    height
                );
                SDL_Texture* lit_sprite_target = SDL_CreateTexture(
                    sdl_renderer,
                    SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,
                    width,
                    height
                );
                if (!sprite_target || !fullscreen_source || !fullscreen_target ||
                    !lighting_target || !baked_target || !canvas_target ||
                    !canvas_text_target || !lit_sprite_target) {
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

                        std::unique_ptr<Engine::Texture> lit_albedo(
                            Engine::Texture::create_solid(sdl_renderer, 4, 4, 32, 64, 96, 255)
                        );
                        std::unique_ptr<Engine::Texture> lit_emissive(
                            Engine::Texture::create_solid(sdl_renderer, 4, 4, 16, 0, 0, 255)
                        );
                        Engine::SpriteMaterialLighting material_lighting;
                        material_lighting.emissive_map = lit_emissive.get();
                        material_lighting.emissive_intensity = 1.0f;
                        material_lighting.ambient = 1.0f;
                        renderer.set_custom_render_target(lit_sprite_target);
                        const auto render_lit_probe = [&](
                            Engine::Texture* albedo,
                            const Engine::SpriteMaterialLighting& lighting_input
                        ) {
                            SDL_SetRenderTarget(sdl_renderer, lit_sprite_target);
                            SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                            SDL_RenderClear(sdl_renderer);
                            renderer.begin_scene(camera);
                            renderer.submit_sprite(
                                albedo, {0.0f, 0.0f, 0.0f}, {48.0f, 48.0f}, 0.0f,
                                {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
                                false, false, 0, 0, nullptr, Engine::DEFAULT_RENDER_LAYER,
                                {}, 0, SDL_BLENDMODE_BLEND, false, &lighting_input
                            );
                            renderer.end_scene();
                            return read_center(sdl_renderer, lit_sprite_target, width, height);
                        };
                        const Pixel lit_sprite_pixel = render_lit_probe(lit_albedo.get(), material_lighting);

                        std::unique_ptr<Engine::Texture> side_normal(
                            Engine::Texture::create_solid(sdl_renderer, 4, 4, 255, 128, 128, 255)
                        );
                        Engine::SpriteMaterialLighting normal_lighting;
                        normal_lighting.normal_map = side_normal.get();
                        normal_lighting.light_direction = {0.0f, 0.0f, 1.0f};
                        normal_lighting.light_intensity = 1.0f;
                        normal_lighting.ambient = 0.0f;
                        const Pixel normal_sprite_pixel = render_lit_probe(lit_albedo.get(), normal_lighting);

                        std::unique_ptr<Engine::Texture> black_albedo(
                            Engine::Texture::create_solid(sdl_renderer, 4, 4, 0, 0, 0, 255)
                        );
                        std::unique_ptr<Engine::Texture> flat_normal(
                            Engine::Texture::create_solid(sdl_renderer, 4, 4, 128, 128, 255, 255)
                        );
                        std::unique_ptr<Engine::Texture> white_specular(
                            Engine::Texture::create_solid(sdl_renderer, 4, 4, 255, 255, 255, 255)
                        );
                        Engine::SpriteMaterialLighting specular_lighting;
                        specular_lighting.normal_map = flat_normal.get();
                        specular_lighting.specular_map = white_specular.get();
                        specular_lighting.light_direction = {0.0f, 0.0f, 1.0f};
                        specular_lighting.light_intensity = 1.0f;
                        specular_lighting.ambient = 0.0f;
                        specular_lighting.specular_strength = 0.5f;
                        specular_lighting.shininess = 8.0f;
                        const Pixel specular_sprite_pixel = render_lit_probe(
                            black_albedo.get(), specular_lighting
                        );
                        const bool lit_sprite_matches =
                            lit_sprite_pixel.red >= 44 && lit_sprite_pixel.red <= 52 &&
                            lit_sprite_pixel.green >= 60 && lit_sprite_pixel.green <= 68 &&
                            lit_sprite_pixel.blue >= 92 && lit_sprite_pixel.blue <= 100 &&
                            lit_sprite_pixel.alpha >= 247 &&
                            normal_sprite_pixel.red <= 2 && normal_sprite_pixel.green <= 2 &&
                            normal_sprite_pixel.blue <= 2 && normal_sprite_pixel.alpha >= 247 &&
                            specular_sprite_pixel.red >= 120 && specular_sprite_pixel.red <= 136 &&
                            specular_sprite_pixel.green >= 120 && specular_sprite_pixel.green <= 136 &&
                            specular_sprite_pixel.blue >= 120 && specular_sprite_pixel.blue <= 136 &&
                            specular_sprite_pixel.alpha >= 247 &&
                            renderer.material_pack_cache().metrics().resident_entries == 3;

                        Engine::EntityManager canvas_entities;
                        const Engine::EntityID canvas_entity = canvas_entities.create_entity();
                        Engine::CanvasComponent canvas;
                        canvas.reference_size = {width, height};
                        canvas.scaling_policy = Engine::PresentationMode::Stretch;
                        canvas.use_safe_area = false;
                        canvas_entities.add_component<Engine::CanvasComponent>(canvas_entity, canvas);
                        canvas_entities.add_component<Engine::HierarchyComponent>(
                            canvas_entity, Engine::HierarchyComponent{}
                        );
                        const Engine::EntityID image_entity = canvas_entities.create_entity();
                        canvas_entities.add_component<Engine::HierarchyComponent>(
                            image_entity, Engine::HierarchyComponent{canvas_entity}
                        );
                        canvas_entities.get_component<Engine::HierarchyComponent>(canvas_entity)
                            .children.push_back(image_entity);
                        Engine::UILayoutComponent image_layout;
                        image_layout.anchor_min = {0.0f, 0.0f};
                        image_layout.anchor_max = {1.0f, 1.0f};
                        image_layout.size_delta = {0.0f, 0.0f};
                        canvas_entities.add_component<Engine::UILayoutComponent>(image_entity, image_layout);
                        Engine::UIImageComponent image;
                        image.material = material;
                        canvas_entities.add_component<Engine::UIImageComponent>(image_entity, image);
                        Engine::AssetDatabase canvas_database(source_dir);
                        Engine::CanvasUISystem canvas_ui;
                        renderer.set_custom_render_target(canvas_target);
                        SDL_SetRenderTarget(sdl_renderer, canvas_target);
                        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                        SDL_RenderClear(sdl_renderer);
                        renderer.begin_scene(camera);
                        const auto canvas_stats = canvas_ui.render(
                            canvas_entities,
                            renderer,
                            canvas_database,
                            {width, height}
                        );
                        renderer.end_scene();
                        const Pixel canvas_pixel = read_center(
                            sdl_renderer,
                            canvas_target,
                            width,
                            height
                        );

                        canvas_entities.get_component<Engine::UIImageComponent>(image_entity).enabled = false;
                        Engine::UITextComponent ui_text;
                        ui_text.text = "I";
                        ui_text.font_size = 32.0f;
                        ui_text.material = material;
                        canvas_entities.add_component<Engine::UITextComponent>(image_entity, ui_text);
                        renderer.set_custom_render_target(canvas_text_target);
                        SDL_SetRenderTarget(sdl_renderer, canvas_text_target);
                        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                        SDL_RenderClear(sdl_renderer);
                        renderer.begin_scene(camera);
                        const auto canvas_text_stats = canvas_ui.render(
                            canvas_entities,
                            renderer,
                            canvas_database,
                            {width, height}
                        );
                        renderer.end_scene();
                        const bool canvas_text_graded = contains_expected_grade(
                            sdl_renderer,
                            canvas_text_target
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

                        auto lighting = std::make_shared<Engine::Lighting2DPass>();
                        Engine::Light2DComponent point;
                        point.type = Engine::Light2DType::Point;
                        point.color = {255, 32, 16, 255};
                        point.intensity = 1.0f;
                        point.range = 28.0f;
                        point.shadow_mode = Engine::ShadowMode::None;
                        const glm::vec2 light_position{7.3f, -5.7f};
                        renderer.clear_lighting_passes();
                        renderer.add_lighting_pass(lighting);
                        renderer.set_material_backend(nullptr);
                        renderer.set_custom_render_target(lighting_target);
                        const auto render_lighting_probe = [&](Engine::Light2DBlendMode mode) {
                            point.blend_mode = mode;
                            lighting->update({{light_position, point}}, {});
                            SDL_SetRenderTarget(sdl_renderer, lighting_target);
                            SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                            SDL_RenderClear(sdl_renderer);
                            renderer.begin_scene(camera);
                            renderer.submit_sprite(
                                white.get(), {0.0f, 0.0f, 0.0f}, {48.0f, 48.0f}, 0.0f,
                                {64, 128, 192, 255}
                            );
                            renderer.end_scene();
                            const glm::vec2 sample = camera.world_to_screen(glm::vec3(light_position, 0.0f));
                            return read_pixel(
                                sdl_renderer,
                                lighting_target,
                                static_cast<int>(std::round(sample.x)),
                                static_cast<int>(std::round(sample.y))
                            );
                        };
                        const Pixel additive_pixel = render_lighting_probe(Engine::Light2DBlendMode::Additive);
                        const Pixel multiply_pixel = render_lighting_probe(Engine::Light2DBlendMode::Multiply);
                        const Pixel screen_pixel = render_lighting_probe(Engine::Light2DBlendMode::Screen);
                        point.intensity = 0.09f;
                        const Pixel zoom_100_pixel = render_lighting_probe(Engine::Light2DBlendMode::Additive);
                        camera.set_zoom(0.9f);
                        const Pixel zoom_090_pixel = render_lighting_probe(Engine::Light2DBlendMode::Additive);
                        camera.set_zoom(0.75f);
                        const Pixel zoom_075_pixel = render_lighting_probe(Engine::Light2DBlendMode::Additive);
                        camera.set_zoom(0.5f);
                        const Pixel zoom_050_pixel = render_lighting_probe(Engine::Light2DBlendMode::Additive);
                        camera.set_zoom(1.0f);
                        const auto render_lighting_translation_probe = [&](float camera_y) {
                            camera.set_position({0.0f, camera_y});
                            point.blend_mode = Engine::Light2DBlendMode::Additive;
                            lighting->update({{light_position, point}}, {});
                            SDL_SetRenderTarget(sdl_renderer, lighting_target);
                            SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                            SDL_RenderClear(sdl_renderer);
                            renderer.begin_scene(camera);
                            renderer.submit_sprite(
                                white.get(), {light_position.x, light_position.y, 0.0f},
                                {128.0f, 128.0f}, 0.0f, {64, 128, 192, 255}
                            );
                            renderer.submit_sprite(
                                white.get(), {0.0f, 34.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
                                {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
                                false, false, 0, 0, nullptr, 1
                            );
                            renderer.end_scene();
                            const glm::vec2 sample = camera.world_to_screen(
                                glm::vec3(light_position, 0.0f)
                            );
                            return read_pixel(
                                sdl_renderer,
                                lighting_target,
                                static_cast<int>(std::round(sample.x)),
                                static_cast<int>(std::round(sample.y))
                            );
                        };
                        const Pixel layer_visible_pixel = render_lighting_translation_probe(0.0f);
                        const Pixel layer_culled_pixel = render_lighting_translation_probe(-10.0f);
                        camera.set_position({0.0f, 0.0f});

                        std::unique_ptr<Engine::Texture> shadow_albedo(
                            Engine::Texture::create_solid(sdl_renderer, 4, 4, 32, 32, 32, 255)
                        );
                        const auto render_shadow_probe = [&](bool receives_shadows, Engine::ShadowMode mode) {
                            point.type = Engine::Light2DType::Point;
                            point.color = {255, 0, 0, 255};
                            point.range = 80.0f;
                            point.intensity = 1.0f;
                            point.height = 64.0f;
                            point.shadow_mode = mode;
                            point.shadow_strength = 1.0f;
                            point.shadow_max_distance = 64.0f;
                            lighting->update(
                                {{{-20.0f, 0.0f}, point, 1, 0.0f}},
                                {Engine::ShadowPolygon2D{
                                    {{-2.0f, -5.0f}, {2.0f, -5.0f}, {2.0f, 5.0f}, {-2.0f, 5.0f}},
                                    Engine::render_layer_mask(0), 32.0f, 1.0f, 2
                                }}
                            );
                            renderer.clear_lighting_passes();
                            renderer.add_lighting_pass(lighting);
                            renderer.set_custom_render_target(lighting_target);
                            SDL_SetRenderTarget(sdl_renderer, lighting_target);
                            SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                            SDL_RenderClear(sdl_renderer);
                            renderer.begin_scene(camera);
                            renderer.submit_sprite(
                                shadow_albedo.get(), {0.0f, 0.0f, 0.0f}, {60.0f, 48.0f}, 0.0f,
                                {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
                                false, false, 0, 0, nullptr, 0, {}, 0,
                                SDL_BLENDMODE_BLEND, false, nullptr, receives_shadows
                            );
                            renderer.end_scene();
                            return read_pixel(sdl_renderer, lighting_target, 42, height / 2);
                        };
                        const Pixel gpu_shadow = render_shadow_probe(true, Engine::ShadowMode::Hard);
                        const Pixel gpu_shadow_rejected = render_shadow_probe(false, Engine::ShadowMode::Hard);
                        const Pixel gpu_unshadowed = render_shadow_probe(true, Engine::ShadowMode::None);
                        const bool realtime_shadow_matches =
                            gpu_shadow.red + 20 < gpu_unshadowed.red &&
                            gpu_shadow_rejected.red + 5 >= gpu_unshadowed.red;

                        const std::filesystem::path baked_tile_path =
                            std::filesystem::temp_directory_path() /
                            ("engine2d-gpu-baked-" + std::to_string(SDL_GetTicksNS()) + ".png");
                        SDL_Surface* baked_tile_surface = SDL_CreateSurface(
                            width, height, SDL_PIXELFORMAT_RGBA32
                        );
                        bool baked_matches = false;
                        Pixel baked_receiver;
                        Pixel baked_non_receiver;
                        if (baked_tile_surface) {
                            SDL_FillSurfaceRect(
                                baked_tile_surface, nullptr,
                                SDL_MapSurfaceRGBA(baked_tile_surface, 64, 64, 64, 255)
                            );
                            if (SDL_SavePNG(baked_tile_surface, baked_tile_path.string().c_str())) {
                                Engine::LightingBakeManifest baked_manifest;
                                baked_manifest.status.current = true;
                                baked_manifest.status.input_hash = "gpu-smoke-bake";
                                baked_manifest.status.baked_light_count = 1;
                                baked_manifest.tiles.push_back({
                                    baked_tile_path, -32.0f, -32.0f, 1.0f,
                                    Engine::render_layer_mask(0), 0, width, height
                                });
                                auto baked_pass = std::make_shared<Engine::BakedLighting2DPass>();
                                if (baked_pass->load(baked_manifest, sdl_renderer)) {
                                    renderer.clear_lighting_passes();
                                    renderer.add_lighting_pass(baked_pass);
                                    renderer.set_custom_render_target(baked_target);
                                    SDL_SetRenderTarget(sdl_renderer, baked_target);
                                    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
                                    SDL_RenderClear(sdl_renderer);
                                    renderer.begin_scene(camera);
                                    renderer.submit_sprite(
                                        white.get(), {-16.0f, 0.0f, 0.0f}, {16.0f, 16.0f}, 0.0f,
                                        {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
                                        false, false, 0, 0, nullptr, 0, {}, 0,
                                        SDL_BLENDMODE_BLEND, true
                                    );
                                    renderer.submit_sprite(
                                        white.get(), {16.0f, 0.0f, 0.0f}, {16.0f, 16.0f}, 0.0f,
                                        {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
                                        false, false, 0, 0, nullptr, 0, {}, 0,
                                        SDL_BLENDMODE_BLEND, false
                                    );
                                    renderer.end_scene();
                                    baked_receiver = read_pixel(
                                        sdl_renderer, baked_target, 16, height / 2
                                    );
                                    baked_non_receiver = read_pixel(
                                        sdl_renderer, baked_target, 48, height / 2
                                    );
                                    baked_matches = baked_receiver.red >= 60 && baked_receiver.red <= 68 &&
                                        baked_non_receiver.red >= 250;
                                }
                            }
                            SDL_DestroySurface(baked_tile_surface);
                        }
                        std::error_code baked_file_error;
                        std::filesystem::remove(baked_tile_path, baked_file_error);
                        const auto channel_delta = [](Uint8 lhs, Uint8 rhs) {
                            return std::abs(static_cast<int>(lhs) - static_cast<int>(rhs));
                        };
                        const bool lighting_matches =
                            additive_pixel.red > 200 && additive_pixel.green > 140 &&
                            multiply_pixel.red >= 48 && multiply_pixel.red <= 80 &&
                            multiply_pixel.green < 80 && multiply_pixel.blue < 80 &&
                            screen_pixel.red > 200 && screen_pixel.green > 140 &&
                            screen_pixel.blue >= 180 && screen_pixel.alpha >= 247 &&
                            channel_delta(zoom_100_pixel.red, zoom_090_pixel.red) <= 2 &&
                            channel_delta(zoom_100_pixel.red, zoom_075_pixel.red) <= 2 &&
                            channel_delta(zoom_100_pixel.red, zoom_050_pixel.red) <= 2 &&
                            channel_delta(zoom_100_pixel.green, zoom_050_pixel.green) <= 2 &&
                            channel_delta(zoom_100_pixel.blue, zoom_050_pixel.blue) <= 2 &&
                            channel_delta(layer_visible_pixel.red, layer_culled_pixel.red) <= 2 &&
                            channel_delta(layer_visible_pixel.green, layer_culled_pixel.green) <= 2 &&
                            channel_delta(layer_visible_pixel.blue, layer_culled_pixel.blue) <= 2;

                        if (renderer.get_frame_metrics().stage(
                                Engine::RenderStage::Lighting
                            ).draw_calls != 1 ||
                            !fullscreen_drawn ||
                            !matches_expected_grade(sprite_pixel) ||
                            canvas_stats.images != 1 ||
                            !matches_expected_grade(canvas_pixel) ||
                            canvas_text_stats.text_lines != 1 ||
                            !canvas_text_graded ||
                            !matches_expected_grade(fullscreen_pixel) ||
                            !lit_sprite_matches ||
                            !realtime_shadow_matches ||
                            !baked_matches ||
                            !lighting_matches) {
                            std::cerr
                                << "GPURenderSmokeTests failed on " << backend->backend_name()
                                << ": sprite RGBA=(" << static_cast<int>(sprite_pixel.red) << ','
                                << static_cast<int>(sprite_pixel.green) << ','
                                << static_cast<int>(sprite_pixel.blue) << ','
                                << static_cast<int>(sprite_pixel.alpha) << ") canvas RGBA=("
                                << static_cast<int>(canvas_pixel.red) << ','
                                << static_cast<int>(canvas_pixel.green) << ','
                                << static_cast<int>(canvas_pixel.blue) << ','
                                << static_cast<int>(canvas_pixel.alpha) << ") fullscreen RGBA=("
                                << static_cast<int>(fullscreen_pixel.red) << ','
                                << static_cast<int>(fullscreen_pixel.green) << ','
                                << static_cast<int>(fullscreen_pixel.blue) << ','
                                << static_cast<int>(fullscreen_pixel.alpha) << ") lit sprite RGBA=("
                                << static_cast<int>(lit_sprite_pixel.red) << ','
                                << static_cast<int>(lit_sprite_pixel.green) << ','
                                << static_cast<int>(lit_sprite_pixel.blue) << ','
                                << static_cast<int>(lit_sprite_pixel.alpha) << ") normal RGBA=("
                                << static_cast<int>(normal_sprite_pixel.red) << ','
                                << static_cast<int>(normal_sprite_pixel.green) << ','
                                << static_cast<int>(normal_sprite_pixel.blue) << ','
                                << static_cast<int>(normal_sprite_pixel.alpha) << ") specular RGBA=("
                                << static_cast<int>(specular_sprite_pixel.red) << ','
                                << static_cast<int>(specular_sprite_pixel.green) << ','
                                << static_cast<int>(specular_sprite_pixel.blue) << ','
                                << static_cast<int>(specular_sprite_pixel.alpha) << ") additive RGBA=("
                                << static_cast<int>(additive_pixel.red) << ','
                                << static_cast<int>(additive_pixel.green) << ','
                                << static_cast<int>(additive_pixel.blue) << ','
                                << static_cast<int>(additive_pixel.alpha) << ") multiply RGBA=("
                                << static_cast<int>(multiply_pixel.red) << ','
                                << static_cast<int>(multiply_pixel.green) << ','
                                << static_cast<int>(multiply_pixel.blue) << ','
                                << static_cast<int>(multiply_pixel.alpha) << ") screen RGBA=("
                                << static_cast<int>(screen_pixel.red) << ','
                                << static_cast<int>(screen_pixel.green) << ','
                                << static_cast<int>(screen_pixel.blue) << ','
                                << static_cast<int>(screen_pixel.alpha) << ") baked receiver/nonreceiver red=("
                                << static_cast<int>(baked_receiver.red) << ','
                                << static_cast<int>(baked_non_receiver.red)
                                << ") realtime shadow/rejected/unshadowed red=("
                                << static_cast<int>(gpu_shadow.red) << ','
                                << static_cast<int>(gpu_shadow_rejected.red) << ','
                                << static_cast<int>(gpu_unshadowed.red)
                                << ") lighting draws=" << renderer.get_frame_metrics().stage(
                                    Engine::RenderStage::Lighting).draw_calls
                                << ") zoom 1.0/0.9/0.75/0.5 red=("
                                << static_cast<int>(zoom_100_pixel.red) << ','
                                << static_cast<int>(zoom_090_pixel.red) << ','
                                << static_cast<int>(zoom_075_pixel.red) << ','
                                << static_cast<int>(zoom_050_pixel.red) << ") translated visible/culled RGBA=("
                                << static_cast<int>(layer_visible_pixel.red) << ','
                                << static_cast<int>(layer_visible_pixel.green) << ','
                                << static_cast<int>(layer_visible_pixel.blue) << ','
                                << static_cast<int>(layer_visible_pixel.alpha) << ")/("
                                << static_cast<int>(layer_culled_pixel.red) << ','
                                << static_cast<int>(layer_culled_pixel.green) << ','
                                << static_cast<int>(layer_culled_pixel.blue) << ','
                                << static_cast<int>(layer_culled_pixel.alpha) << ")\n";
                        } else {
                            std::cout << "GPURenderSmokeTests passed on " << backend->backend_name()
                                      << ": sprite, material pack, realtime/baked shadows, Canvas UI, fullscreen, blend, and zoom-stability probes matched\n";
                            exit_code = 0;
                        }
                    }
                }
                if (sprite_target) SDL_DestroyTexture(sprite_target);
                if (fullscreen_source) SDL_DestroyTexture(fullscreen_source);
                if (fullscreen_target) SDL_DestroyTexture(fullscreen_target);
                if (lighting_target) SDL_DestroyTexture(lighting_target);
                if (baked_target) SDL_DestroyTexture(baked_target);
                if (canvas_target) SDL_DestroyTexture(canvas_target);
                if (canvas_text_target) SDL_DestroyTexture(canvas_text_target);
                if (lit_sprite_target) SDL_DestroyTexture(lit_sprite_target);
            }
        }
    }

    Engine::AssetManager::get().clear();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
