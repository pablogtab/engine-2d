#pragma once

#include "Texture.hpp"
#include "RenderTargetPool.hpp"
#include "LocalEffectPass.hpp"
#include "LightingPass.hpp"
#include "RenderLayer.hpp"
#include "../core/AssetHandle.hpp"
#include "../scene/Camera2D.hpp"
#include <glm/glm.hpp>
#include <array>
#include <span>
#include <vector>
#include <memory>
#include <unordered_map>
#include <SDL3/SDL.h>

namespace Engine {

class ThreadPool;
class PostProcessPipeline;
class Font;
class Material;
class IMaterialBackend;

enum class RenderStage : std::size_t {
    SceneGeometry,
    LocalEffects,
    Lighting,
    GlobalPostProcess,
    CanvasUI,
    Presentation,
    Count
};

struct RenderStageMetrics {
    std::size_t draw_calls{0};
    std::size_t sprites{0};
    std::size_t material_switches{0};
    float cpu_milliseconds{0.0f};
};

struct RenderFrameMetrics {
    std::array<RenderStageMetrics, static_cast<std::size_t>(RenderStage::Count)> stages{};
    [[nodiscard]] const RenderStageMetrics& stage(RenderStage value) const {
        return stages[static_cast<std::size_t>(value)];
    }
};

struct SpriteCommand {
    Texture* texture{nullptr};
    glm::vec3 position{0.0f}; // X, Y, Z (Depth)
    glm::vec2 size{32.0f, 32.0f};
    float rotation{0.0f};
    glm::vec2 pivot{0.5f, 0.5f}; // Normalized Pivot Anchor: (0.5, 0.5) = Center, (0.5, 0.0) = Feet
    SDL_Color color{255, 255, 255, 255};
    SDL_FRect src_rect{0.0f, 0.0f, 0.0f, 0.0f};
    uint64_t sort_key{0};
    bool has_src_rect{false};
    bool flip_x{false};
    bool flip_y{false};
    bool is_screen_space{false};
    bool scale_with_camera_projection{true};
    int16_t sorting_layer{0};
    int16_t order_in_layer{0};
    RenderLayer render_layer{DEFAULT_RENDER_LAYER};
    std::shared_ptr<const Material> material;
    AssetHandle entity_effect_profile{};
    std::uint64_t entity_effect_scope{0};
};

class Renderer2D {
public:
    explicit Renderer2D(SDL_Renderer* renderer);

    void set_thread_pool(ThreadPool* thread_pool) { m_thread_pool = thread_pool; }
    void set_low_latency_mode(bool low_latency) { m_low_latency_mode = low_latency; }
    bool is_low_latency_mode() const { return m_low_latency_mode; }

    void set_post_process_pipeline(PostProcessPipeline* pipeline) { m_post_process_pipeline = pipeline; }
    PostProcessPipeline* get_post_process_pipeline() const { return m_post_process_pipeline; }
    void set_material_backend(std::shared_ptr<IMaterialBackend> backend) { m_material_backend = std::move(backend); }
    [[nodiscard]] const std::shared_ptr<IMaterialBackend>& get_material_backend() const { return m_material_backend; }

    void set_custom_render_target(SDL_Texture* target) { 
        m_custom_render_target = target; 
        if (m_sdl_renderer) {
            SDL_SetRenderTarget(m_sdl_renderer, target);
        }
    }
    SDL_Texture* get_custom_render_target() const { return m_custom_render_target; }
    void set_scene_clear_color(SDL_Color color) { m_scene_clear_color = color; }
    // Local effects borrow intermediate surfaces from the renderer; callers
    // must return them before the end of their render pass.
    [[nodiscard]] SDL_Texture* acquire_local_effect_surface(int width, int height) {
        return m_render_target_pool.acquire(width, height);
    }
    void release_local_effect_surface(SDL_Texture* target) { m_render_target_pool.release(target); }
    // Returns a leased pooled destination. The caller releases it after its
    // consuming stage has completed.
    [[nodiscard]] SDL_Texture* execute_local_effect(
        ILocalEffectPass& pass, SDL_Texture* source, int width, int height
    );
    // Runs enabled passes in order. The returned surface remains leased; every
    // intermediate surface is returned to the pool before this call returns.
    [[nodiscard]] SDL_Texture* execute_local_effect_chain(
        std::span<ILocalEffectPass* const> passes, SDL_Texture* source, int width, int height
    );
    void set_render_layer_effects(RenderLayer layer, std::vector<std::shared_ptr<ILocalEffectPass>> passes);
    void clear_render_layer_effects(RenderLayer layer);
    void clear_all_render_layer_effects();
    [[nodiscard]] std::size_t get_active_render_layer_effect_count(RenderLayer layer) const;
    void set_entity_effect_profile(AssetHandle profile, std::vector<std::shared_ptr<ILocalEffectPass>> passes);
    void clear_all_entity_effect_profiles();
    [[nodiscard]] std::size_t get_active_entity_effect_count(AssetHandle profile) const;
    void add_lighting_pass(std::shared_ptr<ILightingPass> pass);
    void clear_lighting_passes();
    [[nodiscard]] std::size_t get_active_lighting_pass_count() const;
    [[nodiscard]] std::size_t get_active_lighting_pass_count(RenderLayer layer) const;

