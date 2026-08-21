#pragma once

#include "EntityManager.hpp"
#include "components/CoreComponents.hpp"
#include "components/PhysicsComponents.hpp"
#include "components/RenderingComponents.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../scene/SpatialGrid.hpp"
#include "../core/AssetManager.hpp"
#include "../tilemap/Tilemap.hpp"
#include "../renderer/IEntityRenderFilter.hpp"
#include <algorithm>
#include <vector>

namespace Engine {

class RenderSystem {
public:
    // Render all entities possessing TransformComponent and SpriteComponent with 0-overhead Spatial Grid & Top-Down visibility
    static void render(
        EntityManager& entity_manager,
        Renderer2D& renderer,
        const IEntityRenderFilter* visibility_filter = nullptr
    ) {
        // Step 1: Re-populate Spatial Hash Grid with living renderable entities
        static SpatialGrid s_spatial_grid(256.0f);
        s_spatial_grid.clear();

        entity_manager.for_each<TransformComponent, SpriteComponent>(
            [visibility_filter](EntityID id, const TransformComponent& transform, const SpriteComponent& sprite) {
                if (visibility_filter && !visibility_filter->should_render(id)) return;
                if (transform.world_visible && sprite.visible && sprite.texture) {
                    s_spatial_grid.insert(id, transform.world_position, sprite.size * transform.world_scale);
                }
            }
        );

        // Step 2: Query candidate entities within camera bounds (or fallback to full set if no camera active)
        const Camera2D* active_cam = renderer.get_active_camera();
        if (active_cam) {
            SDL_FRect cam_bounds = active_cam->get_world_bounds();
            static std::vector<EntityID> visible_candidates;
            visible_candidates.clear();
            s_spatial_grid.query_bounds(cam_bounds, visible_candidates);

            // Deduplicate candidate IDs since large entities can overlap multiple grid cells
            std::sort(visible_candidates.begin(), visible_candidates.end());
            visible_candidates.erase(std::unique(visible_candidates.begin(), visible_candidates.end()), visible_candidates.end());

            for (EntityID id : visible_candidates) {
                if (visibility_filter && !visibility_filter->should_render(id)) continue;
                if (!entity_manager.has_component<TransformComponent>(id) || !entity_manager.has_component<SpriteComponent>(id)) continue;
                const auto& transform = entity_manager.get_component<TransformComponent>(id);
                const auto& sprite = entity_manager.get_component<SpriteComponent>(id);

                if (!transform.world_visible || !sprite.visible || !sprite.texture) continue;

                const SDL_FRect* src_rect_ptr = sprite.use_src_rect ? &sprite.src_rect : nullptr;
                submit_sprite_with_outline(renderer, transform, sprite, src_rect_ptr);
            }
        } else {
            // Fallback for un-camera-bound rendering
            entity_manager.for_each<TransformComponent, SpriteComponent>(
                [&renderer, visibility_filter](EntityID id, const TransformComponent& transform, const SpriteComponent& sprite) {
                    if (visibility_filter && !visibility_filter->should_render(id)) return;
                    if (!transform.world_visible || !sprite.visible || !sprite.texture) return;

                    const SDL_FRect* src_rect_ptr = sprite.use_src_rect ? &sprite.src_rect : nullptr;
                    submit_sprite_with_outline(renderer, transform, sprite, src_rect_ptr);
                }
            );
        }

        // Step 3: Render Tilemap Grid Components
        entity_manager.for_each<TransformComponent, TilemapComponent>(
            [&renderer, visibility_filter](EntityID id, const TransformComponent& transform, const TilemapComponent& tilemap) {
                if (visibility_filter && !visibility_filter->should_render(id)) return;
                if (!transform.world_visible || tilemap.empty()) return;

                Texture* tex = tilemap.tileset_texture;
                if (!tex && renderer.get_sdl_renderer()) {
                    tex = AssetManager::get().get_texture(renderer.get_sdl_renderer(), tilemap.tileset_path);
                }
                if (!tex) return;

                int tex_w = tex->get_width();
                int px_size = tilemap.tile_pixel_size;
                int cols = std::max(1, tex_w / px_size);

                float tile_sz = tilemap.tile_size * transform.world_scale.x;

                for (const auto& [key, tile_id] : tilemap.tiles) {
                    if (tile_id == 0) continue;

                    int x = 0, y = 0;
                    TilemapComponent::parse_key(key, x, y);

                    int tile_idx = tile_id - 1;
                    int c = tile_idx % cols;
                    int r = tile_idx / cols;

                    SDL_FRect src_rect{
                        static_cast<float>(c * px_size),
                        static_cast<float>(r * px_size),
                        static_cast<float>(px_size),
                        static_cast<float>(px_size)
                    };

                    float start_x = transform.world_position.x + (static_cast<float>(x) * tile_sz);
                    float start_y = transform.world_position.y - (static_cast<float>(y) * tile_sz);

                    glm::vec3 tile_pos(start_x + tile_sz * 0.5f, start_y - tile_sz * 0.5f, transform.world_position.z);

                    renderer.submit_sprite(
                        tex,
                        tile_pos,
                        glm::vec2(tile_sz, tile_sz),
                        transform.world_rotation,
                        SDL_Color{255, 255, 255, 255},
                        &src_rect,
                        glm::vec2(0.5f, 0.5f),
                        false,
                        false,
                        false,
                        0,
                        0,
                        nullptr,
                        tilemap.render_layer
                    );
                }
            }
        );
    }

private:
    static inline void submit_sprite_with_outline(
        Renderer2D& renderer,
        const TransformComponent& transform,
        const SpriteComponent& sprite,
        const SDL_FRect* src_rect_ptr
    ) {
        if (sprite.has_outline && sprite.outline_thickness > 0.0f) {
            float thick = sprite.outline_thickness;
            SDL_Color out_col = sprite.outline_color;

            if (sprite.outline_pulse) {
                static float s_time = 0.0f;
                s_time += 0.0001f;
                float pulse = 0.65f + 0.35f * std::sin(s_time * 24.0f);
                out_col.a = static_cast<uint8_t>(std::clamp(static_cast<float>(out_col.a) * pulse, 0.0f, 255.0f));
            }

            // Outline Z is slightly behind the main sprite
            glm::vec3 outline_z = transform.world_position - glm::vec3(0.0f, 0.0f, 0.005f);
            glm::vec2 size = sprite.size * transform.world_scale;

            // 8-directional dilation for full radial glow border
            float diag = thick * 0.7071f;
            renderer.submit_sprite(sprite.texture, outline_z + glm::vec3(-thick, 0.0f, 0.0f), size, transform.world_rotation, out_col, src_rect_ptr, transform.world_pivot, false, sprite.flip_x, sprite.flip_y, sprite.sorting_layer, sprite.order_in_layer, nullptr, sprite.render_layer);
            renderer.submit_sprite(sprite.texture, outline_z + glm::vec3(thick, 0.0f, 0.0f), size, transform.world_rotation, out_col, src_rect_ptr, transform.world_pivot, false, sprite.flip_x, sprite.flip_y, sprite.sorting_layer, sprite.order_in_layer, nullptr, sprite.render_layer);
            renderer.submit_sprite(sprite.texture, outline_z + glm::vec3(0.0f, -thick, 0.0f), size, transform.world_rotation, out_col, src_rect_ptr, transform.world_pivot, false, sprite.flip_x, sprite.flip_y, sprite.sorting_layer, sprite.order_in_layer, nullptr, sprite.render_layer);
            renderer.submit_sprite(sprite.texture, outline_z + glm::vec3(0.0f, thick, 0.0f), size, transform.world_rotation, out_col, src_rect_ptr, transform.world_pivot, false, sprite.flip_x, sprite.flip_y, sprite.sorting_layer, sprite.order_in_layer, nullptr, sprite.render_layer);
            renderer.submit_sprite(sprite.texture, outline_z + glm::vec3(-diag, -diag, 0.0f), size, transform.world_rotation, out_col, src_rect_ptr, transform.world_pivot, false, sprite.flip_x, sprite.flip_y, sprite.sorting_layer, sprite.order_in_layer, nullptr, sprite.render_layer);
            renderer.submit_sprite(sprite.texture, outline_z + glm::vec3(diag, -diag, 0.0f), size, transform.world_rotation, out_col, src_rect_ptr, transform.world_pivot, false, sprite.flip_x, sprite.flip_y, sprite.sorting_layer, sprite.order_in_layer, nullptr, sprite.render_layer);
            renderer.submit_sprite(sprite.texture, outline_z + glm::vec3(-diag, diag, 0.0f), size, transform.world_rotation, out_col, src_rect_ptr, transform.world_pivot, false, sprite.flip_x, sprite.flip_y, sprite.sorting_layer, sprite.order_in_layer, nullptr, sprite.render_layer);
            renderer.submit_sprite(sprite.texture, outline_z + glm::vec3(diag, diag, 0.0f), size, transform.world_rotation, out_col, src_rect_ptr, transform.world_pivot, false, sprite.flip_x, sprite.flip_y, sprite.sorting_layer, sprite.order_in_layer, nullptr, sprite.render_layer);
        }

        renderer.submit_sprite(
            sprite.texture,
            transform.world_position,
            sprite.size * transform.world_scale,
            transform.world_rotation,
            sprite.color,
            src_rect_ptr,
            transform.world_pivot,
            false,
            sprite.flip_x,
            sprite.flip_y,
            sprite.sorting_layer,
            sprite.order_in_layer,
            sprite.material,
            sprite.render_layer
        );
    }
public:

