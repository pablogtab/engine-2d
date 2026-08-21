#include "Renderer2D.hpp"
#include "SpriteSortKey.hpp"
#include "Font.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "material/IMaterialBackend.hpp"
#include "material/Material.hpp"
#include "core/ThreadPool.hpp"
#include "core/Log.hpp"
#include "core/SIMD.hpp"
#include <algorithm>
#include <chrono>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Engine {

Renderer2D::Renderer2D(SDL_Renderer* renderer)
    : m_sdl_renderer(renderer), m_render_target_pool(renderer) {
    reserve_capacity(10000);
}

void Renderer2D::reserve_capacity(size_t max_sprites) {
    m_sprite_queue.reserve(max_sprites);
    m_sort_temp_buffer.reserve(max_sprites);
    m_vertex_buffer.reserve(max_sprites * 4);
    m_index_buffer.reserve(max_sprites * 6);
}

SDL_Texture* Renderer2D::execute_local_effect(
    ILocalEffectPass& pass, SDL_Texture* source, int width, int height
) {
    if (!pass.is_enabled() || !source || width <= 0 || height <= 0) return nullptr;
    const auto stage_begin = std::chrono::high_resolution_clock::now();
    SDL_Texture* destination = acquire_local_effect_surface(width, height);
    if (!destination) return nullptr;
    const LocalEffectContext context{m_sdl_renderer, source, destination, width, height};
    if (!pass.apply(context)) {
        release_local_effect_surface(destination);
        return nullptr;
    }
    auto& metrics = m_frame_metrics.stages[static_cast<std::size_t>(RenderStage::LocalEffects)];
    ++metrics.draw_calls;
    metrics.cpu_milliseconds += std::chrono::duration<float, std::milli>(
        std::chrono::high_resolution_clock::now() - stage_begin
    ).count();
    return destination;
}

SDL_Texture* Renderer2D::execute_local_effect_chain(
    std::span<ILocalEffectPass* const> passes, SDL_Texture* source, int width, int height
) {
    if (!source || width <= 0 || height <= 0) return nullptr;

    SDL_Texture* current = source;
    bool current_is_leased = false;
    bool applied_effect = false;
    for (ILocalEffectPass* pass : passes) {
        if (!pass || !pass->is_enabled()) continue;
        SDL_Texture* output = execute_local_effect(*pass, current, width, height);
        if (!output) {
            if (current_is_leased) release_local_effect_surface(current);
            return nullptr;
        }
        if (current_is_leased) release_local_effect_surface(current);
        current = output;
        current_is_leased = true;
        applied_effect = true;
    }
    return applied_effect ? current : nullptr;
}

void Renderer2D::set_render_layer_effects(
    RenderLayer layer, std::vector<std::shared_ptr<ILocalEffectPass>> passes
) {
    m_render_layer_effects[std::min<RenderLayer>(layer, 31)] = std::move(passes);
}

void Renderer2D::clear_render_layer_effects(RenderLayer layer) {
    m_render_layer_effects[std::min<RenderLayer>(layer, 31)].clear();
}

void Renderer2D::clear_all_render_layer_effects() {
    for (auto& passes : m_render_layer_effects) passes.clear();
}

std::size_t Renderer2D::get_active_render_layer_effect_count(RenderLayer layer) const {
    const auto& passes = m_render_layer_effects[std::min<RenderLayer>(layer, 31)];
    return static_cast<std::size_t>(std::count_if(passes.begin(), passes.end(), [](const auto& pass) {
        return pass && pass->is_enabled();
    }));
}

void Renderer2D::set_entity_effect_profile(
    AssetHandle profile, std::vector<std::shared_ptr<ILocalEffectPass>> passes
) {
    if (!profile.is_valid()) return;
    m_entity_effect_profiles.insert_or_assign(profile, std::move(passes));
}

void Renderer2D::clear_all_entity_effect_profiles() {
    m_entity_effect_profiles.clear();
}

std::size_t Renderer2D::get_active_entity_effect_count(AssetHandle profile) const {
    const auto found = m_entity_effect_profiles.find(profile);
    if (found == m_entity_effect_profiles.end()) return 0;
    return static_cast<std::size_t>(std::count_if(found->second.begin(), found->second.end(), [](const auto& pass) {
        return pass && pass->is_enabled();
    }));
}

