#include "Animation2D.hpp"

namespace Engine {

Animation2D::Animation2D(SpriteSheet* spritesheet, int start_frame, int end_frame, float frame_rate, bool loop)
    : m_spritesheet(spritesheet), m_start_frame(start_frame), m_end_frame(end_frame),
      m_frame_rate(frame_rate), m_loop(loop), m_current_frame(start_frame) {}

void Animation2D::set_clip(int start_frame, int end_frame, float frame_rate, bool loop) {
    m_start_frame = start_frame;
    m_end_frame = end_frame;
    m_frame_rate = frame_rate;
    m_loop = loop;
    reset();
}

void Animation2D::reset() {
    m_current_frame = m_start_frame;
    m_elapsed_time = 0.0f;
    m_finished = false;
    m_playing = true;
}

void Animation2D::update(float delta_time) {
    if (!m_playing || !m_spritesheet || m_frame_rate <= 0.0f) return;

    m_elapsed_time += delta_time;
    float time_per_frame = 1.0f / m_frame_rate;

    while (m_elapsed_time >= time_per_frame) {
        m_elapsed_time -= time_per_frame;

        if (m_start_frame <= m_end_frame) {
            m_current_frame++;
            if (m_current_frame > m_end_frame) {
                if (m_loop) {
                    m_current_frame = m_start_frame;
                } else {
                    m_current_frame = m_end_frame;
                    m_finished = true;
                    m_playing = false;
                    break;
                }
            }
        } else {
            // Reverse playback
            m_current_frame--;
            if (m_current_frame < m_end_frame) {
                if (m_loop) {
                    m_current_frame = m_start_frame;
                } else {
                    m_current_frame = m_end_frame;
                    m_finished = true;
                    m_playing = false;
                    break;
                }
            }
        }
    }
}

SDL_FRect Animation2D::get_current_frame_rect() const {
    if (m_spritesheet) {
        return m_spritesheet->get_frame_rect(m_current_frame);
    }
    return SDL_FRect{0.0f, 0.0f, 0.0f, 0.0f};
}

} // namespace Engine
