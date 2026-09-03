#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <optional>

namespace Engine {

class PostProcessPipeline;
class AssetDatabase;
struct AssetHandle;

// ---------------------------------------------------------------------------
// PostProcessProfile
// ---------------------------------------------------------------------------
// Complete post-processing stack configuration. Supports continuous profile
// blending for volumes and JSON serialization for reusable presets.
// ---------------------------------------------------------------------------
struct PostProcessProfile {
    std::string name{"Custom"};
    int effect_reference_width{1280};
    int effect_reference_height{720};

    // Vignette
    bool vignette_enabled{true};
    float vignette_intensity{0.45f};
    SDL_Color vignette_color{0, 0, 0, 255};

    // Chromatic Aberration
    bool chromatic_enabled{true};
    float chromatic_offset{0.0f};

    // Bloom
    bool bloom_enabled{true};
    float bloom_intensity{0.35f};
    float bloom_radius{4.0f};

    // Gaussian Blur
    bool blur_enabled{false};
    float blur_radius{0.0f};

    // Color Grading
    bool color_grading_enabled{true};
    SDL_Color color_tint{255, 255, 255, 255};
    SDL_Color shadow_tint{0, 0, 0, 0};

    // Film Grain
    bool film_grain_enabled{false};
    float film_grain_intensity{0.0f};

    // Scanlines CRT
    bool crt_enabled{false};
    float crt_scanline_intensity{0.0f};
    float crt_scanline_spacing{4.0f};
    float crt_flicker{0.02f};

    [[nodiscard]] bool operator==(const PostProcessProfile& other) const;
    [[nodiscard]] bool operator!=(const PostProcessProfile& other) const {
        return !(*this == other);
    }
    [[nodiscard]] std::optional<std::string> validation_error() const;

    // Apply this profile to an active pipeline.
    void apply_to_pipeline(PostProcessPipeline& pipeline) const;

    // Blend smoothly between two profiles.
    static PostProcessProfile lerp(const PostProcessProfile& a, const PostProcessProfile& b, float t);

    // Built-in reference presets.
    static PostProcessProfile PresetDefault();
    static PostProcessProfile PresetCinematic();
    static PostProcessProfile PresetRetroCRT();
    static PostProcessProfile PresetUnderwater();
    static PostProcessProfile PresetNightVision();
    static PostProcessProfile PresetDreamyBloom();

    // JSON serialization.
    std::string to_json() const;
    bool from_json(const std::string& json_str);
    bool save_to_file(const std::string& filepath) const;
    bool load_from_file(const std::string& filepath);
    [[nodiscard]] bool save_asset(const std::string& filepath, AssetDatabase& database) const;
    [[nodiscard]] static std::optional<PostProcessProfile> load_asset(AssetHandle handle, const AssetDatabase& database);
};

} // namespace Engine
