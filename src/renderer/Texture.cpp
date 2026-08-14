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

    std::vector<uint32_t> pixels(w * h);
    for (int i = 0; i < w * h; ++i) {
        uint8_t r = data[i * 4 + 0];
        uint8_t g = data[i * 4 + 1];
        uint8_t b = data[i * 4 + 2];
        uint8_t a = data[i * 4 + 3];
        pixels[i] = (r << 24) | (g << 16) | (b << 8) | a;
    }

    Texture* tex = new Texture(renderer, w, h, pixels.data());
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

Texture* Texture::create_soft_circle(SDL_Renderer* renderer, int radius, uint8_t r, uint8_t g, uint8_t b) {
    int diameter = radius * 2;
    std::vector<uint32_t> pixels(diameter * diameter, 0);
    float rad_f = static_cast<float>(radius);

    for (int y = 0; y < diameter; ++y) {
        for (int x = 0; x < diameter; ++x) {
            float dx = (x - radius + 0.5f) / rad_f;
            float dy = (y - radius + 0.5f) / rad_f;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 1.0f) {
                float alpha_norm = std::pow(1.0f - dist, 1.4f);
                uint8_t final_a = static_cast<uint8_t>(255.0f * alpha_norm);
                pixels[y * diameter + x] = (r << 24) | (g << 16) | (b << 8) | final_a;
            }
        }
    }
    return new Texture(renderer, diameter, diameter, pixels.data());
}

Texture* Texture::create_spark(SDL_Renderer* renderer, int radius, uint8_t r, uint8_t g, uint8_t b) {
    int diameter = radius * 2;
    std::vector<uint32_t> pixels(diameter * diameter, 0);
    float rad_f = static_cast<float>(radius);

    for (int y = 0; y < diameter; ++y) {
        for (int x = 0; x < diameter; ++x) {
            float nx = std::abs(x - radius + 0.5f) / rad_f;
            float ny = std::abs(y - radius + 0.5f) / rad_f;

            float cross_dist = nx + ny;
            float center_dist = std::sqrt(nx * nx + ny * ny);

            float intensity = 0.0f;
            if (cross_dist < 1.0f) {
                intensity = std::pow(1.0f - cross_dist, 2.0f);
            }
            if (center_dist < 0.4f) {
                intensity = std::max(intensity, (1.0f - center_dist / 0.4f));
            }

            if (intensity > 0.005f) {
                uint8_t final_a = static_cast<uint8_t>(std::min(255.0f, 255.0f * intensity));
                pixels[y * diameter + x] = (r << 24) | (g << 16) | (b << 8) | final_a;
            }
        }
    }
    return new Texture(renderer, diameter, diameter, pixels.data());
}

// ---------------------------------------------------------------------------
// Pure Mathematical Generators (Content-Agnostic)
// ---------------------------------------------------------------------------
namespace {

static inline float hash2d(int x, int y, int seed) {
    int n = x + y * 57 + seed * 131;
    n = (n << 13) ^ n;
    int nn = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
    return 1.0f - (static_cast<float>(nn) / 1073741824.0f);
}

static inline float smooth_noise2d(float x, float y, int seed) {
    int ix = static_cast<int>(std::floor(x));
    int iy = static_cast<int>(std::floor(y));
    float fx = x - static_cast<float>(ix);
    float fy = y - static_cast<float>(iy);

    // Quintic C2 smooth curve
    float u = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    float v = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);

    float n00 = hash2d(ix, iy, seed);
    float n10 = hash2d(ix + 1, iy, seed);
    float n01 = hash2d(ix, iy + 1, seed);
    float n11 = hash2d(ix + 1, iy + 1, seed);

    float nx0 = std::lerp(n00, n10, u);
    float nx1 = std::lerp(n01, n11, u);
    return std::lerp(nx0, nx1, v);
}

static inline float fbm_noise2d(float x, float y, int octaves, float persistence, int seed) {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float max_val = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        total += (smooth_noise2d(x * frequency, y * frequency, seed + i * 31) * 0.5f + 0.5f) * amplitude;
        max_val += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }
    return max_val > 0.0f ? (total / max_val) : 0.0f;
}

