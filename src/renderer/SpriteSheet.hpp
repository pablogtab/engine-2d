#pragma once

#include "Texture.hpp"
#include <SDL3/SDL.h>

namespace Engine {

class SpriteSheet {
public:
    SpriteSheet(Texture* texture, int columns, int rows);
    SpriteSheet(Texture* texture, float frame_width, float frame_height);

    SDL_FRect get_frame_rect(int index) const;
    SDL_FRect get_frame_rect(int col, int row) const;

    Texture* get_texture() const { return m_texture; }
    int get_columns() const { return m_columns; }
    int get_rows() const { return m_rows; }
    int get_total_frames() const { return m_total_frames; }
    float get_frame_width() const { return m_frame_width; }
    float get_frame_height() const { return m_frame_height; }

private:
    Texture* m_texture{nullptr};
    int m_columns{1};
    int m_rows{1};
    int m_total_frames{1};
    float m_frame_width{0.0f};
    float m_frame_height{0.0f};
};

} // namespace Engine
