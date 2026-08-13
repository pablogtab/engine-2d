#include "SpriteSheet.hpp"
#include <algorithm>

namespace Engine {

SpriteSheet::SpriteSheet(Texture* texture, int columns, int rows)
    : m_texture(texture), m_columns(std::max(1, columns)), m_rows(std::max(1, rows)) {
    if (m_texture) {
        m_frame_width = static_cast<float>(m_texture->get_width()) / static_cast<float>(m_columns);
        m_frame_height = static_cast<float>(m_texture->get_height()) / static_cast<float>(m_rows);
    }
    m_total_frames = m_columns * m_rows;
}

SpriteSheet::SpriteSheet(Texture* texture, float frame_width, float frame_height)
    : m_texture(texture), m_frame_width(frame_width), m_frame_height(frame_height) {
    if (m_texture && frame_width > 0.0f && frame_height > 0.0f) {
        m_columns = static_cast<int>(static_cast<float>(m_texture->get_width()) / frame_width);
        m_rows = static_cast<int>(static_cast<float>(m_texture->get_height()) / frame_height);
    }
    m_columns = std::max(1, m_columns);
    m_rows = std::max(1, m_rows);
    m_total_frames = m_columns * m_rows;
}

SDL_FRect SpriteSheet::get_frame_rect(int index) const {
    if (index < 0 || index >= m_total_frames) {
        index = 0;
    }
    int col = index % m_columns;
    int row = index / m_columns;
    return get_frame_rect(col, row);
}

SDL_FRect SpriteSheet::get_frame_rect(int col, int row) const {
    col = std::clamp(col, 0, m_columns - 1);
    row = std::clamp(row, 0, m_rows - 1);

    return SDL_FRect{
        static_cast<float>(col) * m_frame_width,
        static_cast<float>(row) * m_frame_height,
        m_frame_width,
        m_frame_height
    };
}

} // namespace Engine