void Renderer2D::add_lighting_pass(std::shared_ptr<ILightingPass> pass) {
    if (!pass) return;
    m_lighting_passes.erase(
        std::remove_if(m_lighting_passes.begin(), m_lighting_passes.end(), [&pass](const auto& existing) {
            return existing && std::string_view(existing->name()) == pass->name();
        }),
        m_lighting_passes.end()
    );
    m_lighting_passes.push_back(std::move(pass));
}

void Renderer2D::clear_lighting_passes() { m_lighting_passes.clear(); }

std::size_t Renderer2D::get_active_lighting_pass_count() const {
    return static_cast<std::size_t>(std::count_if(
        m_lighting_passes.begin(), m_lighting_passes.end(),
        [](const auto& pass) { return pass && pass->is_enabled(); }
    ));
}

std::size_t Renderer2D::get_active_lighting_pass_count(RenderLayer layer) const {
    return static_cast<std::size_t>(std::count_if(
        m_lighting_passes.begin(), m_lighting_passes.end(),
        [layer](const auto& pass) {
            return pass && pass->is_enabled() && renders_layer(pass->affected_layers(), layer);
        }
    ));
}

void Renderer2D::execute_lighting_stage(
    SDL_Texture* scene_target,
    RenderLayer layer,
    int width,
    int height
) {
    if (!scene_target || width <= 0 || height <= 0) return;
    auto& metrics = m_frame_metrics.stages[static_cast<std::size_t>(RenderStage::Lighting)];
    for (const auto& pass : m_lighting_passes) {
        if (!pass || !pass->is_enabled() || !renders_layer(pass->affected_layers(), layer)) continue;
        const auto stage_begin = std::chrono::high_resolution_clock::now();
        const RenderLayerMask previously_composited_layers =
            m_lighting_composited_layers[pass.get()];
        if (pass->apply({
                m_sdl_renderer,
                scene_target,
                m_active_camera,
                layer,
                width,
                height,
                previously_composited_layers
            })) {
            ++metrics.draw_calls;
            m_lighting_composited_layers[pass.get()] |= render_layer_mask(layer);
        }
        metrics.cpu_milliseconds += std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - stage_begin
        ).count();
    }
}

