#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <functional>

namespace Engine {

enum class ProceduralType {
    NoiseGradient,   // Fractal gradient noise (organic terrain, clouds, surfaces)
    Voronoi,         // Cellular / Dirichlet polygons (cobblestones, scales, stylized tiles)
    LinearGradient,  // Smooth directional color ramp (vertical / horizontal)
    RadialGradient,  // Radial color ramp from center out
    Checkerboard,    // Alternating grid tiles
    Custom           // User-supplied (x, y) normalized mapping function
};

struct ProceduralTextureDesc {
    ProceduralType type{ProceduralType::NoiseGradient};
    int width{128};
    int height{128};
    SDL_Color color_a{255, 255, 255, 255};
    SDL_Color color_b{0, 0, 0, 255};
    float scale{0.04f};          // Spatial frequency / zoom factor
    int octaves{3};              // Fractal detail layers (for noise)
    float persistence{0.5f};     // Multiplier for octave amplitude
    int seed{1337};              // Random seed for reproducibility
    int cell_count{16};          // Point count for Voronoi cells
    std::function<float(float u, float v)> custom_generator{nullptr};
};

class Texture {
public:
    Texture(SDL_Renderer* renderer, int width, int height, const uint32_t* pixels);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    SDL_Texture* get_sdl_texture() const { return m_texture; }
    int get_width() const { return m_width; }
    int get_height() const { return m_height; }
    [[nodiscard]] std::uint8_t alpha_at(int x, int y) const;
    [[nodiscard]] std::span<const std::uint32_t> pixels() const { return m_pixels; }
    const std::string& get_filepath() const { return m_filepath; }
    void set_filepath(const std::string& path) { m_filepath = path; }

    // File loader (PNG, JPG, BMP, TGA)
    static Texture* load_from_file(SDL_Renderer* renderer, const std::string& filepath);

    // Content-Agnostic Procedural Generators
    static Texture* create_procedural(SDL_Renderer* renderer, const ProceduralTextureDesc& desc);

    // Procedural helpers
    static Texture* get_white(SDL_Renderer* renderer);
    static Texture* create_solid(SDL_Renderer* renderer, int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    static Texture* create_grid(SDL_Renderer* renderer, int size, uint8_t r, uint8_t g, uint8_t b);
    static Texture* create_circle(SDL_Renderer* renderer, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    static Texture* create_soft_circle(SDL_Renderer* renderer, int radius, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
    static Texture* create_spark(SDL_Renderer* renderer, int radius, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);

private:
    SDL_Texture* m_texture{nullptr};
    int m_width{0};
    int m_height{0};
    std::string m_filepath{""};
    std::vector<std::uint8_t> m_alpha;
    std::vector<std::uint32_t> m_pixels;
};

} // namespace Engine