    // Render debug visualizer overlays for all active physics colliders
    static void render_debug_colliders(EntityManager& entity_manager, Renderer2D& renderer, Texture* circle_texture = nullptr) {
        entity_manager.for_each<TransformComponent, ColliderComponent>(
            [&renderer, circle_texture](EntityID id, const TransformComponent& transform, const ColliderComponent& collider) {
                if (!transform.world_visible) return;

                for (const auto& shape : collider.shapes) {
                    glm::vec3 shape_pos = transform.world_position + glm::vec3(shape.offset * transform.world_scale, 0.0f);
                    SDL_Color col = shape.is_trigger ? SDL_Color{0, 255, 255, 160} : SDL_Color{255, 60, 60, 160};

                    if (shape.type == ColliderShapeType::Box) {
                        renderer.submit_sprite(
                            nullptr,
                            shape_pos,
                            shape.size * transform.world_scale,
                            transform.world_rotation,
                            col,
                            nullptr,
                            transform.world_pivot,
                            true /* is_overlay = true to force render on top! */
                        );
                    } else if (shape.type == ColliderShapeType::Circle) {
                        renderer.submit_sprite(
                            circle_texture,
                            shape_pos,
                            glm::vec2(shape.radius * 2.0f) * transform.world_scale,
                            transform.world_rotation,
                            col,
                            nullptr,
                            glm::vec2(0.5f, 0.5f),
                            true /* is_overlay = true to force render on top! */
                        );
                    } else if (shape.type == ColliderShapeType::Polygon) {
                        size_t num_verts = shape.vertices.size();
                        if (num_verts >= 2) {
                            float rad = glm::radians(transform.world_rotation);
                            float cos_r = std::cos(rad);
                            float sin_r = std::sin(rad);

                            auto transform_pt = [&](const glm::vec2& v) -> glm::vec2 {
                                glm::vec2 scaled = (v + shape.offset) * transform.world_scale;
                                glm::vec2 rotated(scaled.x * cos_r - scaled.y * sin_r,
                                                  scaled.x * sin_r + scaled.y * cos_r);
                                return glm::vec2(transform.world_position.x + rotated.x,
                                                 transform.world_position.y + rotated.y);
                            };

                            for (size_t i = 0; i < num_verts; ++i) {
                                glm::vec2 pA = transform_pt(shape.vertices[i]);
                                glm::vec2 pB = transform_pt(shape.vertices[(i + 1) % num_verts]);

                                glm::vec2 delta = pB - pA;
                                float length = glm::length(delta);
                                if (length > 0.001f) {
                                    float angle = -glm::degrees(std::atan2(delta.y, delta.x));
                                    glm::vec2 mid = (pA + pB) * 0.5f;

                                    renderer.submit_sprite(
                                        nullptr,
                                        glm::vec3(mid, transform.world_position.z),
                                        glm::vec2(length, 3.0f),
                                        angle,
                                        col,
                                        nullptr,
                                        glm::vec2(0.5f, 0.5f),
                                        true
                                    );
                                }
                            }
                        }
                    }
                }
            }
        );

        // Render Joint connection lines (chains / cables)
        entity_manager.for_each<TransformComponent, JointComponent>(
            [&renderer, &entity_manager](EntityID idA, const TransformComponent& txA, const JointComponent& joint) {
                if (!entity_manager.is_alive(joint.target_entity) ||
                    !entity_manager.has_component<TransformComponent>(joint.target_entity)) {
                    return;
                }
                const auto& txB = entity_manager.get_component<TransformComponent>(joint.target_entity);

                float radA = glm::radians(txA.world_rotation);
                glm::vec2 rotA(joint.local_anchor_a.x * std::cos(radA) - joint.local_anchor_a.y * std::sin(radA),
                               joint.local_anchor_a.x * std::sin(radA) + joint.local_anchor_a.y * std::cos(radA));
                glm::vec2 anchorA = glm::vec2(txA.world_position.x, txA.world_position.y) + rotA * txA.world_scale;

                float radB = glm::radians(txB.world_rotation);
                glm::vec2 rotB(joint.local_anchor_b.x * std::cos(radB) - joint.local_anchor_b.y * std::sin(radB),
                               joint.local_anchor_b.x * std::sin(radB) + joint.local_anchor_b.y * std::cos(radB));
                glm::vec2 anchorB = glm::vec2(txB.world_position.x, txB.world_position.y) + rotB * txB.world_scale;

                glm::vec2 delta = anchorB - anchorA;
                float len = glm::length(delta);
                if (len < 0.001f) return;

                float max_l = (joint.distance_config.max_length >= 0.0f) ? joint.distance_config.max_length : len;
                float slack = max_l - len;

                // If it's a flexible rope and there is slack (d < max_length), draw a drooping catenary Bézier curve!
                if (joint.type == JointType::Rope && slack > 2.0f) {
                    float sag = std::min(slack * 0.65f, 150.0f);
                    glm::vec2 ctrl_pt = (anchorA + anchorB) * 0.5f + glm::vec2(0.0f, -sag);

                    constexpr int SEGMENTS = 12;
                    glm::vec2 prev_pt = anchorA;

                    for (int s = 1; s <= SEGMENTS; ++s) {
                        float t = static_cast<float>(s) / static_cast<float>(SEGMENTS);
                        float inv_t = 1.0f - t;
                        glm::vec2 curr_pt = (inv_t * inv_t) * anchorA +
                                            (2.0f * inv_t * t) * ctrl_pt +
                                            (t * t) * anchorB;

                        glm::vec2 seg_delta = curr_pt - prev_pt;
                        float seg_len = glm::length(seg_delta);
                        if (seg_len > 0.001f) {
                            float seg_angle = -glm::degrees(std::atan2(seg_delta.y, seg_delta.x));
                            glm::vec2 seg_mid = (prev_pt + curr_pt) * 0.5f;

                            renderer.submit_sprite(
                                nullptr,
                                glm::vec3(seg_mid, 0.0f),
                                glm::vec2(seg_len, 3.5f),
                                seg_angle,
                                SDL_Color{220, 180, 100, 240}, // Natural hemp rope golden brown
                                nullptr,
                                glm::vec2(0.5f, 0.5f),
                                true
                            );
                        }
                        prev_pt = curr_pt;
                    }
                } else {
                    // Tight cable or rigid rod
                    float angle = -glm::degrees(std::atan2(delta.y, delta.x));
                    glm::vec2 mid = (anchorA + anchorB) * 0.5f;
                    renderer.submit_sprite(
                        nullptr,
                        glm::vec3(mid, 0.0f),
                        glm::vec2(len, 3.5f),
                        angle,
                        SDL_Color{230, 210, 130, 240}, // Metallic cable gold
                        nullptr,
                        glm::vec2(0.5f, 0.5f),
                        true
                    );
                }
            }
        );
    }
};

} // namespace Engine
