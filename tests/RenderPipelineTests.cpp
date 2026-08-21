#include "renderer/Renderer2D.hpp"
#include "renderer/Texture.hpp"
#include "renderer/postprocess/IPostProcessPass.hpp"
#include "renderer/postprocess/PostProcessPipeline.hpp"
#include "scene/Camera2D.hpp"
#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

class OrderedLocalPass final : public Engine::ILocalEffectPass {
public:
    OrderedLocalPass(std::vector<std::string>& order, std::string label)
        : m_order(order), m_label(std::move(label)) {}
    [[nodiscard]] const char* name() const override { return m_label.c_str(); }
    [[nodiscard]] bool is_enabled() const override { return true; }
    bool apply(const Engine::LocalEffectContext& context) override {
        m_order.push_back(m_label);
        ++apply_count;
        if (!SDL_SetRenderTarget(context.renderer, context.destination)) return false;
        SDL_SetRenderDrawColor(context.renderer, 0, 0, 0, 0);
        SDL_RenderClear(context.renderer);
        return SDL_RenderTexture(context.renderer, context.source, nullptr, nullptr);
    }
    int apply_count{0};
private:
    std::vector<std::string>& m_order;
    std::string m_label;
};

class OrderedGlobalPass final : public Engine::IPostProcessPass {
public:
    explicit OrderedGlobalPass(std::vector<std::string>& order) : m_order(order) {}
    const std::string& get_name() const override { return m_name; }
    bool is_enabled() const override { return true; }
    void set_enabled(bool) override {}
    void apply(
        SDL_Renderer* renderer,
        SDL_Texture* source,
        SDL_Texture* destination,
        const Engine::PostProcessContext&
    ) override {
        m_order.push_back("global");
        assert(SDL_SetRenderTarget(renderer, destination));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        SDL_SetTextureColorMod(source, 128, 255, 128);
        assert(SDL_RenderTexture(renderer, source, nullptr, nullptr));
        SDL_SetTextureColorMod(source, 255, 255, 255);
    }
private:
    std::vector<std::string>& m_order;
    std::string m_name{"Ordered Global"};
};

void read_pixel(SDL_Surface* surface, int x, int y, Uint8& red, Uint8& green, Uint8& blue, Uint8& alpha) {
    assert(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha));
}

void test_entity_layer_global_and_hud_order() {
    constexpr int width = 64;
    constexpr int height = 32;
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        Engine::Renderer2D renderer(sdl_renderer);
        renderer.set_scene_clear_color({0, 0, 0, 255});
        Engine::Camera2D camera(static_cast<float>(width), static_cast<float>(height));
        std::unique_ptr<Engine::Texture> white(
            Engine::Texture::create_solid(sdl_renderer, 4, 4, 255, 255, 255, 255)
        );
        assert(white);

        std::vector<std::string> order;
        const Engine::AssetHandle entity_profile{1001};
        auto entity_pass = std::make_shared<OrderedLocalPass>(order, "entity");
        auto layer_pass = std::make_shared<OrderedLocalPass>(order, "layer");
        renderer.set_entity_effect_profile(entity_profile, {entity_pass});
        renderer.set_render_layer_effects(2, {layer_pass});

        Engine::PostProcessPipeline pipeline;
        pipeline.init(sdl_renderer, width, height, width, height);
        pipeline.add_pass(std::make_shared<OrderedGlobalPass>(order));
        renderer.set_post_process_pipeline(&pipeline);
        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {-16.0f, 0.0f, 0.0f}, {12.0f, 12.0f}, 23.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 2, entity_profile, 77
        );
        renderer.submit_sprite(
            white.get(), {0.0f, 0.0f, 0.0f}, {12.0f, 12.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 2
        );
        renderer.submit_screen_sprite(
            white.get(), {56.0f, 16.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}
        );
        renderer.end_scene();

        assert(order == std::vector<std::string>({"entity", "layer", "global"}));
        assert(entity_pass->apply_count == 1);
        assert(layer_pass->apply_count == 1);
        const auto& metrics = renderer.get_frame_metrics();
        assert(metrics.stage(Engine::RenderStage::SceneGeometry).sprites == 2);
        assert(metrics.stage(Engine::RenderStage::LocalEffects).draw_calls == 4);
        assert(metrics.stage(Engine::RenderStage::CanvasUI).sprites == 1);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);

        SDL_RenderPresent(sdl_renderer);
        Uint8 world_r = 0, world_g = 0, world_b = 0, world_a = 0;
        read_pixel(surface, 32, 16, world_r, world_g, world_b, world_a);
        assert(world_r >= 126 && world_r <= 130);
        assert(world_g == 255);
        assert(world_b >= 126 && world_b <= 130);
        assert(world_a == 255);
        Uint8 hud_r = 0, hud_g = 0, hud_b = 0, hud_a = 0;
        read_pixel(surface, 56, 16, hud_r, hud_g, hud_b, hud_a);
        assert(hud_r == 255 && hud_g == 255 && hud_b == 255 && hud_a == 255);
        pipeline.destroy_targets();
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

