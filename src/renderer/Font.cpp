#include "Font.hpp"
#include "../core/Log.hpp"
#include <stb/stb_truetype.h>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>

namespace Engine {

std::unique_ptr<Font> Font::load_from_file(
    SDL_Renderer* renderer, 
    const std::string& filepath, 
    float font_size, 
    int first_char, 
    int char_count
) {
    if (!renderer) {
        ENGINE_LOG_ERROR("[FONT] Cannot load font: SDL_Renderer is null.");
        return nullptr;
    }

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        ENGINE_LOG_ERROR("[FONT] Failed to open font file: '{}'", filepath);
        return nullptr;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> ttf_buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(ttf_buffer.data()), file_size)) {
        ENGINE_LOG_ERROR("[FONT] Failed to read font file content: '{}'", filepath);
        return nullptr;
    }

    constexpr int ATLAS_W = 512;
    constexpr int ATLAS_H = 512;
    std::vector<unsigned char> mono_bitmap(ATLAS_W * ATLAS_H, 0);
    std::vector<stbtt_bakedchar> cdata(char_count);

    int bake_res = stbtt_BakeFontBitmap(
        ttf_buffer.data(), 0, font_size,
        mono_bitmap.data(), ATLAS_W, ATLAS_H,
        first_char, char_count, cdata.data()
    );

    if (bake_res <= 0) {
        ENGINE_LOG_WARN("[FONT] Font atlas full or partially clipped for '{}' with size {:.1f}px (Result: {})",
                        filepath, font_size, bake_res);
    }

    // Convert 1-channel alpha bitmap to 32-bit RGBA texture (Pure white with alpha falloff)
    std::vector<uint32_t> rgba_pixels(ATLAS_W * ATLAS_H);
    for (int i = 0; i < ATLAS_W * ATLAS_H; ++i) {
        uint8_t a = mono_bitmap[i];
        rgba_pixels[i] = (255 << 24) | (255 << 16) | (255 << 8) | a;
    }

    auto font = std::make_unique<Font>();
    font->m_atlas_texture = std::make_unique<Texture>(renderer, ATLAS_W, ATLAS_H, rgba_pixels.data());
    font->m_font_size = font_size;
    font->m_line_height = font_size * 1.25f;

    for (int i = 0; i < char_count; ++i) {
        char c = static_cast<char>(first_char + i);
        const auto& bc = cdata[i];

        Glyph glyph;
        glyph.src_rect = SDL_FRect{
            static_cast<float>(bc.x0),
            static_cast<float>(bc.y0),
            static_cast<float>(bc.x1 - bc.x0),
            static_cast<float>(bc.y1 - bc.y0)
        };
        glyph.size = glm::vec2(glyph.src_rect.w, glyph.src_rect.h);
        glyph.offset = glm::vec2(bc.xoff, bc.yoff);
        glyph.advance_x = bc.xadvance;

        font->m_glyphs[c] = glyph;
    }

    ENGINE_LOG_INFO("[FONT] Loaded TrueType font: '{}' (Size: {:.1f}px, Glyphs: {})", filepath, font_size, char_count);
    return font;
}

std::unique_ptr<Font> Font::create_default(SDL_Renderer* renderer, float font_size) {
    // List of standard system TTF fonts across macOS, Windows and Linux
    static const std::vector<std::string> s_system_fonts = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Geneva.ttf",
        "/System/Library/Fonts/Monaco.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf"
    };

    for (const auto& path : s_system_fonts) {
        std::ifstream test(path);
        if (test.good()) {
            auto font = load_from_file(renderer, path, font_size);
            if (font) return font;
        }
    }

    ENGINE_LOG_WARN("[FONT] No standard system TTF font found on system.");
    return nullptr;
}

glm::vec2 Font::measure_text(const std::string& text, float scale) const {
    if (text.empty()) return glm::vec2(0.0f);

    float cur_x = 0.0f;
    float max_x = 0.0f;
    float max_y = m_line_height * scale;

    for (char c : text) {
        if (c == '\n') {
            max_x = std::max(max_x, cur_x);
            cur_x = 0.0f;
            max_y += m_line_height * scale;
            continue;
        }

        const Glyph* g = get_glyph(c);
        if (g) {
            cur_x += g->advance_x * scale;
        } else {
            cur_x += (m_font_size * 0.5f) * scale; // Default whitespace advance
        }
    }

    max_x = std::max(max_x, cur_x);
    return glm::vec2(max_x, max_y);
}

} // namespace Engine
