#include "Renderer2D.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "core/ThreadPool.hpp"
#include "core/Log.hpp"
#include "core/SIMD.hpp"
#include <algorithm>
#include <chrono>

namespace Engine {

Renderer2D::Renderer2D(SDL_Renderer* renderer)
    : m_sdl_renderer(renderer) {
    reserve_capacity(10000);
}

void Renderer2D::reserve_capacity(size_t max_sprites) {
    m_sprite_queue.reserve(max_sprites);
    m_sort_temp_buffer.reserve(max_sprites);
    m_vertex_buffer.reserve(max_sprites * 4);
    m_index_buffer.reserve(max_sprites * 6);
}

void Renderer2D::begin_scene(const Camera2D& camera) {
    m_active_camera = &camera;
    m_sprite_queue.clear();
    m_draw_calls = 0;
    m_rendered_sprites = 0;
    m_culled_sprites = 0;

    if (m_post_process_pipeline && m_post_process_pipeline->get_active_pass_count() > 0) {
        m_post_process_pipeline->begin_capture();
    }
}

void Renderer2D::submit_sprite(
    Texture* texture, 
    const glm::vec3& position, 
    const glm::vec2& size, 
    float rotation, 
    SDL_Color color,
    const SDL_FRect* src_rect,
    const glm::vec2& pivot,
    bool is_overlay,
    bool flip_x,
    bool flip_y
) {
    // 2D Frustum Culling Check
    if (m_active_camera && !m_active_camera->is_visible(position, size)) {
        m_culled_sprites++;
        return; // Early discard out-of-screen sprite
    }

    SpriteCommand cmd{texture, position, size, rotation, pivot, color};
    if (src_rect) {
        cmd.src_rect = *src_rect;
        cmd.has_src_rect = true;
    }
    cmd.flip_x = flip_x;
    cmd.flip_y = flip_y;

    if (is_overlay) {
        cmd.sort_key = 0xFFFFFFFFFFFFFFFFULL; // Force overlay to sort after all game world sprites!
    } else {
        // Fast 64-bit multi-key quantization (Z-depth, Y-position, Texture Pointer)
        uint32_t z_key = static_cast<uint32_t>(position.z + 10000.0f);
        uint16_t y_key = static_cast<uint16_t>(position.y + 32768.0f);
        uint16_t tex_key = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(texture) & 0xFFFF);
        cmd.sort_key = (static_cast<uint64_t>(z_key) << 32) | (static_cast<uint64_t>(y_key) << 16) | tex_key;
    }

    m_sprite_queue.push_back(cmd);
}

void Renderer2D::submit_sprites_batch(const SpriteCommand* commands, size_t count) {
    if (!commands || count == 0) return;

    if (m_thread_pool && m_thread_pool->get_thread_count() > 1 && count >= 512) {
        std::vector<SpriteCommand> processed_cmds(count);
        std::vector<uint8_t> is_visible(count, 0);

        m_thread_pool->parallel_for(0, count, [&](size_t i) {
            const auto& src = commands[i];
            if (!src.texture) return;

            if (m_active_camera && !m_active_camera->is_visible(src.position, src.size)) {
                return; // Culled
            }

            SpriteCommand cmd = src;
            uint32_t z_key = static_cast<uint32_t>(src.position.z + 10000.0f);
            uint16_t y_key = static_cast<uint16_t>(src.position.y + 32768.0f);
            uint16_t tex_key = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(src.texture) & 0xFFFF);
            cmd.sort_key = (static_cast<uint64_t>(z_key) << 32) | (static_cast<uint64_t>(y_key) << 16) | tex_key;

            processed_cmds[i] = cmd;
            is_visible[i] = 1;
        });

        for (size_t i = 0; i < count; ++i) {
            if (is_visible[i]) {
                m_sprite_queue.push_back(processed_cmds[i]);
            } else {
                m_culled_sprites++;
            }
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            submit_sprite(
                commands[i].texture,
                commands[i].position,
                commands[i].size,
                commands[i].rotation,
                commands[i].color,
                commands[i].has_src_rect ? &commands[i].src_rect : nullptr
            );
        }
    }
}

void Renderer2D::flush_batch(Texture* texture) {
    // Legacy single flush stub (retained for API compatibility)
}

