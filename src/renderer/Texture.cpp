#include "stb/stb_image.h"
#include "Texture.hpp"
#include "../core/Log.hpp"
#include <vector>
#include <cmath>

namespace Engine {

Texture::Texture(SDL_Renderer* renderer, int width, int height, const uint32_t* pixels)
    : m_width(width), m_height(height) {

    m_texture = SDL_CreateTexture(
        renderer, 
        SDL_PIXELFORMAT_RGBA8888, 
        SDL_TEXTUREACCESS_STATIC, 
        width, 
        height
    );

    if (m_texture && pixels) {
        SDL_UpdateTexture(m_texture, nullptr, pixels, width * sizeof(uint32_t));
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND);
    }
}

Texture::~Texture() {
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
    }
}

Texture* Texture::load_from_file(SDL_Renderer* renderer, const std::string& filepath) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* data = stbi_load(filepath.c_str(), &w, &h, &channels, STBI_rgb_alpha);

    if (!data) {
        ENGINE_LOG_ERROR("Failed to load image asset '{}': {}", filepath, stbi_failure_reason());
        return nullptr;
    }

    Texture* tex = new Texture(renderer, w, h, reinterpret_cast<const uint32_t*>(data));
    stbi_image_free(data);

    ENGINE_LOG_INFO("Loaded texture asset: '{}' ({}x{} px)", filepath, w, h);
    return tex;
}

Texture* Texture::create_solid(SDL_Renderer* renderer, int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint32_t> pixels(width * height);
    uint32_t color = (r << 24) | (g << 16) | (b << 8) | a;
    for (auto& p : pixels) {
        p = color;
    }
    return new Texture(renderer, width, height, pixels.data());
}

Texture* Texture::create_grid(SDL_Renderer* renderer, int size, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint32_t> pixels(size * size);
    uint32_t main_color = (r << 24) | (g << 16) | (b << 8) | 255;
    uint32_t border_color = ((r/2) << 24) | ((g/2) << 16) | ((b/2) << 8) | 255;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (x == 0 || y == 0 || x == size - 1 || y == size - 1) {
                pixels[y * size + x] = border_color;
            } else {
                pixels[y * size + x] = main_color;
            }
        }
    }
    return new Texture(renderer, size, size, pixels.data());
}

Texture* Texture::create_circle(SDL_Renderer* renderer, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int diameter = radius * 2;
    std::vector<uint32_t> pixels(diameter * diameter, 0);

    for (int y = 0; y < diameter; ++y) {
        for (int x = 0; x < diameter; ++x) {
            float dx = x - radius + 0.5f;
            float dy = y - radius + 0.5f;
            float dist_sq = dx * dx + dy * dy;
            if (dist_sq <= radius * radius) {
                // Subtle 1.5px sub-pixel anti-aliasing feathering
                float alpha_factor = 1.0f;
                float dist = std::sqrt(dist_sq);
                float edge_threshold = static_cast<float>(radius) - 1.5f;
                if (dist > edge_threshold) {
                    alpha_factor = (static_cast<float>(radius) - dist) / 1.5f;
                    if (alpha_factor < 0.0f) alpha_factor = 0.0f;
                }
                uint8_t final_a = static_cast<uint8_t>(a * alpha_factor);
                pixels[y * diameter + x] = (r << 24) | (g << 16) | (b << 8) | final_a;
            }
        }
    }
    return new Texture(renderer, diameter, diameter, pixels.data());
}

} // namespace Engine