static inline float voronoi2d(float x, float y, float scale, int seed) {
    float gx = x * scale;
    float gy = y * scale;
    int ix = static_cast<int>(std::floor(gx));
    int iy = static_cast<int>(std::floor(gy));

    float min_dist = 10.0f;

    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            int cell_x = ix + dx;
            int cell_y = iy + dy;

            float px = (hash2d(cell_x, cell_y, seed) * 0.5f + 0.5f);
            float py = (hash2d(cell_x, cell_y, seed + 101) * 0.5f + 0.5f);

            float vx = static_cast<float>(cell_x) + px - gx;
            float vy = static_cast<float>(cell_y) + py - gy;
            float dist = std::sqrt(vx * vx + vy * vy);
            if (dist < min_dist) {
                min_dist = dist;
            }
        }
    }
    return std::clamp(min_dist, 0.0f, 1.0f);
}

static inline uint32_t sample_color_ramp(const SDL_Color& a, const SDL_Color& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    uint8_t red   = static_cast<uint8_t>(std::lerp(static_cast<float>(a.r), static_cast<float>(b.r), t));
    uint8_t green = static_cast<uint8_t>(std::lerp(static_cast<float>(a.g), static_cast<float>(b.g), t));
    uint8_t blue  = static_cast<uint8_t>(std::lerp(static_cast<float>(a.b), static_cast<float>(b.b), t));
    uint8_t alpha = static_cast<uint8_t>(std::lerp(static_cast<float>(a.a), static_cast<float>(b.a), t));
    return (red << 24) | (green << 16) | (blue << 8) | alpha;
}

} // namespace

Texture* Texture::create_procedural(SDL_Renderer* renderer, const ProceduralTextureDesc& desc) {
    int w = std::max(2, desc.width);
    int h = std::max(2, desc.height);
    std::vector<uint32_t> pixels(w * h, 0);

    float center_x = static_cast<float>(w) * 0.5f;
    float center_y = static_cast<float>(h) * 0.5f;
    float max_radius = std::sqrt(center_x * center_x + center_y * center_y);

    for (int y = 0; y < h; ++y) {
        float norm_v = static_cast<float>(y) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            float norm_u = static_cast<float>(x) / static_cast<float>(w);
            float sample_val = 0.0f;

            switch (desc.type) {
                case ProceduralType::NoiseGradient: {
                    float sample_x = static_cast<float>(x) * desc.scale;
                    float sample_y = static_cast<float>(y) * desc.scale;
                    sample_val = fbm_noise2d(sample_x, sample_y, desc.octaves, desc.persistence, desc.seed);
                    break;
                }
                case ProceduralType::Voronoi: {
                    sample_val = voronoi2d(static_cast<float>(x), static_cast<float>(y), desc.scale, desc.seed);
                    break;
                }
                case ProceduralType::LinearGradient: {
                    sample_val = norm_v; // Vertical linear gradient
                    break;
                }
                case ProceduralType::RadialGradient: {
                    float dx = static_cast<float>(x) - center_x;
                    float dy = static_cast<float>(y) - center_y;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    sample_val = std::clamp(dist / max_radius, 0.0f, 1.0f);
                    break;
                }
                case ProceduralType::Checkerboard: {
                    int check_x = static_cast<int>(norm_u / std::max(0.001f, desc.scale));
                    int check_y = static_cast<int>(norm_v / std::max(0.001f, desc.scale));
                    sample_val = ((check_x + check_y) % 2 == 0) ? 0.0f : 1.0f;
                    break;
                }
                case ProceduralType::Custom: {
                    if (desc.custom_generator) {
                        sample_val = std::clamp(desc.custom_generator(norm_u, norm_v), 0.0f, 1.0f);
                    }
                    break;
                }
            }

            pixels[y * w + x] = sample_color_ramp(desc.color_a, desc.color_b, sample_val);
        }
    }

    return new Texture(renderer, w, h, pixels.data());
}

} // namespace Engine
