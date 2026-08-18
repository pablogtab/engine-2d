#include "renderer/postprocess/IPostProcessPass.hpp"
#include "renderer/postprocess/PostProcessPipeline.hpp"
#include "renderer/postprocess/PostProcessProfile.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/PostProcessProfileCommands.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool nearly_equal(float a, float b, float epsilon = 0.0001f) {
    return std::abs(a - b) <= epsilon;
}

void test_effect_scale_matches_authoring_resolution() {
    Engine::PostProcessContext native{1280, 720, 1280, 720};
    assert(nearly_equal(native.horizontal_scale(), 1.0f));
    assert(nearly_equal(native.vertical_scale(), 1.0f));
    assert(nearly_equal(native.pixel_scale(), 1.0f));

    Engine::PostProcessContext full_hd{1920, 1080, 1280, 720};
    assert(nearly_equal(full_hd.horizontal_scale(), 1.5f));
    assert(nearly_equal(full_hd.vertical_scale(), 1.5f));
    assert(nearly_equal(full_hd.scale_pixels(4.0f), 6.0f));

    Engine::PostProcessContext half{640, 360, 1280, 720};
    assert(nearly_equal(half.pixel_scale(), 0.5f));
    assert(nearly_equal(half.scale_pixels(4.0f), 2.0f));
}

void test_non_uniform_output_uses_area_preserving_pixel_scale() {
    Engine::PostProcessContext context{1920, 720, 1280, 720};
    assert(nearly_equal(context.horizontal_scale(), 1.5f));
    assert(nearly_equal(context.vertical_scale(), 1.0f));
    assert(nearly_equal(context.pixel_scale(), std::sqrt(1.5f)));
}

void test_profile_round_trip_preserves_effect_reference_and_parameters() {
    Engine::PostProcessProfile source = Engine::PostProcessProfile::PresetRetroCRT();
    source.effect_reference_width = 1600;
    source.effect_reference_height = 900;
    source.blur_enabled = true;
    source.blur_radius = 2.25f;
    source.shadow_tint = SDL_Color{12, 34, 56, 78};

    Engine::PostProcessProfile restored;
    assert(restored.from_json(source.to_json()));
    assert(restored.name == source.name);
    assert(restored.effect_reference_width == 1600);
    assert(restored.effect_reference_height == 900);
    assert(restored.blur_enabled);
    assert(nearly_equal(restored.blur_radius, 2.25f));
    assert(restored.shadow_tint.r == 12);
    assert(restored.shadow_tint.g == 34);
    assert(restored.shadow_tint.b == 56);
    assert(restored.shadow_tint.a == 78);
    assert(restored.crt_enabled == source.crt_enabled);
    assert(nearly_equal(restored.crt_scanline_spacing, source.crt_scanline_spacing));
}

void test_profile_updates_pipeline_reference_without_renderer() {
    Engine::PostProcessPipeline pipeline;
    pipeline.init(nullptr, 1920, 1080);

    Engine::PostProcessProfile profile;
    profile.effect_reference_width = 960;
    profile.effect_reference_height = 540;
    profile.apply_to_pipeline(pipeline);

    const auto& context = pipeline.get_context();
    assert(context.output_width == 1920);
    assert(context.output_height == 1080);
    assert(context.authoring_width == 960);
    assert(context.authoring_height == 540);
    assert(nearly_equal(context.pixel_scale(), 2.0f));
}

void test_profile_edit_is_undoable() {
    Engine::PostProcessPipeline pipeline;
    pipeline.init(nullptr, 1920, 1080);
    Engine::PostProcessProfile profile = Engine::PostProcessProfile::PresetDefault();
    Engine::PostProcessProfile changed = profile;
    changed.bloom_intensity = 1.25f;
    changed.effect_reference_width = 1920;
    changed.effect_reference_height = 1080;

    Engine::EditorCommandHistory history;
    history.execute_command(std::make_unique<Engine::ModifyPostProcessProfileCommand>(
        &profile, &pipeline, profile, changed
    ));
    assert(nearly_equal(profile.bloom_intensity, 1.25f));
    assert(pipeline.get_context().authoring_width == 1920);

    history.undo();
    assert(nearly_equal(profile.bloom_intensity, Engine::PostProcessProfile::PresetDefault().bloom_intensity));
    assert(pipeline.get_context().authoring_width == 1280);

    history.redo();
    assert(nearly_equal(profile.bloom_intensity, 1.25f));
    assert(pipeline.get_context().authoring_width == 1920);
}

} // namespace

int main() {
    test_effect_scale_matches_authoring_resolution();
    test_non_uniform_output_uses_area_preserving_pixel_scale();
    test_profile_round_trip_preserves_effect_reference_and_parameters();
    test_profile_updates_pipeline_reference_without_renderer();
    test_profile_edit_is_undoable();
    std::cout << "PostProcessTests passed\n";
    return 0;
}
