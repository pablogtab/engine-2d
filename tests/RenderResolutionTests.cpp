#include "renderer/RenderResolution.hpp"

#include <cassert>
#include <cmath>

namespace {

bool near(float lhs, float rhs, float epsilon = 0.001f) {
    return std::abs(lhs - rhs) <= epsilon;
}

void test_settings_are_normalized() {
    Engine::RenderResolutionSettings settings;
    settings.fixed_scale = 0.1f;
    settings.dynamic_min_scale = 0.9f;
    settings.dynamic_max_scale = 0.6f;
    settings.dynamic_target_fps = 500.0f;
    const auto normalized = Engine::normalize_render_resolution_settings(settings);
    assert(near(normalized.fixed_scale, 0.5f));
    assert(near(normalized.dynamic_min_scale, 0.6f));
    assert(near(normalized.dynamic_max_scale, 0.9f));
    assert(near(normalized.dynamic_target_fps, 240.0f));
}

void test_fixed_scale_is_stable() {
    Engine::DynamicResolutionController controller;
    Engine::RenderResolutionSettings settings;
    settings.mode = Engine::RenderScaleMode::Fixed;
    settings.fixed_scale = 0.75f;
    controller.configure(settings);
    assert(near(controller.update(1.0f / 20.0f), 0.75f));
    assert(near(controller.update(1.0f / 120.0f), 0.75f));
}

void test_dynamic_scale_tracks_sustained_load_with_bounds() {
    Engine::DynamicResolutionController controller;
    Engine::RenderResolutionSettings settings;
    settings.mode = Engine::RenderScaleMode::Dynamic;
    settings.dynamic_min_scale = 0.6f;
    settings.dynamic_max_scale = 0.9f;
    settings.dynamic_target_fps = 60.0f;
    controller.configure(settings);
    assert(near(controller.current_scale(), 0.9f));

    for (int frame = 0; frame < 240; ++frame) controller.update(1.0f / 30.0f);
    assert(near(controller.current_scale(), 0.6f));

    for (int frame = 0; frame < 900; ++frame) controller.update(1.0f / 120.0f);
    assert(near(controller.current_scale(), 0.9f));
}

void test_debugger_hitch_is_ignored() {
    Engine::DynamicResolutionController controller;
    Engine::RenderResolutionSettings settings;
    settings.mode = Engine::RenderScaleMode::Dynamic;
    settings.dynamic_min_scale = 0.5f;
    settings.dynamic_max_scale = 1.0f;
    controller.configure(settings);
    controller.update(1.0f);
    assert(near(controller.current_scale(), 1.0f));
}

void test_target_hysteresis_avoids_near_target_oscillation() {
    Engine::DynamicResolutionController controller;
    Engine::RenderResolutionSettings settings;
    settings.mode = Engine::RenderScaleMode::Dynamic;
    settings.dynamic_min_scale = 0.5f;
    settings.dynamic_max_scale = 0.8f;
    settings.dynamic_target_fps = 60.0f;
    controller.configure(settings);
    for (int frame = 0; frame < 600; ++frame) controller.update(1.0f / 58.0f);
    assert(near(controller.current_scale(), 0.8f));
}

} // namespace

int main() {
    test_settings_are_normalized();
    test_fixed_scale_is_stable();
    test_dynamic_scale_tracks_sustained_load_with_bounds();
    test_debugger_hitch_is_ignored();
    test_target_hysteresis_avoids_near_target_oscillation();
    return 0;
}
