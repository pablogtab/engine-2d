#include "renderer/LightingPass.hpp"
#include "renderer/Renderer2D.hpp"
#include "renderer/Texture.hpp"
#include "renderer/material/FragmentShader.hpp"
#include "renderer/material/IMaterialBackend.hpp"
#include "renderer/material/Material.hpp"
#include "renderer/postprocess/IPostProcessPass.hpp"
#include "renderer/postprocess/PostProcessPipeline.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/RenderSystem.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "scene/Camera2D.hpp"
#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {

class RecordingMaterialBackend final : public Engine::IMaterialBackend {
public:
    [[nodiscard]] bool is_available() const override { return true; }
    [[nodiscard]] const char* backend_name() const override { return "recording"; }
    bool bind(const Engine::Material&, const Engine::MaterialRenderContext&) override {
        ++bind_count;
        return true;
    }
    void unbind() override { ++unbind_count; }
    bool draw_fullscreen(
        const Engine::Material&,
        SDL_Texture*,
        const Engine::MaterialRenderContext&
    ) override {
        return true;
    }
    int bind_count{0};
    int unbind_count{0};
};

class SleepingLocalPass final : public Engine::ILocalEffectPass {
public:
    [[nodiscard]] const char* name() const override { return "Sleeping Local"; }
    [[nodiscard]] bool is_enabled() const override { return true; }
    bool apply(const Engine::LocalEffectContext& context) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!SDL_SetRenderTarget(context.renderer, context.destination)) return false;
        SDL_SetRenderDrawColor(context.renderer, 0, 0, 0, 0);
        SDL_RenderClear(context.renderer);
        return SDL_RenderTexture(context.renderer, context.source, nullptr, nullptr);
    }
};

class SleepingLightingPass final : public Engine::ILightingPass {
public:
    [[nodiscard]] const char* name() const override { return "Sleeping Light"; }
    [[nodiscard]] bool is_enabled() const override { return true; }
    [[nodiscard]] Engine::RenderLayerMask affected_layers() const override {
        return Engine::ALL_RENDER_LAYERS;
    }
    bool apply(const Engine::LightingContext&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
    }
};

class SleepingGlobalPass final : public Engine::IPostProcessPass {
public:
    const std::string& get_name() const override { return m_name; }
    bool is_enabled() const override { return true; }
    void set_enabled(bool) override {}
    void apply(
        SDL_Renderer* renderer,
        SDL_Texture* source,
        SDL_Texture* destination,
        const Engine::PostProcessContext&
    ) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        assert(SDL_SetRenderTarget(renderer, destination));
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        assert(SDL_RenderTexture(renderer, source, nullptr, nullptr));
    }
private:
    std::string m_name{"Sleeping Global"};
};

void assert_frame_metrics_are_zero(const Engine::RenderFrameMetrics& metrics) {
    for (std::size_t index = 0; index < static_cast<std::size_t>(Engine::RenderStage::Count); ++index) {
        const auto& stage = metrics.stages[index];
        assert(stage.draw_calls == 0);
        assert(stage.sprites == 0);
        assert(stage.material_switches == 0);
        assert(stage.cpu_milliseconds == 0.0f);
    }
}

