#include "Time.hpp"
#include <algorithm>

namespace Engine {

void Time::update(float raw_delta_time) {
    // Delta time clamping to prevent physics explosion / tunneling on lag spikes
    s_real_delta_time = std::clamp(raw_delta_time, 0.0001f, 0.1f);
    s_delta_time = s_real_delta_time * s_time_scale;

    s_real_time += s_real_delta_time;
    s_time += s_delta_time;
    s_frame_count++;
}

} // namespace Engine
