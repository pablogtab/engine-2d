#pragma once

#include "Texture.hpp"
#include "../scene/Camera2D.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <SDL3/SDL.h>

namespace Engine {

class ThreadPool;
class PostProcessPipeline;
class Font;

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
    int16_t sorting_layer{0};
    int16_t order_in_layer{0};
};

class Renderer2D {
public:
    explicit Renderer2D(SDL_Renderer* renderer);

    void set_thread_pool(ThreadPool* thread_pool) { m_thread_pool = thread_pool; }
    void set_low_latency_mode(bool low_latency) { m_low_latency_mode = low_latency; }
    bool is_low_latency_mode() const { return m_low_latency_mode; }

    void set_post_process_pipeline(PostProcessPipeline* pipeline) { m_post_process_pipeline = pipeline; }
    PostProcessPipeline* get_post_process_pipeline() const { return m_post_process_pipeline; }

    void set_custom_render_target(SDL_Texture* target) { 
        m_custom_render_target = target; 
        if (m_sdl_renderer) {
            SDL_SetRenderTarget(m_sdl_renderer, target);
        }
    }
    SDL_Texture* get_custom_render_target() const { return m_custom_render_target; }
    void set_scene_clear_color(SDL_Color color) { m_scene_clear_color = color; }

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
        int16_t order_in_layer = 0
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
        bool flip_y = false
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
        float z_layer = 1.0f
    );
    void submit_sprites_batch(const SpriteCommand* commands, size_t count);
    void end_scene();

    size_t get_draw_call_count() const { return m_draw_calls; }
    size_t get_rendered_sprite_count() const { return m_rendered_sprites; }
    size_t get_culled_sprite_count() const { return m_culled_sprites; }
    const Camera2D* get_active_camera() const { return m_active_camera; }
    SDL_Renderer* get_sdl_renderer() const { return m_sdl_renderer; }

private:
    void flush_batch(Texture* texture);

    SDL_Renderer* m_sdl_renderer{nullptr};
    SDL_Color m_scene_clear_color{24, 28, 36, 255};
    ThreadPool* m_thread_pool{nullptr};
    PostProcessPipeline* m_post_process_pipeline{nullptr};
    SDL_Texture* m_custom_render_target{nullptr};
    const Camera2D* m_active_camera{nullptr};
    std::vector<SpriteCommand> m_sprite_queue;
    std::vector<SpriteCommand> m_sort_temp_buffer;

    std::vector<SDL_Vertex> m_vertex_buffer;
    std::vector<int> m_index_buffer;
    size_t m_draw_calls{0};
    size_t m_rendered_sprites{0};
    size_t m_culled_sprites{0};
    bool m_low_latency_mode{true};
};

} // namespace Engine
