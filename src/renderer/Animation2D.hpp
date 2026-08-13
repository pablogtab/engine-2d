#pragma once

#include "SpriteSheet.hpp"
#include <SDL3/SDL.h>

namespace Engine {

class Animation2D {
public:
    Animation2D() = default;
    Animation2D(SpriteSheet* spritesheet, int start_frame, int end_frame, float frame_rate = 10.0f, bool loop = true);

    void set_spritesheet(SpriteSheet* spritesheet) { m_spritesheet = spritesheet; }
    void set_clip(int start_frame, int end_frame, float frame_rate = 10.0f, bool loop = true);

    void update(float delta_time);

    void play() { m_playing = true; }
    void pause() { m_playing = false; }
    void stop() { m_playing = false; reset(); }
    void reset();

    SDL_FRect get_current_frame_rect() const;
    Texture* get_texture() const { return m_spritesheet ? m_spritesheet->get_texture() : nullptr; }
    
    int get_current_frame_index() const { return m_current_frame; }
    bool is_playing() const { return m_playing; }
    bool is_finished() const { return m_finished; }

private:
    SpriteSheet* m_spritesheet{nullptr};
    int m_start_frame{0};
    int m_end_frame{0};
    float m_frame_rate{10.0f}; // Frames per second
    bool m_loop{true};

    int m_current_frame{0};
    float m_elapsed_time{0.0f};
    bool m_playing{true};
    bool m_finished{false};
};

} // namespace Engine
