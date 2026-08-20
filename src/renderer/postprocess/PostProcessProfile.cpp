#include "PostProcessProfile.hpp"
#include "PostProcessPipeline.hpp"
#include "PostProcessPasses.hpp"
#include "../../core/Log.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include <fstream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace Engine {

using json = nlohmann::json;

namespace {

json color_to_json(SDL_Color color) {
    return json::array({color.r, color.g, color.b, color.a});
}

SDL_Color color_from_json(const json& value) {
    if (!value.is_array() || value.size() != 4) {
        throw json::type_error::create(302, "post-process color must contain four channels", &value);
    }
    return SDL_Color{
        value.at(0).get<uint8_t>(),
        value.at(1).get<uint8_t>(),
        value.at(2).get<uint8_t>(),
        value.at(3).get<uint8_t>()
    };
}

} // namespace

static inline float lerp_f(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline uint8_t lerp_u8(uint8_t a, uint8_t b, float t) {
    return static_cast<uint8_t>(std::clamp(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t, 0.0f, 255.0f));
}

static inline SDL_Color lerp_col(SDL_Color a, SDL_Color b, float t) {
    return SDL_Color{
        lerp_u8(a.r, b.r, t),
        lerp_u8(a.g, b.g, t),
        lerp_u8(a.b, b.b, t),
        lerp_u8(a.a, b.a, t)
    };
}

void PostProcessProfile::apply_to_pipeline(PostProcessPipeline& pipeline) const {
    pipeline.set_effect_reference_size(effect_reference_width, effect_reference_height);

    // 1. Vignette
    if (auto vig = pipeline.get_pass<VignettePass>("Vignette")) {
        vig->set_enabled(vignette_enabled);
        vig->set_intensity(vignette_intensity);
        vig->set_color(vignette_color);
    }

    // 2. Chromatic Aberration
    if (auto chr = pipeline.get_pass<ChromaticAberrationPass>("ChromaticAberration")) {
        chr->set_enabled(chromatic_enabled);
        chr->set_offset(chromatic_offset);
    }

    // 3. Bloom
    if (auto blm = pipeline.get_pass<BloomPass>("Bloom")) {
        blm->set_enabled(bloom_enabled);
        blm->set_intensity(bloom_intensity);
        blm->set_radius(bloom_radius);
    }

    // 4. Gaussian Blur
    if (auto blr = pipeline.get_pass<GaussianBlurPass>("GaussianBlur")) {
        blr->set_enabled(blur_enabled);
        blr->set_radius(blur_radius);
    }

    // 5. Color Grading
    if (auto col = pipeline.get_pass<ColorGradingPass>("ColorGrading")) {
        col->set_enabled(color_grading_enabled);
        col->set_tint(color_tint);
        col->set_shadow_tint(shadow_tint);
    }

    // 6. Film Grain
    if (auto grn = pipeline.get_pass<FilmGrainPass>("FilmGrain")) {
        grn->set_enabled(film_grain_enabled);
        grn->set_intensity(film_grain_intensity);
    }

    // 7. Scanlines CRT
    if (auto crt = pipeline.get_pass<ScanlinesCRTPass>("ScanlinesCRT")) {
        crt->set_enabled(crt_enabled);
        crt->set_scanline_intensity(crt_scanline_intensity);
        crt->set_scanline_spacing(crt_scanline_spacing);
        crt->set_flicker(crt_flicker);
    }
}

PostProcessProfile PostProcessProfile::lerp(const PostProcessProfile& a, const PostProcessProfile& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    PostProcessProfile result;
    result.name = (t < 0.5f) ? a.name : b.name;
    result.effect_reference_width = std::max(1, static_cast<int>(std::lround(lerp_f(
        static_cast<float>(a.effect_reference_width),
        static_cast<float>(b.effect_reference_width),
        t
    ))));
    result.effect_reference_height = std::max(1, static_cast<int>(std::lround(lerp_f(
        static_cast<float>(a.effect_reference_height),
        static_cast<float>(b.effect_reference_height),
        t
    ))));

    result.vignette_enabled = a.vignette_enabled || b.vignette_enabled;
    result.vignette_intensity = lerp_f(a.vignette_intensity, b.vignette_intensity, t);
    result.vignette_color = lerp_col(a.vignette_color, b.vignette_color, t);

    result.chromatic_enabled = a.chromatic_enabled || b.chromatic_enabled;
    result.chromatic_offset = lerp_f(a.chromatic_offset, b.chromatic_offset, t);

    result.bloom_enabled = a.bloom_enabled || b.bloom_enabled;
    result.bloom_intensity = lerp_f(a.bloom_intensity, b.bloom_intensity, t);
    result.bloom_radius = lerp_f(a.bloom_radius, b.bloom_radius, t);

    result.blur_enabled = a.blur_enabled || b.blur_enabled;
    result.blur_radius = lerp_f(a.blur_radius, b.blur_radius, t);

    result.color_grading_enabled = a.color_grading_enabled || b.color_grading_enabled;
    result.color_tint = lerp_col(a.color_tint, b.color_tint, t);
    result.shadow_tint = lerp_col(a.shadow_tint, b.shadow_tint, t);

    result.film_grain_enabled = a.film_grain_enabled || b.film_grain_enabled;
    result.film_grain_intensity = lerp_f(a.film_grain_intensity, b.film_grain_intensity, t);

    result.crt_enabled = a.crt_enabled || b.crt_enabled;
    result.crt_scanline_intensity = lerp_f(a.crt_scanline_intensity, b.crt_scanline_intensity, t);
    result.crt_scanline_spacing = lerp_f(a.crt_scanline_spacing, b.crt_scanline_spacing, t);
    result.crt_flicker = lerp_f(a.crt_flicker, b.crt_flicker, t);

    return result;
}

PostProcessProfile PostProcessProfile::PresetDefault() {
    PostProcessProfile p;
    p.name = "Default";
    p.vignette_enabled = true;
    p.vignette_intensity = 0.20f; // Very soft, natural framing
    p.bloom_enabled = true;
    p.bloom_intensity = 0.15f;
    p.bloom_radius = 3.0f;
    p.color_tint = SDL_Color{255, 255, 255, 255};
    return p;
}

PostProcessProfile PostProcessProfile::PresetCinematic() {
    PostProcessProfile p;
    p.name = "Cinematic";
    p.vignette_enabled = true;
    p.vignette_intensity = 0.38f;
    p.vignette_color = SDL_Color{10, 15, 25, 255};
    p.bloom_enabled = true;
    p.bloom_intensity = 0.22f;
    p.bloom_radius = 4.0f;
    p.color_tint = SDL_Color{248, 250, 255, 255};
    p.shadow_tint = SDL_Color{15, 25, 45, 18}; // Soft cinematic teal tone
    p.film_grain_enabled = true;
    p.film_grain_intensity = 0.10f;
    return p;
}

PostProcessProfile PostProcessProfile::PresetRetroCRT() {
    PostProcessProfile p;
    p.name = "RetroCRT";
    p.vignette_enabled = true;
    p.vignette_intensity = 0.35f;
    p.vignette_color = SDL_Color{0, 0, 0, 255};
    p.crt_enabled = true;
    p.crt_scanline_intensity = 0.32f; // Fine, subtle scanlines
    p.crt_scanline_spacing = 3.0f;
    p.crt_flicker = 0.015f;
    p.chromatic_enabled = true;
    p.chromatic_offset = 0.6f;
    p.bloom_enabled = true;
    p.bloom_intensity = 0.20f;
    p.bloom_radius = 3.5f;
    p.color_tint = SDL_Color{245, 255, 245, 255};
    return p;
}

PostProcessProfile PostProcessProfile::PresetUnderwater() {
    PostProcessProfile p;
    p.name = "Underwater";
    p.vignette_enabled = true;
    p.vignette_intensity = 0.42f;
    p.vignette_color = SDL_Color{5, 30, 55, 255};
    p.blur_enabled = true;
    p.blur_radius = 0.7f; // Gentle aquatic softening, not blinding blur
    p.color_tint = SDL_Color{200, 235, 255, 255};
    p.shadow_tint = SDL_Color{0, 35, 70, 25};
    p.chromatic_enabled = true;
    p.chromatic_offset = 0.8f;
    return p;
}

PostProcessProfile PostProcessProfile::PresetNightVision() {
    PostProcessProfile p;
    p.name = "NightVision";
    p.vignette_enabled = true;
    p.vignette_intensity = 0.50f;
    p.vignette_color = SDL_Color{0, 0, 0, 255};
    p.bloom_enabled = true;
    p.bloom_intensity = 0.35f;
    p.bloom_radius = 5.0f;
    p.film_grain_enabled = true;
    p.film_grain_intensity = 0.22f;
    p.color_tint = SDL_Color{140, 255, 150, 255};
    p.shadow_tint = SDL_Color{0, 35, 10, 40};
    return p;
}

PostProcessProfile PostProcessProfile::PresetDreamyBloom() {
    PostProcessProfile p;
    p.name = "DreamyBloom";
    p.vignette_enabled = true;
    p.vignette_intensity = 0.25f;
    p.vignette_color = SDL_Color{255, 230, 240, 255};
    p.bloom_enabled = true;
    p.bloom_intensity = 0.45f;
    p.bloom_radius = 5.0f;
    p.blur_enabled = false;
    p.color_tint = SDL_Color{255, 248, 250, 255};
    p.shadow_tint = SDL_Color{255, 200, 220, 12};
    return p;
}

std::string PostProcessProfile::to_json() const {
    const json document = {
        {"name", name},
        {"effect_reference", {
            {"width", effect_reference_width},
            {"height", effect_reference_height}
        }},
        {"vignette", {
            {"enabled", vignette_enabled},
            {"intensity", vignette_intensity},
            {"color", color_to_json(vignette_color)}
        }},
        {"chromatic", {
            {"enabled", chromatic_enabled},
            {"offset", chromatic_offset}
        }},
        {"bloom", {
            {"enabled", bloom_enabled},
            {"intensity", bloom_intensity},
            {"radius", bloom_radius}
        }},
        {"blur", {
            {"enabled", blur_enabled},
            {"radius", blur_radius}
        }},
        {"color_grading", {
            {"enabled", color_grading_enabled},
            {"tint", color_to_json(color_tint)},
            {"shadow_tint", color_to_json(shadow_tint)}
        }},
        {"film_grain", {
            {"enabled", film_grain_enabled},
            {"intensity", film_grain_intensity}
        }},
        {"crt", {
            {"enabled", crt_enabled},
            {"scanline_intensity", crt_scanline_intensity},
            {"scanline_spacing", crt_scanline_spacing},
            {"flicker", crt_flicker}
        }}
    };
    return document.dump(2) + '\n';
}

bool PostProcessProfile::from_json(const std::string& json_str) {
    try {
        const json document = json::parse(json_str);
        PostProcessProfile parsed;
        parsed.name = document.at("name").get<std::string>();

        const auto& reference = document.at("effect_reference");
        parsed.effect_reference_width = std::max(1, reference.at("width").get<int>());
        parsed.effect_reference_height = std::max(1, reference.at("height").get<int>());

        const auto& vignette = document.at("vignette");
        parsed.vignette_enabled = vignette.at("enabled").get<bool>();
        parsed.vignette_intensity = vignette.at("intensity").get<float>();
        parsed.vignette_color = color_from_json(vignette.at("color"));

        const auto& chromatic = document.at("chromatic");
        parsed.chromatic_enabled = chromatic.at("enabled").get<bool>();
        parsed.chromatic_offset = chromatic.at("offset").get<float>();

        const auto& bloom = document.at("bloom");
        parsed.bloom_enabled = bloom.at("enabled").get<bool>();
        parsed.bloom_intensity = bloom.at("intensity").get<float>();
        parsed.bloom_radius = bloom.at("radius").get<float>();

        const auto& blur = document.at("blur");
        parsed.blur_enabled = blur.at("enabled").get<bool>();
        parsed.blur_radius = blur.at("radius").get<float>();

        const auto& color_grading = document.at("color_grading");
        parsed.color_grading_enabled = color_grading.at("enabled").get<bool>();
        parsed.color_tint = color_from_json(color_grading.at("tint"));
        parsed.shadow_tint = color_from_json(color_grading.at("shadow_tint"));

        const auto& film_grain = document.at("film_grain");
        parsed.film_grain_enabled = film_grain.at("enabled").get<bool>();
        parsed.film_grain_intensity = film_grain.at("intensity").get<float>();

        const auto& crt = document.at("crt");
        parsed.crt_enabled = crt.at("enabled").get<bool>();
        parsed.crt_scanline_intensity = crt.at("scanline_intensity").get<float>();
        parsed.crt_scanline_spacing = crt.at("scanline_spacing").get<float>();
        parsed.crt_flicker = crt.at("flicker").get<float>();

        *this = std::move(parsed);
        return true;
    } catch (const json::exception& error) {
        ENGINE_LOG_ERROR("[POST-PROCESS] Invalid profile JSON: {}", error.what());
        return false;
    }
}

bool PostProcessProfile::save_to_file(const std::string& filepath) const {
    std::ofstream out(filepath);
    if (!out.is_open()) return false;
    out << to_json();
    ENGINE_LOG_INFO("[POST-PROCESS] Saved profile preset '{}' to {}", name, filepath);
    return true;
}

bool PostProcessProfile::load_from_file(const std::string& filepath) {
    std::ifstream in(filepath);
    if (!in.is_open()) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    return from_json(ss.str());
}

bool PostProcessProfile::save_asset(const std::string& filepath, AssetDatabase& database) const {
    if (std::filesystem::path(filepath).extension() != ".postprocess" || !save_to_file(filepath)) return false;
    const auto handle = database.import_asset(filepath);
    return handle.has_value() && database.find(*handle)->type == AssetType::PostProcessProfile;
}

std::optional<PostProcessProfile> PostProcessProfile::load_asset(AssetHandle handle, const AssetDatabase& database) {
    const AssetRecord* record = database.find(handle);
    if (!record || record->status != AssetStatus::Ready || record->type != AssetType::PostProcessProfile) return std::nullopt;
    PostProcessProfile profile;
    if (!profile.load_from_file(database.resolve(handle))) return std::nullopt;
    return profile;
}

} // namespace Engine