    void reserve_capacity(size_t max_sprites);
    void begin_scene(const Camera2D& camera);
    void submit_sprite(
        Texture* texture, 
        const glm::vec3& position, 
        const glm::vec2& size, 
        float rotation = 0.0f, 
        SDL_Color color = {255, 255, 255, 255},
        const SDL_FRect* src_rect = nullptr,
        const glm::vec2& pivot = {0.5f, 0.5f},
        bool is_overlay = false,
        bool flip_x = false,
        bool flip_y = false,
        int16_t sorting_layer = 0,
        int16_t order_in_layer = 0,
        std::shared_ptr<const Material> material = nullptr,
        RenderLayer render_layer = DEFAULT_RENDER_LAYER,
        AssetHandle entity_effect_profile = {},
        std::uint64_t entity_effect_scope = 0
    );
    void submit_screen_sprite(
        Texture* texture,
        const glm::vec2& screen_position,
        const glm::vec2& size,
        float rotation = 0.0f,
        SDL_Color color = {255, 255, 255, 255},
        const SDL_FRect* src_rect = nullptr,
        const glm::vec2& pivot = {0.0f, 0.0f},
        float z_layer = 0.0f,
        bool flip_x = false,
        bool flip_y = false,
        std::shared_ptr<const Material> material = nullptr,
        bool scale_with_camera_projection = true
    );
    void submit_text(
        const Font& font,
        const std::string& text,
        const glm::vec3& world_position,
        float scale = 1.0f,
        SDL_Color color = {255, 255, 255, 255},
        const glm::vec2& pivot = {0.0f, 0.0f}
    );
    void submit_screen_text(
        const Font& font,
        const std::string& text,
        const glm::vec2& screen_position,
        float scale = 1.0f,
        SDL_Color color = {255, 255, 255, 255},
        const glm::vec2& pivot = {0.0f, 0.0f},
        float z_layer = 1.0f,
        bool scale_with_camera_projection = true,
        std::shared_ptr<const Material> material = nullptr
    );
    void submit_sprites_batch(const SpriteCommand* commands, size_t count);
    void end_scene();
    bool present();

    size_t get_draw_call_count() const { return m_draw_calls; }
    size_t get_rendered_sprite_count() const { return m_rendered_sprites; }
    size_t get_culled_sprite_count() const { return m_culled_sprites; }
    [[nodiscard]] const RenderFrameMetrics& get_frame_metrics() const { return m_frame_metrics; }
    [[nodiscard]] const RenderTargetPoolMetrics& get_render_target_pool_metrics() const { return m_render_target_pool.metrics(); }
    const Camera2D* get_active_camera() const { return m_active_camera; }
    SDL_Renderer* get_sdl_renderer() const { return m_sdl_renderer; }

private:
    void execute_lighting_stage(SDL_Texture* scene_target, RenderLayer layer, int width, int height);

    SDL_Renderer* m_sdl_renderer{nullptr};
    SDL_Color m_scene_clear_color{24, 28, 36, 255};
    ThreadPool* m_thread_pool{nullptr};
    PostProcessPipeline* m_post_process_pipeline{nullptr};
    std::vector<std::shared_ptr<ILightingPass>> m_lighting_passes;
    std::unordered_map<const ILightingPass*, RenderLayerMask> m_lighting_composited_layers;
    std::array<std::vector<std::shared_ptr<ILocalEffectPass>>, 32> m_render_layer_effects;
    std::unordered_map<AssetHandle, std::vector<std::shared_ptr<ILocalEffectPass>>> m_entity_effect_profiles;
    std::shared_ptr<IMaterialBackend> m_material_backend;
    SDL_Texture* m_custom_render_target{nullptr};
    RenderTargetPool m_render_target_pool;
    const Camera2D* m_active_camera{nullptr};
    std::vector<SpriteCommand> m_sprite_queue;
    std::vector<SpriteCommand> m_sort_temp_buffer;

    std::vector<SDL_Vertex> m_vertex_buffer;
    std::vector<int> m_index_buffer;
    size_t m_draw_calls{0};
    size_t m_rendered_sprites{0};
    size_t m_culled_sprites{0};
    RenderFrameMetrics m_frame_metrics{};
    std::uint32_t m_screen_submission_order{0};
    bool m_low_latency_mode{true};
};

} // namespace Engine
