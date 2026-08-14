#pragma once

#include "Texture.hpp"
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <unordered_map>
#include <SDL3/SDL.h>

namespace Engine {

struct Glyph {
    SDL_FRect src_rect{0.0f, 0.0f, 0.0f, 0.0f}; // Source coordinates in atlas texture
    glm::vec2 size{0.0f, 0.0f};                  // Rendered width and height
    glm::vec2 offset{0.0f, 0.0f};                // Bearing / baseline offset (xoff, yoff)
    float advance_x{0.0f};                       // Horizontal spacing to next character
};

class Font {
public:
    Font() = default;
    ~Font() = default;

    // Loads a TrueType (.ttf/.otf) font from disk and rasterizes an anti-aliased GPU atlas texture
    static std::unique_ptr<Font> load_from_file(
        SDL_Renderer* renderer, 
        const std::string& filepath, 
        float font_size = 28.0f, 
        int first_char = 32, 
        int char_count = 96
    );

    // Creates an embedded fallback font (attempts system TTF or procedural high-contrast bitmap font)
    static std::unique_ptr<Font> create_default(SDL_Renderer* renderer, float font_size = 24.0f);

    Texture* get_atlas_texture() const { return m_atlas_texture.get(); }
    float get_font_size() const { return m_font_size; }
    float get_line_height() const { return m_line_height; }

    const Glyph* get_glyph(char c) const {
        auto it = m_glyphs.find(c);
        if (it != m_glyphs.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // Measures total bounding box size of a string in pixels
    glm::vec2 measure_text(const std::string& text, float scale = 1.0f) const;

private:
    std::unique_ptr<Texture> m_atlas_texture;
    std::unordered_map<char, Glyph> m_glyphs;
    float m_font_size{28.0f};
    float m_line_height{32.0f};
};

} // namespace Engine