void Renderer2D::end_scene() {
    if (!m_active_camera || !m_sdl_renderer || m_sprite_queue.empty()) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    size_t sprite_count = m_sprite_queue.size();

    // 1. O(N) 64-bit LSD Radix Sort (4 passes of 16-bit buckets)
    m_sort_temp_buffer.resize(sprite_count);

    for (int shift = 0; shift < 64; shift += 16) {
        size_t count[65536] = {0};

        for (size_t i = 0; i < sprite_count; ++i) {
            uint16_t bucket = static_cast<uint16_t>((m_sprite_queue[i].sort_key >> shift) & 0xFFFF);
            count[bucket]++;
        }

        size_t pref[65536];
        pref[0] = 0;
        for (int i = 1; i < 65536; ++i) {
            pref[i] = pref[i - 1] + count[i - 1];
        }

        for (size_t i = 0; i < sprite_count; ++i) {
            uint16_t bucket = static_cast<uint16_t>((m_sprite_queue[i].sort_key >> shift) & 0xFFFF);
            m_sort_temp_buffer[pref[bucket]++] = m_sprite_queue[i];
        }

        m_sprite_queue.swap(m_sort_temp_buffer);
    }

    auto t1 = std::chrono::high_resolution_clock::now();

    // 2. Resize output vertex & index buffers to exact required capacity
    m_vertex_buffer.resize(sprite_count * 4);
    m_index_buffer.resize(sprite_count * 6);

    auto process_sprite = [this](size_t idx) {
        const auto& cmd = m_sprite_queue[idx];

        // Calculate GPU screen position based on camera parallax and Z-depth
        glm::vec2 screen_pos = m_active_camera->world_to_screen(cmd.position);
        float z_scale = m_active_camera->get_z_scale(cmd.position.z) * m_active_camera->get_effective_zoom();

        glm::vec2 scaled_size = cmd.size * z_scale;
        float min_x = -cmd.pivot.x * scaled_size.x;
        float max_x = (1.0f - cmd.pivot.x) * scaled_size.x;
        float min_y = -cmd.pivot.y * scaled_size.y;
        float max_y = (1.0f - cmd.pivot.y) * scaled_size.y;

        // Quad corners relative to pivot anchor (SIMD transformed)
        glm::vec2 corners_rel[4] = {
            {min_x, min_y},
            {max_x, min_y},
            {max_x, max_y},
            {min_x, max_y}
        };

        glm::vec2 corners[4];
        SIMD::transform_quad_corners(corners_rel, corners, screen_pos, cmd.rotation);

        // Normalize UVs (0.0f to 1.0f)
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        if (cmd.has_src_rect && cmd.texture && cmd.texture->get_width() > 0 && cmd.texture->get_height() > 0) {
            float tw = static_cast<float>(cmd.texture->get_width());
            float th = static_cast<float>(cmd.texture->get_height());
            u0 = cmd.src_rect.x / tw;
            v0 = cmd.src_rect.y / th;
            u1 = (cmd.src_rect.x + cmd.src_rect.w) / tw;
            v1 = (cmd.src_rect.y + cmd.src_rect.h) / th;
        }

        if (cmd.flip_x) {
            std::swap(u0, u1);
        }
        if (cmd.flip_y) {
            std::swap(v0, v1);
        }

        SDL_FColor col{
            cmd.color.r / 255.0f,
            cmd.color.g / 255.0f,
            cmd.color.b / 255.0f,
            cmd.color.a / 255.0f
        };

        size_t v_idx = idx * 4;
        m_vertex_buffer[v_idx + 0] = SDL_Vertex{{corners[0].x, corners[0].y}, col, {u0, v0}};
        m_vertex_buffer[v_idx + 1] = SDL_Vertex{{corners[1].x, corners[1].y}, col, {u1, v0}};
        m_vertex_buffer[v_idx + 2] = SDL_Vertex{{corners[2].x, corners[2].y}, col, {u1, v1}};
        m_vertex_buffer[v_idx + 3] = SDL_Vertex{{corners[3].x, corners[3].y}, col, {u0, v1}};

        size_t i_idx = idx * 6;
        int base_v = static_cast<int>(v_idx);
        m_index_buffer[i_idx + 0] = base_v + 0;
        m_index_buffer[i_idx + 1] = base_v + 1;
        m_index_buffer[i_idx + 2] = base_v + 2;
        m_index_buffer[i_idx + 3] = base_v + 0;
        m_index_buffer[i_idx + 4] = base_v + 2;
        m_index_buffer[i_idx + 5] = base_v + 3;
    };

    // 3. Multi-threaded parallel vertex generation
    if (m_thread_pool && m_thread_pool->get_thread_count() > 1 && sprite_count >= 256) {
        m_thread_pool->parallel_for(0, sprite_count, process_sprite);
    } else {
        for (size_t i = 0; i < sprite_count; ++i) {
            process_sprite(i);
        }
    }

    auto t2 = std::chrono::high_resolution_clock::now();

    // 4. Batch flushing by texture ranges
    size_t batch_start = 0;
    Texture* current_texture = m_sprite_queue[0].texture;

    for (size_t i = 1; i <= sprite_count; ++i) {
        Texture* tex = (i < sprite_count) ? m_sprite_queue[i].texture : nullptr;
        if (tex != current_texture || i == sprite_count) {
            size_t batch_count = i - batch_start;
            if (batch_count > 0) {
                SDL_Texture* sdl_tex = current_texture ? current_texture->get_sdl_texture() : nullptr;
                SDL_RenderGeometry(
                    m_sdl_renderer,
                    sdl_tex,
                    m_vertex_buffer.data(),
                    static_cast<int>(m_vertex_buffer.size()),
                    &m_index_buffer[batch_start * 6],
                    static_cast<int>(batch_count * 6)
                );
                m_draw_calls++;
                m_rendered_sprites += batch_count;
            }
            batch_start = i;
            current_texture = tex;
        }
    }

    // 5. Execute Post-Processing Shader Chain (if active)
    if (m_post_process_pipeline && m_post_process_pipeline->get_active_pass_count() > 0) {
        m_post_process_pipeline->end_capture_and_process(nullptr);
    }
    m_post_process_pipeline = nullptr; // Reset per-frame so pipeline never leaks across different scenes!

    auto t3 = std::chrono::high_resolution_clock::now();

    // Log detailed sub-stage breakdown every 2 seconds for heavy scenes
    static float log_timer = 0.0f;
    log_timer += 0.016f;
    if (log_timer >= 2.0f && sprite_count >= 30000) {
        log_timer = 0.0f;
        float sort_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
        float vert_ms = std::chrono::duration<float, std::milli>(t2 - t1).count();
        float gpu_ms  = std::chrono::duration<float, std::milli>(t3 - t2).count();
        ENGINE_LOG_INFO("[PIPELINE PROFILE] Sort: {} ms | Vertices (Parallel): {} ms | GPU Submit: {} ms",
                        sort_ms, vert_ms, gpu_ms);
    }
}

} // namespace Engine