void test_fast_path_and_frame_state_reset() {
    SDL_Surface* surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        Engine::Renderer2D renderer(sdl_renderer);
        Engine::Camera2D camera(32.0f, 32.0f);
        std::unique_ptr<Engine::Texture> white(
            Engine::Texture::create_solid(sdl_renderer, 4, 4, 255, 255, 255, 255)
        );
        assert(white);

        renderer.begin_scene(camera);
        renderer.submit_sprite(white.get(), {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f});
        renderer.end_scene();
        assert(renderer.get_frame_metrics().stage(Engine::RenderStage::SceneGeometry).draw_calls == 1);
        assert(renderer.get_frame_metrics().stage(Engine::RenderStage::LocalEffects).draw_calls == 0);
        assert(renderer.get_render_target_pool_metrics().allocated_targets == 0);

        std::vector<std::string> order;
        const Engine::AssetHandle profile{1002};
        auto pass = std::make_shared<OrderedLocalPass>(order, "entity");
        renderer.set_entity_effect_profile(profile, {pass});
        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 0, profile, 1
        );
        renderer.end_scene();
        assert(pass->apply_count == 1);
        assert(renderer.get_frame_metrics().stage(Engine::RenderStage::LocalEffects).draw_calls == 2);
        const std::size_t allocated_after_effect = renderer.get_render_target_pool_metrics().allocated_targets;
        assert(allocated_after_effect >= 2);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);

        renderer.clear_all_entity_effect_profiles();
        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 0, profile, 1
        );
        renderer.end_scene();
        assert(pass->apply_count == 1);
        assert(renderer.get_frame_metrics().stage(Engine::RenderStage::SceneGeometry).sprites == 1);
        assert(renderer.get_frame_metrics().stage(Engine::RenderStage::LocalEffects).draw_calls == 0);
        assert(renderer.get_render_target_pool_metrics().allocated_targets == allocated_after_effect);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

void assert_texture_size(SDL_Texture* texture, int expected_width, int expected_height) {
    assert(texture);
    float width = 0.0f;
    float height = 0.0f;
    assert(SDL_GetTextureSize(texture, &width, &height));
    assert(static_cast<int>(width) == expected_width);
    assert(static_cast<int>(height) == expected_height);
}

void test_pipeline_resize_recreates_intermediate_surfaces() {
    SDL_Surface* surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        Engine::PostProcessPipeline pipeline;
        pipeline.init(sdl_renderer, 32, 24, 128, 72);
        assert_texture_size(pipeline.get_capture_target(), 32, 24);
        assert_texture_size(pipeline.get_half_target_a(), 16, 12);
        assert_texture_size(pipeline.get_quarter_target(), 8, 6);

        pipeline.resize(48, 20);
        assert(pipeline.get_width() == 48);
        assert(pipeline.get_height() == 20);
        assert(pipeline.get_context().output_width == 48);
        assert(pipeline.get_context().output_height == 20);
        assert_texture_size(pipeline.get_capture_target(), 48, 20);
        assert_texture_size(pipeline.get_half_target_a(), 24, 10);
        assert_texture_size(pipeline.get_quarter_target(), 12, 5);

        pipeline.destroy_targets();
        assert(pipeline.get_capture_target() == nullptr);
        assert(pipeline.get_half_target_a() == nullptr);
        assert(pipeline.get_quarter_target() == nullptr);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

} // namespace

int main() {
    test_entity_layer_global_and_hud_order();
    test_fast_path_and_frame_state_reset();
    test_pipeline_resize_recreates_intermediate_surfaces();
    std::cout << "RenderPipelineTests passed\n";
    return 0;
}
