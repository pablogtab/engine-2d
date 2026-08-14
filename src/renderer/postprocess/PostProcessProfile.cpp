#include "PostProcessProfile.hpp"
#include "PostProcessPipeline.hpp"
#include "PostProcessPasses.hpp"
#include "../../core/Log.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Engine {

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
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"name\": \"" << name << "\",\n";
    ss << "  \"vignette\": {\n";
    ss << "    \"enabled\": " << (vignette_enabled ? "true" : "false") << ",\n";
    ss << "    \"intensity\": " << vignette_intensity << ",\n";
    ss << "    \"color\": [" << (int)vignette_color.r << ", " << (int)vignette_color.g << ", " << (int)vignette_color.b << ", " << (int)vignette_color.a << "]\n";
    ss << "  },\n";
    ss << "  \"chromatic\": {\n";
    ss << "    \"enabled\": " << (chromatic_enabled ? "true" : "false") << ",\n";
    ss << "    \"offset\": " << chromatic_offset << "\n";
    ss << "  },\n";
    ss << "  \"bloom\": {\n";
    ss << "    \"enabled\": " << (bloom_enabled ? "true" : "false") << ",\n";
    ss << "    \"intensity\": " << bloom_intensity << ",\n";
    ss << "    \"radius\": " << bloom_radius << "\n";
    ss << "  },\n";
    ss << "  \"blur\": {\n";
    ss << "    \"enabled\": " << (blur_enabled ? "true" : "false") << ",\n";
    ss << "    \"radius\": " << blur_radius << "\n";
    ss << "  },\n";
    ss << "  \"color_grading\": {\n";
    ss << "    \"enabled\": " << (color_grading_enabled ? "true" : "false") << ",\n";
    ss << "    \"tint\": [" << (int)color_tint.r << ", " << (int)color_tint.g << ", " << (int)color_tint.b << ", " << (int)color_tint.a << "],\n";
    ss << "    \"shadow_tint\": [" << (int)shadow_tint.r << ", " << (int)shadow_tint.g << ", " << (int)shadow_tint.b << ", " << (int)shadow_tint.a << "]\n";
    ss << "  },\n";
    ss << "  \"film_grain\": {\n";
    ss << "    \"enabled\": " << (film_grain_enabled ? "true" : "false") << ",\n";
    ss << "    \"intensity\": " << film_grain_intensity << "\n";
    ss << "  },\n";
    ss << "  \"crt\": {\n";
    ss << "    \"enabled\": " << (crt_enabled ? "true" : "false") << ",\n";
    ss << "    \"scanline_intensity\": " << crt_scanline_intensity << ",\n";
    ss << "    \"scanline_spacing\": " << crt_scanline_spacing << ",\n";
    ss << "    \"flicker\": " << crt_flicker << "\n";
    ss << "  }\n";
    ss << "}\n";
    return ss.str();
}

bool PostProcessProfile::from_json(const std::string& json_str) {
    auto find_float = [&json_str](const std::string& key, float default_val) -> float {
        size_t pos = json_str.find("\"" + key + "\"");
        if (pos == std::string::npos) return default_val;
        size_t col = json_str.find(':', pos);
        if (col == std::string::npos) return default_val;
        return std::stof(json_str.substr(col + 1));
    };

    auto find_bool = [&json_str](const std::string& key, bool default_val) -> bool {
        size_t pos = json_str.find("\"" + key + "\"");
        if (pos == std::string::npos) return default_val;
        size_t col = json_str.find(':', pos);
        if (col == std::string::npos) return default_val;
        size_t t_pos = json_str.find("true", col);
        size_t f_pos = json_str.find("false", col);
        if (t_pos < f_pos) return true;
        if (f_pos < t_pos) return false;
        return default_val;
    };

    try {
        vignette_intensity = find_float("vignette_intensity", vignette_intensity);
        chromatic_offset = find_float("offset", chromatic_offset);
        bloom_intensity = find_float("intensity", bloom_intensity);
        bloom_radius = find_float("radius", bloom_radius);
        blur_radius = find_float("blur_radius", blur_radius);
        film_grain_intensity = find_float("film_grain_intensity", film_grain_intensity);
        crt_scanline_intensity = find_float("scanline_intensity", crt_scanline_intensity);
        return true;
    } catch (...) {
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

} // namespace Engine
