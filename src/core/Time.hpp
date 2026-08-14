#pragma once

#include <cstdint>

namespace Engine {

class Time {
public:
    // Returns scaled delta time (affected by time_scale). Use for physics, gameplay & movement simulations.
    static float get_delta_time() { return s_delta_time; }

    // Returns unscaled real frame delta time (independent of time_scale). Use for UI animations and profilers.
    static float get_real_delta_time() { return s_real_delta_time; }

    // Returns total scaled game time elapsed in seconds since application start.
    static float get_time() { return s_time; }

    // Returns total unscaled real time elapsed in seconds since application start.
    static float get_real_time() { return s_real_time; }

    // Returns the total number of frames elapsed.
    static uint64_t get_frame_count() { return s_frame_count; }

    // Sets the global time scale factor (1.0 = normal, 0.0 = pause, 0.25 = slow motion, 2.0 = fast forward).
    static void set_time_scale(float scale) { s_time_scale = (scale < 0.0f) ? 0.0f : scale; }

    // Returns the current time scale factor.
    static float get_time_scale() { return s_time_scale; }

    // Internal update called by Engine::Application at the start of each frame loop.
    static void update(float raw_delta_time);

private:
    inline static float s_delta_time{0.016f};
    inline static float s_real_delta_time{0.016f};
    inline static float s_time{0.0f};
    inline static float s_real_time{0.0f};
    inline static float s_time_scale{1.0f};
    inline static uint64_t s_frame_count{0};
};

} // namespace Engine
