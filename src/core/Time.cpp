#include "Time.hpp"
#include <algorithm>

namespace Engine {

void Time::update(float raw_delta_time) {
    if (s_suspended) {
        reset_frame_delta();
        return;
    }
    // Delta time clamping to prevent physics explosion / tunneling on lag spikes
    s_real_delta_time = std::clamp(raw_delta_time, 0.0001f, 0.1f);
    s_delta_time = s_real_delta_time * s_time_scale;

    s_real_time += s_real_delta_time;
    s_time += s_delta_time;
    s_frame_count++;
}

void Time::suspend() {
    s_suspended = true;
    reset_frame_delta();
}

void Time::resume() {
    s_suspended = false;
    reset_frame_delta();
}

void Time::reset_frame_delta() {
    s_delta_time = 0.0f;
    s_real_delta_time = 0.0f;
}

void Time::reset() {
    s_delta_time = 0.0f;
    s_real_delta_time = 0.0f;
    s_time = 0.0f;
    s_real_time = 0.0f;
    s_time_scale = 1.0f;
    s_frame_count = 0;
    s_suspended = false;
}

} // namespace Engine