void Renderer2D::begin_scene(const Camera2D& camera) {
    m_active_camera = &camera;
    m_sprite_queue.clear();
    m_draw_calls = 0;
    m_rendered_sprites = 0;
    m_culled_sprites = 0;
    m_frame_metrics = {};
    m_lighting_composited_layers.clear();

    if (m_custom_render_target) {
        SDL_SetRenderTarget(m_sdl_renderer, m_custom_render_target);
    }

    if (m_post_process_pipeline && m_post_process_pipeline->get_active_pass_count() > 0) {
        // The editor clears its Viewport render target before scene rendering.
        // Include that backdrop in the capture so full-screen passes affect the
        // entire camera frame, not only geometry drawn after begin_scene().
        m_post_process_pipeline->begin_capture(m_custom_render_target, m_scene_clear_color);
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
    bool flip_y,
    int16_t sorting_layer,
    int16_t order_in_layer,
    std::shared_ptr<const Material> material,
    RenderLayer render_layer,
    AssetHandle entity_effect_profile,
    std::uint64_t entity_effect_scope
) {
    render_layer = std::min<RenderLayer>(render_layer, 31);
    if (m_active_camera && !m_active_camera->renders_layer(render_layer)) {
        ++m_culled_sprites;
        return;
    }
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
    cmd.sorting_layer = sorting_layer;
    cmd.order_in_layer = order_in_layer;
    cmd.render_layer = render_layer;
    cmd.material = std::move(material);
    cmd.entity_effect_profile = entity_effect_profile;
    cmd.entity_effect_scope = entity_effect_profile.is_valid() ? entity_effect_scope : 0;

    if (is_overlay) {
        cmd.sort_key = 0xFFFFFFFFFFFFFFFFULL; // Force overlay to sort after all game world sprites!
    } else {
        // Explicit layer/order precede depth and Y sorting. Texture batching is
        // handled later by contiguous equal-texture commands where possible.
        cmd.sort_key = SpriteSortKey::world(
            sorting_layer,
            position.z,
            position.y,
            order_in_layer
        );
    }

    m_sprite_queue.push_back(cmd);
}

void Renderer2D::submit_screen_sprite(
    Texture* texture,
    const glm::vec2& screen_position,
    const glm::vec2& size,
    float rotation,
    SDL_Color color,
    const SDL_FRect* src_rect,
    const glm::vec2& pivot,
    float z_layer,
    bool flip_x,
    bool flip_y,
    std::shared_ptr<const Material> material
) {
    if (!texture) return;

    SpriteCommand cmd;
    cmd.texture = texture;
    cmd.position = glm::vec3(screen_position.x, screen_position.y, z_layer);
    cmd.size = size;
    cmd.rotation = rotation;
    cmd.pivot = pivot;
    cmd.color = color;
    if (src_rect) {
        cmd.src_rect = *src_rect;
        cmd.has_src_rect = true;
    }
    cmd.flip_x = flip_x;
    cmd.flip_y = flip_y;
    cmd.is_screen_space = true;
    cmd.material = std::move(material);

    // Screen-space sort key: Bit 63 is 1 to guarantee rendering on top of all world geometry!
    cmd.sort_key = SpriteSortKey::screen(z_layer, texture);

    m_sprite_queue.push_back(cmd);
}

void Renderer2D::submit_text(
    const Font& font,
    const std::string& text,
    const glm::vec3& world_position,
    float scale,
    SDL_Color color,
    const glm::vec2& pivot
) {
    if (text.empty()) return;
    Texture* atlas = font.get_atlas_texture();
    if (!atlas) return;

    glm::vec2 text_size = font.measure_text(text, scale);
    glm::vec2 origin = glm::vec2(world_position.x, world_position.y) - (text_size * pivot);

    float cur_x = origin.x;
    float cur_y = origin.y;

    for (char c : text) {
        if (c == '\n') {
            cur_x = origin.x;
            cur_y -= font.get_line_height() * scale;
            continue;
        }

        const Glyph* g = font.get_glyph(c);
        if (!g) continue;

        if (g->size.x > 0.0f && g->size.y > 0.0f) {
            glm::vec3 glyph_pos = glm::vec3(
                cur_x + g->offset.x * scale,
                cur_y - g->offset.y * scale - (font.get_font_size() * 0.85f * scale),
                world_position.z
            );
            glm::vec2 glyph_size = g->size * scale;

            submit_sprite(
                atlas,
                glyph_pos,
                glyph_size,
                0.0f,
                color,
                &g->src_rect,
                glm::vec2(0.0f, 0.0f)
            );
        }

        cur_x += g->advance_x * scale;
    }
}

void Renderer2D::submit_screen_text(
    const Font& font,
    const std::string& text,
    const glm::vec2& screen_position,
    float scale,
    SDL_Color color,
    const glm::vec2& pivot,
    float z_layer
) {
    if (text.empty()) return;
    Texture* atlas = font.get_atlas_texture();
    if (!atlas) return;

    glm::vec2 text_size = font.measure_text(text, scale);
    glm::vec2 origin = screen_position - (text_size * pivot);

    float cur_x = origin.x;
    float cur_y = origin.y;

    for (char c : text) {
        if (c == '\n') {
            cur_x = origin.x;
            cur_y += font.get_line_height() * scale;
            continue;
        }

        const Glyph* g = font.get_glyph(c);
        if (!g) continue;

        if (g->size.x > 0.0f && g->size.y > 0.0f) {
            glm::vec2 glyph_pos = glm::vec2(
                cur_x + g->offset.x * scale,
                cur_y + g->offset.y * scale
            );
            glm::vec2 glyph_size = g->size * scale;

            submit_screen_sprite(
                atlas,
                glyph_pos,
                glyph_size,
                0.0f,
                color,
                &g->src_rect,
                glm::vec2(0.0f, 0.0f),
                z_layer
            );
        }

        cur_x += g->advance_x * scale;
    }
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

void Renderer2D::end_scene() {
    if (!m_active_camera || !m_sdl_renderer) return;

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

    size_t world_space_count = sprite_count;
    for (size_t index = 0; index < sprite_count; ++index) {
        if (m_sprite_queue[index].is_screen_space) {
            world_space_count = index;
            break;
        }
    }

    // Render layers are world-space composition groups. Keep the screen-space
    // partition untouched so Canvas/UI always remains after global processing.
    // Counting placement keeps this grouping linear and stable after radix sort.
    std::array<size_t, 32> layer_counts{};
    for (size_t index = 0; index < world_space_count; ++index) {
        ++layer_counts[m_sprite_queue[index].render_layer];
    }
    std::array<size_t, 32> layer_offsets{};
    for (size_t layer = 1; layer < layer_offsets.size(); ++layer) {
        layer_offsets[layer] = layer_offsets[layer - 1] + layer_counts[layer - 1];
    }
    auto write_offsets = layer_offsets;
    for (size_t index = 0; index < world_space_count; ++index) {
        const auto& command = m_sprite_queue[index];
        m_sort_temp_buffer[write_offsets[command.render_layer]++] = command;
    }
    for (size_t index = world_space_count; index < sprite_count; ++index) {
        m_sort_temp_buffer[index] = m_sprite_queue[index];
    }
    m_sprite_queue.swap(m_sort_temp_buffer);

    // An affected entity is one composition item even when it emitted several
    // primitives (outlines or tilemap cells). Compact each entity's commands at
    // its first authored position while preserving their internal sorted order.
    std::unordered_map<std::uint64_t, std::vector<size_t>> entity_groups;
    for (size_t index = 0; index < world_space_count; ++index) {
        const auto& command = m_sprite_queue[index];
        if (command.entity_effect_scope == 0 || !command.entity_effect_profile.is_valid()) continue;
        const std::uint64_t group_key =
            (static_cast<std::uint64_t>(command.render_layer) << 56) | command.entity_effect_scope;
        entity_groups[group_key].push_back(index);
    }
    if (!entity_groups.empty()) {
        std::unordered_set<std::uint64_t> emitted_groups;
        size_t output_index = 0;
        for (size_t index = 0; index < world_space_count; ++index) {
            const auto& command = m_sprite_queue[index];
            if (command.entity_effect_scope == 0 || !command.entity_effect_profile.is_valid()) {
                m_sort_temp_buffer[output_index++] = command;
                continue;
            }
            const std::uint64_t group_key =
                (static_cast<std::uint64_t>(command.render_layer) << 56) | command.entity_effect_scope;
            if (!emitted_groups.insert(group_key).second) continue;
            for (const size_t grouped_index : entity_groups[group_key]) {
                m_sort_temp_buffer[output_index++] = m_sprite_queue[grouped_index];
            }
        }
        for (size_t index = world_space_count; index < sprite_count; ++index) {
            m_sort_temp_buffer[output_index++] = m_sprite_queue[index];
        }
        m_sprite_queue.swap(m_sort_temp_buffer);
    }

    auto t1 = std::chrono::high_resolution_clock::now();

    // 2. Resize output vertex & index buffers to exact required capacity
    m_vertex_buffer.resize(sprite_count * 4);
    m_index_buffer.resize(sprite_count * 6);

    auto process_sprite = [this](size_t idx) {
        const auto& cmd = m_sprite_queue[idx];

        glm::vec2 screen_pos;
        glm::vec2 scaled_size;

        if (cmd.is_screen_space) {
            const float projection_scale = m_active_camera ? m_active_camera->get_projection_scale() : 1.0f;
            screen_pos = glm::vec2(cmd.position.x, cmd.position.y) * projection_scale;
            scaled_size = cmd.size * projection_scale;
        } else {
            // Calculate GPU screen position based on camera parallax and Z-depth
            screen_pos = m_active_camera->world_to_screen(cmd.position);
            float z_scale = m_active_camera->get_z_scale(cmd.position.z) * m_active_camera->get_effective_zoom();
            scaled_size = cmd.size * z_scale;
        }

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
        const float view_rotation = cmd.is_screen_space
            ? cmd.rotation
            : cmd.rotation + m_active_camera->get_rotation();
        SIMD::transform_quad_corners(corners_rel, corners, screen_pos, view_rotation);

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

    // 4. Find separation point between World-Space and Screen-Space (Sorted via Bit 63)
    const size_t screen_space_start = world_space_count;

    int material_output_width = 1;
    int material_output_height = 1;
    SDL_GetCurrentRenderOutputSize(m_sdl_renderer, &material_output_width, &material_output_height);
    const MaterialRenderContext material_context{material_output_width, material_output_height};

    auto flush_range = [&](RenderStage stage, size_t start_idx, size_t end_idx) {
        if (start_idx >= end_idx) return;
        auto& metrics = m_frame_metrics.stages[static_cast<std::size_t>(stage)];
        const auto stage_begin = std::chrono::high_resolution_clock::now();
        size_t batch_start = start_idx;
        Texture* current_texture = m_sprite_queue[start_idx].texture;
        auto current_material = m_sprite_queue[start_idx].material;

        for (size_t i = start_idx + 1; i <= end_idx; ++i) {
            Texture* tex = (i < end_idx) ? m_sprite_queue[i].texture : nullptr;
            auto material = (i < end_idx) ? m_sprite_queue[i].material : nullptr;
            if (tex != current_texture || material != current_material || i == end_idx) {
                size_t batch_count = i - batch_start;
                if (batch_count > 0) {
                    const bool material_bound = current_material && m_material_backend &&
                        m_material_backend->bind(*current_material, material_context);
                    if (material_bound) ++metrics.material_switches;
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
                    metrics.draw_calls++;
                    metrics.sprites += batch_count;
                    if (material_bound) m_material_backend->unbind();
                }
                batch_start = i;
                current_texture = tex;
                current_material = std::move(material);
            }
        }
        metrics.cpu_milliseconds += std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - stage_begin
        ).count();
    };

    SDL_Texture* const scene_target = SDL_GetRenderTarget(m_sdl_renderer);
    int composition_width = 0;
    int composition_height = 0;
    if (scene_target) {
        float texture_width = 0.0f;
        float texture_height = 0.0f;
        SDL_GetTextureSize(scene_target, &texture_width, &texture_height);
        composition_width = static_cast<int>(texture_width);
        composition_height = static_cast<int>(texture_height);
    } else {
        SDL_GetCurrentRenderOutputSize(m_sdl_renderer, &composition_width, &composition_height);
    }

    auto flush_entity_effects = [&](
        size_t start_idx,
        size_t end_idx,
        SDL_Texture* composition_target
    ) {
        size_t direct_start = start_idx;
        size_t index = start_idx;
        while (index < end_idx) {
            const auto& command = m_sprite_queue[index];
            const auto profile_found = m_entity_effect_profiles.find(command.entity_effect_profile);
            const bool has_effect = command.entity_effect_scope != 0 &&
                profile_found != m_entity_effect_profiles.end() &&
                get_active_entity_effect_count(command.entity_effect_profile) > 0;
            if (!has_effect) {
                ++index;
                continue;
            }

            if (direct_start < index) flush_range(RenderStage::SceneGeometry, direct_start, index);
            size_t group_end = index + 1;
            while (group_end < end_idx &&
                   m_sprite_queue[group_end].entity_effect_scope == command.entity_effect_scope &&
                   m_sprite_queue[group_end].entity_effect_profile == command.entity_effect_profile) {
                ++group_end;
            }

            SDL_Texture* source = acquire_local_effect_surface(composition_width, composition_height);
            if (!source || !SDL_SetRenderTarget(m_sdl_renderer, source)) {
                if (source) release_local_effect_surface(source);
                SDL_SetRenderTarget(m_sdl_renderer, composition_target);
                flush_range(RenderStage::SceneGeometry, index, group_end);
            } else {
                SDL_SetRenderDrawColor(m_sdl_renderer, 0, 0, 0, 0);
                SDL_RenderClear(m_sdl_renderer);
                flush_range(RenderStage::SceneGeometry, index, group_end);
                std::vector<ILocalEffectPass*> passes;
                for (const auto& pass : profile_found->second) {
                    if (pass && pass->is_enabled()) passes.push_back(pass.get());
                }
                SDL_Texture* output = execute_local_effect_chain(
                    passes, source, composition_width, composition_height
                );
                SDL_SetRenderTarget(m_sdl_renderer, composition_target);
                SDL_RenderTexture(m_sdl_renderer, output ? output : source, nullptr, nullptr);
                ++m_frame_metrics.stages[static_cast<std::size_t>(RenderStage::LocalEffects)].draw_calls;
                if (output) release_local_effect_surface(output);
                release_local_effect_surface(source);
            }
            index = group_end;
            direct_start = index;
        }
        if (direct_start < end_idx) {
            SDL_SetRenderTarget(m_sdl_renderer, composition_target);
            flush_range(RenderStage::SceneGeometry, direct_start, end_idx);
        }
    };

    // A. Flush each render-layer composition group. Effect-free layers stay on
    // the direct path; affected layers are isolated, processed, and composited.
    size_t layer_start = 0;
    while (layer_start < screen_space_start) {
        const RenderLayer layer = m_sprite_queue[layer_start].render_layer;
        size_t layer_end = layer_start + 1;
        while (layer_end < screen_space_start && m_sprite_queue[layer_end].render_layer == layer) ++layer_end;

        const bool has_layer_effects = get_active_render_layer_effect_count(layer) > 0;
        const bool has_lighting = get_active_lighting_pass_count(layer) > 0;
        if (!has_layer_effects && !has_lighting) {
            flush_entity_effects(layer_start, layer_end, scene_target);
        } else {
            SDL_Texture* source = acquire_local_effect_surface(composition_width, composition_height);
            if (!source || !SDL_SetRenderTarget(m_sdl_renderer, source)) {
                if (source) release_local_effect_surface(source);
                SDL_SetRenderTarget(m_sdl_renderer, scene_target);
                flush_entity_effects(layer_start, layer_end, scene_target);
            } else {
                SDL_SetRenderDrawColor(m_sdl_renderer, 0, 0, 0, 0);
                SDL_RenderClear(m_sdl_renderer);
                flush_entity_effects(layer_start, layer_end, source);
                SDL_Texture* output = nullptr;
                if (has_layer_effects) {
                    std::vector<ILocalEffectPass*> passes;
                    for (const auto& pass : m_render_layer_effects[layer]) {
                        if (pass && pass->is_enabled()) passes.push_back(pass.get());
                    }
                    output = execute_local_effect_chain(
                        passes, source, composition_width, composition_height
                    );
                }
                SDL_Texture* const layer_target = output ? output : source;
                if (has_lighting) {
                    execute_lighting_stage(layer_target, layer, composition_width, composition_height);
                }
                SDL_SetRenderTarget(m_sdl_renderer, scene_target);
                SDL_RenderTexture(m_sdl_renderer, layer_target, nullptr, nullptr);
                if (has_layer_effects) {
                    ++m_frame_metrics.stages[static_cast<std::size_t>(RenderStage::LocalEffects)].draw_calls;
                }
                if (output) release_local_effect_surface(output);
                release_local_effect_surface(source);
            }
        }
        layer_start = layer_end;
    }

    // B. Execute Post-Processing Shader Chain on the lit world and output to
    // the custom render target (or backbuffer).
    if (m_post_process_pipeline && m_post_process_pipeline->get_active_pass_count() > 0) {
        const auto post_process_begin = std::chrono::high_resolution_clock::now();
        const std::size_t active_passes = m_post_process_pipeline->get_active_pass_count();
        m_post_process_pipeline->end_capture_and_process(m_custom_render_target);
        auto& metrics = m_frame_metrics.stages[static_cast<std::size_t>(RenderStage::GlobalPostProcess)];
        // One logical draw per pass plus the final processed-frame composite.
        metrics.draw_calls = active_passes + 1;
        metrics.cpu_milliseconds = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - post_process_begin
        ).count();
    }
    m_post_process_pipeline = nullptr; // Reset per-frame

    // C. Flush Screen-Space HUD / UI directly on top of the processed image (Crisp & Unaffected)
    if (m_custom_render_target) {
        SDL_SetRenderTarget(m_sdl_renderer, m_custom_render_target);
    }
    flush_range(RenderStage::CanvasUI, screen_space_start, sprite_count);

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

bool Renderer2D::present() {
    if (!m_sdl_renderer) return false;
    const auto stage_begin = std::chrono::high_resolution_clock::now();
    const bool presented = SDL_RenderPresent(m_sdl_renderer);
    auto& metrics = m_frame_metrics.stages[static_cast<std::size_t>(RenderStage::Presentation)];
    metrics.draw_calls = presented ? 1 : 0;
    metrics.cpu_milliseconds = std::chrono::duration<float, std::milli>(
        std::chrono::high_resolution_clock::now() - stage_begin
    ).count();
    return presented;
}

} // namespace Engine
