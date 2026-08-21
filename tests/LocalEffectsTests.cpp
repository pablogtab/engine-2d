#include "renderer/Renderer2D.hpp"
#include "renderer/Texture.hpp"
#include "scene/Camera2D.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

class OrderedCopyPass final : public Engine::ILocalEffectPass {
public:
    OrderedCopyPass(std::vector<int>& order, int id) : m_order(order), m_id(id) {}
    [[nodiscard]] const char* name() const override { return "Ordered Copy"; }
    [[nodiscard]] bool is_enabled() const override { return enabled; }
    bool apply(const Engine::LocalEffectContext& context) override {
        m_order.push_back(m_id);
        ++apply_count;
        if (!succeed || !SDL_SetRenderTarget(context.renderer, context.destination)) return false;
        SDL_SetRenderDrawColor(context.renderer, 0, 0, 0, 0);
        SDL_RenderClear(context.renderer);
        return SDL_RenderTexture(context.renderer, context.source, nullptr, nullptr);
    }
    bool enabled{true};
    bool succeed{true};
    int apply_count{0};
private:
    std::vector<int>& m_order;
    int m_id{0};
};

void test_multipass_order_failure_and_pool_reuse() {
    SDL_Surface* surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        Engine::Renderer2D renderer(sdl_renderer);
        std::unique_ptr<Engine::Texture> source(
            Engine::Texture::create_solid(sdl_renderer, 8, 8, 255, 255, 255, 255)
        );
        assert(source);
        std::vector<int> order;
        OrderedCopyPass first(order, 1);
        OrderedCopyPass second(order, 2);
        OrderedCopyPass third(order, 3);
        std::array<Engine::ILocalEffectPass*, 3> passes{&first, &second, &third};

        SDL_Texture* output = renderer.execute_local_effect_chain(
            passes, source->get_sdl_texture(), 8, 8
        );
        assert(output);
        assert(order == std::vector<int>({1, 2, 3}));
        assert(renderer.get_render_target_pool_metrics().leased_targets == 1);
        renderer.release_local_effect_surface(output);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
        const std::size_t allocated = renderer.get_render_target_pool_metrics().allocated_targets;

        order.clear();
        output = renderer.execute_local_effect_chain(passes, source->get_sdl_texture(), 8, 8);
        assert(output && order == std::vector<int>({1, 2, 3}));
        renderer.release_local_effect_surface(output);
        assert(renderer.get_render_target_pool_metrics().allocated_targets == allocated);

        second.succeed = false;
        order.clear();
        assert(!renderer.execute_local_effect_chain(passes, source->get_sdl_texture(), 8, 8));
        assert(order == std::vector<int>({1, 2}));
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

Uint8 render_translucent_sprite(bool isolated, int& effect_applications) {
    constexpr int size = 48;
    SDL_Surface* surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    Uint8 red = 0, green = 0, blue = 0, alpha = 0;
    {
        Engine::Renderer2D renderer(sdl_renderer);
        renderer.set_scene_clear_color({0, 0, 0, 255});
        Engine::Camera2D camera(static_cast<float>(size), static_cast<float>(size));
        std::unique_ptr<Engine::Texture> white(
            Engine::Texture::create_solid(sdl_renderer, 4, 4, 255, 255, 255, 255)
        );
        assert(white);
        std::vector<int> order;
        auto effect = std::make_shared<OrderedCopyPass>(order, 1);
        const Engine::AssetHandle profile{2001};
        if (isolated) renderer.set_entity_effect_profile(profile, {effect});

        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {0.0f, 0.0f, 0.0f}, {20.0f, 20.0f}, 37.0f,
            {255, 0, 0, 128}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 0,
            isolated ? profile : Engine::AssetHandle{}, isolated ? 91 : 0
        );
        if (isolated) {
            renderer.submit_sprite(
                white.get(), {15.0f, 0.0f, 0.0f}, {4.0f, 4.0f}, 0.0f,
                {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
                false, false, 0, 0, nullptr, 0, profile, 91
            );
        }
        renderer.end_scene();
        effect_applications = effect->apply_count;
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
        SDL_RenderPresent(sdl_renderer);
        assert(SDL_ReadSurfacePixel(surface, size / 2, size / 2, &red, &green, &blue, &alpha));
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
    return red;
}

void test_entity_isolation_preserves_transform_and_alpha() {
    int direct_applications = 0;
    int isolated_applications = 0;
    const Uint8 direct_red = render_translucent_sprite(false, direct_applications);
    const Uint8 isolated_red = render_translucent_sprite(true, isolated_applications);
    assert(direct_applications == 0);
    assert(isolated_applications == 1);
    assert(std::abs(static_cast<int>(direct_red) - static_cast<int>(isolated_red)) <= 2);
    assert(direct_red >= 126 && direct_red <= 130);
}

void test_layer_effect_isolates_one_composition_group() {
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
        std::vector<int> order;
        auto effect = std::make_shared<OrderedCopyPass>(order, 1);
        renderer.set_render_layer_effects(3, {effect});

        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {-5.0f, 0.0f, 0.0f}, {6.0f, 6.0f}, 0.0f,
            {255, 0, 0, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 3
        );
        renderer.submit_sprite(
            white.get(), {5.0f, 0.0f, 0.0f}, {6.0f, 6.0f}, 0.0f,
            {0, 255, 0, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 3
        );
        renderer.submit_sprite(white.get(), {0.0f, 8.0f, 0.0f}, {4.0f, 4.0f});
        renderer.end_scene();
        assert(effect->apply_count == 1);
        assert(renderer.get_frame_metrics().stage(Engine::RenderStage::SceneGeometry).sprites == 3);
        assert(renderer.get_frame_metrics().stage(Engine::RenderStage::LocalEffects).draw_calls == 2);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

} // namespace

int main() {
    test_multipass_order_failure_and_pool_reuse();
    test_entity_isolation_preserves_transform_and_alpha();
    test_layer_effect_isolates_one_composition_group();
    std::cout << "LocalEffectsTests passed\n";
    return 0;
}
