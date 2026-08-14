#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace Engine {

class Texture {
public:
    Texture(SDL_Renderer* renderer, int width, int height, const uint32_t* pixels);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    SDL_Texture* get_sdl_texture() const { return m_texture; }
    int get_width() const { return m_width; }
    int get_height() const { return m_height; }

    // File loader (PNG, JPG, BMP, TGA)
    static Texture* load_from_file(SDL_Renderer* renderer, const std::string& filepath);

    // Procedural generators for instant demos without external image dependencies
    static Texture* create_solid(SDL_Renderer* renderer, int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    static Texture* create_grid(SDL_Renderer* renderer, int size, uint8_t r, uint8_t g, uint8_t b);
    static Texture* create_circle(SDL_Renderer* renderer, int radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    static Texture* create_soft_circle(SDL_Renderer* renderer, int radius, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
    static Texture* create_spark(SDL_Renderer* renderer, int radius, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);

private:
    SDL_Texture* m_texture{nullptr};
    int m_width{0};
    int m_height{0};
};

} // namespace Engine
