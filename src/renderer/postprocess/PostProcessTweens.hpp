#pragma once

#include "PostProcessPipeline.hpp"
#include "PostProcessPasses.hpp"

namespace Engine {

class PostProcessTweens {
public:
    static void trigger_flash(
        PostProcessPipeline& pipeline, 
        SDL_Color color = SDL_Color{255, 255, 255, 200}, 
        float duration = 0.25f, 
        EaseType ease = EaseType::OutQuad
    ) {
        if (auto flash = pipeline.get_pass<ScreenFlashPass>("ScreenFlash")) {
            flash->trigger_flash(color, duration, ease);
        }
    }

    static void trigger_chromatic_aberration(
        PostProcessPipeline& pipeline, 
        float offset_pixels = 5.0f, 
        float duration = 0.30f, 
        EaseType ease = EaseType::OutQuad
    ) {
        if (auto chr = pipeline.get_pass<ChromaticAberrationPass>("ChromaticAberration")) {
            chr->trigger(offset_pixels, duration, ease);
        }
    }

    static void trigger_bloom_pulse(
        PostProcessPipeline& pipeline, 
        float peak_intensity = 0.80f, 
        float duration = 0.40f
    ) {
        if (auto blm = pipeline.get_pass<BloomPass>("Bloom")) {
            blm->trigger_pulse(peak_intensity, duration);
        }
    }

    // Preset de impacto / detonación explosiva en 1 sola llamada
    static void trigger_explosion_fx(
        PostProcessPipeline& pipeline,
        float chroma_offset = 6.5f,
        float bloom_pulse = 0.9f,
        SDL_Color flash_col = SDL_Color{255, 240, 200, 130}
    ) {
        trigger_flash(pipeline, flash_col, 0.22f, EaseType::OutQuad);
        trigger_chromatic_aberration(pipeline, chroma_offset, 0.32f, EaseType::OutQuad);
        trigger_bloom_pulse(pipeline, bloom_pulse, 0.45f);
    }
};

} // namespace Engine
