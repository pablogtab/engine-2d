#include "BakedLighting2DSystem.hpp"

#include "RenderTargetPool.hpp"
#include "Renderer2D.hpp"
#include "../scene/Camera2D.hpp"
#include <SDL3/SDL.h>
#include <array>

namespace Engine {

bool BakedLighting2DPass::load(const LightingBakeManifest& manifest, SDL_Renderer* renderer) {
    if (!renderer) return false;
    std::vector<RuntimeTile> loaded;
    loaded.reserve(manifest.tiles.size());
    RenderLayerMask layers = 0;
    for (const LightingBakeTile& tile : manifest.tiles) {
        std::unique_ptr<Texture> texture(Texture::load_from_file(renderer, tile.image_path.string()));
        if (!texture || !texture->get_sdl_texture()) return false;
        layers |= tile.affected_layers;
        loaded.push_back({std::move(texture), tile});
    }
    m_tiles = std::move(loaded);
    m_affected_layers = layers;
    m_loaded_hash = manifest.status.input_hash;
    m_using_stale_bake = !manifest.status.current;
    return true;
}

void BakedLighting2DPass::clear() {
    m_tiles.clear();
    m_affected_layers = 0;
    m_loaded_hash.clear();
    m_using_stale_bake = false;
}

bool BakedLighting2DPass::apply(const LightingContext& context) {
    if (!context.renderer || !context.scene_target || !context.render_targets || !context.camera ||
        !context.baked_receiver_mask || !renders_layer(m_affected_layers, context.render_layer)) return false;
    SDL_Texture* factor = context.render_targets->acquire(context.width, context.height);
    SDL_Texture* inverse_mask = context.render_targets->acquire(context.width, context.height);
    if (!factor || !inverse_mask) {
        if (factor) context.render_targets->release(factor);
        if (inverse_mask) context.render_targets->release(inverse_mask);
        return false;
    }
    struct ReturnTarget {
        RenderTargetPool* pool;
        SDL_Texture* texture;
        ~ReturnTarget() { if (pool && texture) pool->release(texture); }
    } returned_factor{context.render_targets, factor},
      returned_inverse{context.render_targets, inverse_mask};

    SDL_Texture* previous_target = SDL_GetRenderTarget(context.renderer);
    SDL_BlendMode previous_blend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(context.renderer, &previous_blend);
    std::uint8_t previous_r = 0, previous_g = 0, previous_b = 0, previous_a = 0;
    SDL_GetRenderDrawColor(context.renderer, &previous_r, &previous_g, &previous_b, &previous_a);

    SDL_SetRenderTarget(context.renderer, factor);
    SDL_SetRenderDrawBlendMode(context.renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(context.renderer, 255, 255, 255, 255);
    SDL_RenderClear(context.renderer);
    for (const RuntimeTile& tile : m_tiles) {
        if (!renders_layer(tile.metadata.affected_layers, context.render_layer)) continue;
        const float world_width = static_cast<float>(tile.metadata.pixel_width) / tile.metadata.texels_per_unit;
        const float world_height = static_cast<float>(tile.metadata.pixel_height) / tile.metadata.texels_per_unit;
        const std::array<glm::vec2, 4> world{{
            {tile.metadata.origin_x, tile.metadata.origin_y + world_height},
            {tile.metadata.origin_x + world_width, tile.metadata.origin_y + world_height},
            {tile.metadata.origin_x + world_width, tile.metadata.origin_y},
            {tile.metadata.origin_x, tile.metadata.origin_y}
        }};
        std::array<SDL_Vertex, 4> vertices{};
        constexpr std::array<SDL_FPoint, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        for (std::size_t index = 0; index < vertices.size(); ++index) {
            const glm::vec2 screen = context.camera->world_to_screen(glm::vec3(world[index], 0.0f)) * context.raster_scale;
            vertices[index] = {{screen.x, screen.y}, {1, 1, 1, 1}, uv[index]};
        }
        constexpr std::array<int, 6> indices{0, 1, 2, 0, 2, 3};
        SDL_Texture* source = tile.texture->get_sdl_texture();
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_NONE);
        SDL_RenderGeometry(context.renderer, source, vertices.data(), 4, indices.data(), 6);
    }

    // Convert receiver alpha to RGB coverage independently of sprite albedo.
    SDL_SetRenderTarget(context.renderer, inverse_mask);
    SDL_SetRenderDrawBlendMode(context.renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(context.renderer, 0, 0, 0, 0);
    SDL_RenderClear(context.renderer);
    SDL_SetTextureColorMod(context.baked_receiver_mask, 255, 255, 255);
    SDL_SetTextureBlendMode(context.baked_receiver_mask, SDL_BLENDMODE_BLEND);
    SDL_RenderTexture(context.renderer, context.baked_receiver_mask, nullptr, nullptr);

    // Keep only lightmap values covered by explicit baked receivers.
    SDL_SetRenderTarget(context.renderer, factor);
    SDL_SetTextureBlendMode(inverse_mask, SDL_BLENDMODE_MOD);
    SDL_RenderTexture(context.renderer, inverse_mask, nullptr, nullptr);

    // Build RGB = 1 - receiver coverage using only standard SDL blend modes.
    SDL_SetRenderTarget(context.renderer, inverse_mask);
    SDL_SetRenderDrawBlendMode(context.renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(context.renderer, 255, 255, 255, 255);
    SDL_RenderClear(context.renderer);
    const std::array<SDL_Vertex, 4> inverse_vertices{{
        {{0.0f, 0.0f}, {0, 0, 0, 1}, {0, 0}},
        {{static_cast<float>(context.width), 0.0f}, {0, 0, 0, 1}, {1, 0}},
        {{static_cast<float>(context.width), static_cast<float>(context.height)}, {0, 0, 0, 1}, {1, 1}},
        {{0.0f, static_cast<float>(context.height)}, {0, 0, 0, 1}, {0, 1}}
    }};
    constexpr std::array<int, 6> inverse_indices{0, 1, 2, 0, 2, 3};
    SDL_SetTextureColorMod(context.baked_receiver_mask, 255, 255, 255);
    SDL_SetTextureBlendMode(context.baked_receiver_mask, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(
        context.renderer, context.baked_receiver_mask,
        inverse_vertices.data(), static_cast<int>(inverse_vertices.size()),
        inverse_indices.data(), static_cast<int>(inverse_indices.size())
    );

    // Select the baked factor only at receiver pixels:
    // factor = lightmap * coverage + (1 - coverage).
    SDL_SetRenderTarget(context.renderer, factor);
    SDL_SetTextureBlendMode(inverse_mask, SDL_BLENDMODE_ADD);
    SDL_RenderTexture(context.renderer, inverse_mask, nullptr, nullptr);
    SDL_SetRenderTarget(context.renderer, context.scene_target);
    SDL_SetTextureBlendMode(factor, SDL_BLENDMODE_MOD);
    SDL_RenderTexture(context.renderer, factor, nullptr, nullptr);
    SDL_SetTextureBlendMode(context.baked_receiver_mask, SDL_BLENDMODE_BLEND);

    SDL_SetRenderTarget(context.renderer, previous_target);
    SDL_SetRenderDrawBlendMode(context.renderer, previous_blend);
    SDL_SetRenderDrawColor(context.renderer, previous_r, previous_g, previous_b, previous_a);
    return true;
}

BakedLighting2DSystem::BakedLighting2DSystem()
    : m_runtime_pass(std::make_shared<BakedLighting2DPass>()) {}

void BakedLighting2DSystem::configure(
    const std::filesystem::path& project_root,
    const std::filesystem::path& scene_path,
    Renderer2D& renderer
) {
    if (scene_path.empty()) {
        m_runtime_pass->clear();
        m_scene_path.clear();
        return;
    }
    const std::filesystem::path normalized = std::filesystem::absolute(scene_path).lexically_normal();
    if (normalized != m_scene_path) {
        m_runtime_pass->clear();
        m_scene_path = normalized;
        m_next_poll = {};
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= m_next_poll) {
        m_next_poll = now + std::chrono::milliseconds(500);
        if (const auto manifest = LightingBakeService::load_latest_valid(project_root, normalized);
            manifest && manifest->status.input_hash != m_runtime_pass->loaded_hash()) {
            static_cast<void>(m_runtime_pass->load(*manifest, renderer.get_sdl_renderer()));
        }
    }
    if (m_runtime_pass->is_enabled()) renderer.add_lighting_pass(m_runtime_pass);
}

} // namespace Engine