void test_stage_metrics_and_frame_reset() {
    constexpr int width = 64;
    constexpr int height = 32;
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        Engine::Renderer2D renderer(sdl_renderer);
        Engine::Camera2D camera(static_cast<float>(width), static_cast<float>(height));
        std::unique_ptr<Engine::Texture> white(
            Engine::Texture::create_solid(sdl_renderer, 4, 4, 255, 255, 255, 255)
        );
        assert(white);

        auto shader = std::make_shared<Engine::FragmentShader>("Metrics Shader");
        auto material_a = std::make_shared<Engine::Material>("Metrics A", shader);
        auto material_b = std::make_shared<Engine::Material>("Metrics B", shader);
        auto material_backend = std::make_shared<RecordingMaterialBackend>();
        renderer.set_material_backend(material_backend);

        const Engine::AssetHandle effect_profile{3001};
        renderer.set_entity_effect_profile(effect_profile, {std::make_shared<SleepingLocalPass>()});
        renderer.add_lighting_pass(std::make_shared<SleepingLightingPass>());

        Engine::PostProcessPipeline pipeline;
        pipeline.init(sdl_renderer, width, height, width, height);
        pipeline.add_pass(std::make_shared<SleepingGlobalPass>());
        renderer.set_post_process_pipeline(&pipeline);

        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {-10.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, material_a, 0, effect_profile, 91
        );
        renderer.submit_sprite(
            white.get(), {10.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 1, material_b
        );
        renderer.submit_screen_sprite(
            white.get(), {4.0f, 4.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.0f, 0.0f}
        );
        renderer.end_scene();

        const auto& metrics = renderer.get_frame_metrics();
        const auto& geometry = metrics.stage(Engine::RenderStage::SceneGeometry);
        const auto& local = metrics.stage(Engine::RenderStage::LocalEffects);
        const auto& lighting = metrics.stage(Engine::RenderStage::Lighting);
        const auto& global = metrics.stage(Engine::RenderStage::GlobalPostProcess);
        const auto& canvas = metrics.stage(Engine::RenderStage::CanvasUI);
        assert(geometry.sprites == 2);
        assert(geometry.draw_calls == 2);
        assert(geometry.material_switches == 2);
        assert(geometry.cpu_milliseconds >= 0.0f);
        assert(local.draw_calls == 2);
        assert(local.cpu_milliseconds >= 1.0f);
        assert(lighting.draw_calls == 1);
        assert(lighting.cpu_milliseconds >= 1.0f);
        assert(global.draw_calls == 2);
        assert(global.cpu_milliseconds >= 1.0f);
        assert(canvas.sprites == 1);
        assert(canvas.draw_calls == 1);
        assert(material_backend->bind_count == 2);
        assert(material_backend->unbind_count == 2);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
        assert(renderer.present());
        const auto& presentation = renderer.get_frame_metrics().stage(
            Engine::RenderStage::Presentation
        );
        assert(presentation.draw_calls == 1);
        assert(presentation.cpu_milliseconds >= 0.0f);

        const auto pool_before_reset = renderer.get_render_target_pool_metrics();
        renderer.clear_all_entity_effect_profiles();
        renderer.clear_lighting_passes();
        renderer.begin_scene(camera);
        renderer.end_scene();
        assert_frame_metrics_are_zero(renderer.get_frame_metrics());
        assert(renderer.get_render_target_pool_metrics().allocated_targets == pool_before_reset.allocated_targets);
        assert(renderer.get_render_target_pool_metrics().allocated_bytes == pool_before_reset.allocated_bytes);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
        pipeline.destroy_targets();
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

void test_render_target_memory_metrics() {
    SDL_Surface* surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        Engine::RenderTargetPool pool(sdl_renderer);
        SDL_Texture* first = pool.acquire(16, 8);
        SDL_Texture* second = pool.acquire(16, 8);
        assert(first && second && first != second);
        assert(pool.metrics().allocated_targets == 2);
        assert(pool.metrics().leased_targets == 2);
        assert(pool.metrics().allocated_bytes == 16U * 8U * 4U * 2U);

        pool.release(first);
        SDL_Texture* reused = pool.acquire(16, 8);
        assert(reused == first);
        assert(pool.metrics().allocated_targets == 2);
        assert(pool.metrics().allocated_bytes == 16U * 8U * 4U * 2U);
        pool.release(reused);
        pool.release(second);
        assert(pool.metrics().leased_targets == 0);

        pool.clear();
        assert(pool.metrics().allocated_targets == 0);
        assert(pool.metrics().leased_targets == 0);
        assert(pool.metrics().allocated_bytes == 0);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

void test_material_only_sprite_reaches_the_renderer() {
    SDL_Surface* surface = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        auto shader = std::make_shared<Engine::FragmentShader>("Procedural Quad");
        shader->set_sampler_count(0);
        auto material = std::make_shared<Engine::Material>("Procedural Quad", shader);

        Engine::EntityManager entities;
        const Engine::EntityID entity = entities.create_entity();
        entities.add_component<Engine::TransformComponent>(entity, Engine::TransformComponent{});
        Engine::SpriteComponent sprite;
        sprite.texture = nullptr;
        sprite.material = material;
        sprite.size = {8.0f, 8.0f};
        entities.add_component<Engine::SpriteComponent>(entity, sprite);

        auto backend = std::make_shared<RecordingMaterialBackend>();
        Engine::Renderer2D renderer(sdl_renderer);
        renderer.set_material_backend(backend);
        Engine::Camera2D camera(32.0f, 32.0f);
        renderer.begin_scene(camera);
        Engine::RenderSystem::render(entities, renderer);
        renderer.end_scene();

        assert(backend->bind_count == 1);
        assert(renderer.get_frame_metrics().stage(
            Engine::RenderStage::SceneGeometry
        ).sprites == 1);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

} // namespace

int main() {
    test_stage_metrics_and_frame_reset();
    test_render_target_memory_metrics();
    test_material_only_sprite_reaches_the_renderer();
    std::cout << "RenderMetricsTests passed\n";
    return 0;
}
