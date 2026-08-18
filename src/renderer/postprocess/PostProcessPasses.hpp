#pragma once

#include "IPostProcessPass.hpp"
#include "../../core/Tween.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
#include <vector>

namespace Engine {

// ---------------------------------------------------------------------------
// 1. ScreenFlashPass: Full-screen damage / explosion light flash & tint
// ---------------------------------------------------------------------------
class ScreenFlashPass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "ScreenFlash"; return n; }
    bool is_enabled() const override { return m_enabled && m_current_color.a > 0; }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void trigger_flash(SDL_Color color, float duration = 0.25f, EaseType ease = EaseType::OutQuad) {
        m_start_color = color;
        m_current_color = color;
        m_duration = std::max(0.01f, duration);
        m_elapsed = 0.0f;
        m_ease = ease;
    }

    void update(float delta_time) override {
        if (m_elapsed < m_duration) {
            m_elapsed += delta_time;
            float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
            float alpha_factor = 1.0f - Tween::apply(t, m_ease);
            m_current_color.a = static_cast<uint8_t>(static_cast<float>(m_start_color.a) * alpha_factor);
        } else {
            m_current_color.a = 0;
        }
    }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, const PostProcessContext& context) override {
        SDL_RenderTexture(renderer, source, nullptr, nullptr);
        if (m_current_color.a > 0) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, m_current_color.r, m_current_color.g, m_current_color.b, m_current_color.a);
            SDL_FRect screen_rect{0.0f, 0.0f, static_cast<float>(context.output_width), static_cast<float>(context.output_height)};
            SDL_RenderFillRect(renderer, &screen_rect);
        }
    }

    void reset() override {
        m_current_color.a = 0;
        m_elapsed = m_duration;
    }

private:
    bool m_enabled{true};
    SDL_Color m_start_color{255, 255, 255, 255};
    SDL_Color m_current_color{255, 255, 255, 0};
    float m_duration{0.25f};
    float m_elapsed{1.0f};
    EaseType m_ease{EaseType::OutQuad};
};

// ---------------------------------------------------------------------------
// 2. VignettePass: Procedural Smoothstep Radial Vignette (Zero Mesh Seams)
// ---------------------------------------------------------------------------
class VignettePass : public IPostProcessPass {
public:
    ~VignettePass() override {
        if (m_vignette_tex) {
            SDL_DestroyTexture(m_vignette_tex);
            m_vignette_tex = nullptr;
        }
    }

    const std::string& get_name() const override { static std::string n = "Vignette"; return n; }
    bool is_enabled() const override { return m_enabled && m_intensity > 0.01f; }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void set_intensity(float intensity) { m_intensity = std::clamp(intensity, 0.0f, 1.0f); }
    float get_intensity() const { return m_intensity; }

    void set_color(SDL_Color color) { m_color = color; }
    SDL_Color get_color() const { return m_color; }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, const PostProcessContext&) override {
        // 1. Draw base scene
        SDL_RenderTexture(renderer, source, nullptr, nullptr);

        if (m_intensity <= 0.01f) return;

        // 2. Lazy creation of 256x256 smooth radial gradient texture
        if (!m_vignette_tex && renderer) {
            constexpr int TEX_SIZE = 256;
            std::vector<uint8_t> pixels(TEX_SIZE * TEX_SIZE * 4, 255);

            for (int y = 0; y < TEX_SIZE; ++y) {
                float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(TEX_SIZE) * 2.0f - 1.0f;
                for (int x = 0; x < TEX_SIZE; ++x) {
                    float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(TEX_SIZE) * 2.0f - 1.0f;
                    
                    // Cinematic corner falloff: center remains unshaded, only outer corners shade
                    float dist = std::sqrt(nx * nx + ny * ny);
                    float v = std::clamp((dist - 0.75f) / (1.40f - 0.75f), 0.0f, 1.0f);
                    float smooth_v = v * v * (3.0f - 2.0f * v); // Cubic smoothstep

                    uint8_t a = static_cast<uint8_t>(smooth_v * 255.0f);
                    int idx = (y * TEX_SIZE + x) * 4;
                    pixels[idx + 0] = 255; // R
                    pixels[idx + 1] = 255; // G
                    pixels[idx + 2] = 255; // B
                    pixels[idx + 3] = a;   // A
                }
            }

            m_vignette_tex = SDL_CreateTexture(
                renderer, 
                SDL_PIXELFORMAT_RGBA32, 
                SDL_TEXTUREACCESS_STATIC, 
                TEX_SIZE, 
                TEX_SIZE
            );
            if (m_vignette_tex) {
                SDL_UpdateTexture(m_vignette_tex, nullptr, pixels.data(), TEX_SIZE * 4);
                SDL_SetTextureBlendMode(m_vignette_tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(m_vignette_tex, SDL_SCALEMODE_LINEAR);
            }
        }

        // 3. Blit smooth continuous radial vignette
        if (m_vignette_tex) {
            SDL_SetTextureColorMod(m_vignette_tex, m_color.r, m_color.g, m_color.b);
            uint8_t final_a = static_cast<uint8_t>(std::clamp(m_intensity * (static_cast<float>(m_color.a) / 255.0f) * 180.0f, 0.0f, 255.0f));
            SDL_SetTextureAlphaMod(m_vignette_tex, final_a);
            SDL_RenderTexture(renderer, m_vignette_tex, nullptr, nullptr);
        }
    }

    void reset() override {
        if (m_vignette_tex) {
            SDL_DestroyTexture(m_vignette_tex);
            m_vignette_tex = nullptr;
        }
    }

private:
    bool m_enabled{true};
    float m_intensity{0.25f};
    SDL_Color m_color{0, 0, 0, 255};
    SDL_Texture* m_vignette_tex{nullptr};
};

// ---------------------------------------------------------------------------
// 3. ChromaticAberrationPass: High-speed RGB color fringe offset
// ---------------------------------------------------------------------------
class ChromaticAberrationPass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "ChromaticAberration"; return n; }
    bool is_enabled() const override { return m_enabled && m_offset > 0.05f; }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void trigger(float offset_pixels, float duration = 0.3f, EaseType ease = EaseType::OutQuad) {
        m_start_offset = offset_pixels;
        m_offset = offset_pixels;
        m_duration = std::max(0.01f, duration);
        m_elapsed = 0.0f;
        m_ease = ease;
    }

    void set_offset(float offset_pixels) { m_offset = offset_pixels; m_start_offset = offset_pixels; }
    float get_offset() const { return m_offset; }

    void update(float delta_time) override {
        if (m_elapsed < m_duration) {
            m_elapsed += delta_time;
            float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
            m_offset = Tween::lerp(m_start_offset, 0.0f, t, m_ease);
        }
    }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, const PostProcessContext& context) override {
        float w = static_cast<float>(context.output_width);
        float h = static_cast<float>(context.output_height);

        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(source, 255, 255, 255);
        SDL_SetTextureAlphaMod(source, 255);
        SDL_FRect dst_full{0.0f, 0.0f, w, h};
        SDL_RenderTexture(renderer, source, nullptr, &dst_full);

        if (m_offset > 0.1f) {
            float alpha_ratio = std::clamp(m_offset / std::max(0.1f, m_start_offset), 0.0f, 1.0f);
            uint8_t a = static_cast<uint8_t>(90.0f * alpha_ratio);

            SDL_SetTextureBlendMode(source, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(source, a);

            // Red shifted left
            SDL_SetTextureColorMod(source, 255, 30, 30);
            const float scaled_offset = context.scale_pixels(m_offset);
            SDL_FRect dst_red{-scaled_offset, 0.0f, w, h};
            SDL_RenderTexture(renderer, source, nullptr, &dst_red);

            // Cyan shifted right
            SDL_SetTextureColorMod(source, 0, 210, 255);
            SDL_FRect dst_blue{scaled_offset, 0.0f, w, h};
            SDL_RenderTexture(renderer, source, nullptr, &dst_blue);

            // Restore source state
            SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(source, 255, 255, 255);
            SDL_SetTextureAlphaMod(source, 255);
        }
    }

private:
    bool m_enabled{true};
    float m_start_offset{0.0f};
    float m_offset{0.0f};
    float m_duration{0.3f};
    float m_elapsed{1.0f};
    EaseType m_ease{EaseType::OutQuad};
};

// ---------------------------------------------------------------------------
// 4. BloomPass: Soft Optical Glow on Highlights
// ---------------------------------------------------------------------------
class BloomPass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "Bloom"; return n; }
    bool is_enabled() const override { return m_enabled && (m_intensity > 0.01f || m_pulse_intensity > 0.01f); }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void set_intensity(float intensity) { m_intensity = std::clamp(intensity, 0.0f, 2.0f); }
    float get_intensity() const { return m_intensity; }

    void set_radius(float radius) { m_radius = std::clamp(radius, 1.0f, 12.0f); }
    float get_radius() const { return m_radius; }

    void trigger_pulse(float peak_intensity, float duration = 0.35f) {
        m_start_pulse = peak_intensity;
        m_pulse_intensity = peak_intensity;
        m_pulse_duration = std::max(0.01f, duration);
        m_pulse_elapsed = 0.0f;
    }

    void update(float delta_time) override {
        if (m_pulse_elapsed < m_pulse_duration) {
            m_pulse_elapsed += delta_time;
            float t = std::clamp(m_pulse_elapsed / m_pulse_duration, 0.0f, 1.0f);
            m_pulse_intensity = Tween::lerp(m_start_pulse, 0.0f, t, EaseType::OutQuad);
        } else {
            m_pulse_intensity = 0.0f;
        }
    }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, const PostProcessContext& context) override {
        float w = static_cast<float>(context.output_width);
        float h = static_cast<float>(context.output_height);

        // 1. Draw base crisp scene
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(source, 255, 255, 255);
        SDL_SetTextureAlphaMod(source, 255);
        SDL_FRect full_rect{0.0f, 0.0f, w, h};
        SDL_RenderTexture(renderer, source, nullptr, &full_rect);

        // 2. Additive glow halos (subtle, non-muddy optical bleed)
        float total_intensity = m_intensity + m_pulse_intensity;
        if (total_intensity > 0.02f) {
            uint8_t alpha = static_cast<uint8_t>(std::clamp(total_intensity * 32.0f, 0.0f, 140.0f));
            SDL_SetTextureBlendMode(source, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(source, alpha);
            SDL_SetTextureColorMod(source, 240, 245, 255);

            float r = context.scale_pixels(m_radius) * 1.5f;
            SDL_FRect tap1{-r, 0.0f, w, h};
            SDL_FRect tap2{r, 0.0f, w, h};
            SDL_FRect tap3{0.0f, -r, w, h};
            SDL_FRect tap4{0.0f, r, w, h};

            SDL_RenderTexture(renderer, source, nullptr, &tap1);
            SDL_RenderTexture(renderer, source, nullptr, &tap2);
            SDL_RenderTexture(renderer, source, nullptr, &tap3);
            SDL_RenderTexture(renderer, source, nullptr, &tap4);

            // Restore source
            SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(source, 255);
            SDL_SetTextureColorMod(source, 255, 255, 255);
        }
    }

private:
    bool m_enabled{true};
    float m_intensity{0.20f};
    float m_radius{4.0f};
    float m_start_pulse{0.0f};
    float m_pulse_intensity{0.0f};
    float m_pulse_duration{0.35f};
    float m_pulse_elapsed{1.0f};
};

// ---------------------------------------------------------------------------
// 5. GaussianBlurPass: Smooth Multi-Tap Screen Space Blur
// ---------------------------------------------------------------------------
class GaussianBlurPass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "GaussianBlur"; return n; }
    bool is_enabled() const override { return m_enabled && m_radius > 0.01f; }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void set_radius(float radius) { m_radius = std::clamp(radius, 0.0f, 6.0f); }
    float get_radius() const { return m_radius; }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, const PostProcessContext& context) override {
        float w = static_cast<float>(context.output_width);
        float h = static_cast<float>(context.output_height);

        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(source, 255, 255, 255);

        // 1. Draw base crisp scene at 100% full opacity
        SDL_SetTextureAlphaMod(source, 255);
        SDL_FRect dst_center{0.0f, 0.0f, w, h};
        SDL_RenderTexture(renderer, source, nullptr, &dst_center);

        // 2. Soft blurred taps scaled smoothly by radius
        if (m_radius > 0.08f) {
            float blur_weight = std::clamp(m_radius / 3.0f, 0.0f, 1.0f);
            uint8_t tap_alpha = static_cast<uint8_t>(35.0f * blur_weight);
            SDL_SetTextureAlphaMod(source, tap_alpha);
            float r = context.scale_pixels(m_radius);
            SDL_FRect t1{-r, 0.0f, w, h};
            SDL_FRect t2{r, 0.0f, w, h};
            SDL_FRect t3{0.0f, -r, w, h};
            SDL_FRect t4{0.0f, r, w, h};
            SDL_RenderTexture(renderer, source, nullptr, &t1);
            SDL_RenderTexture(renderer, source, nullptr, &t2);
            SDL_RenderTexture(renderer, source, nullptr, &t3);
            SDL_RenderTexture(renderer, source, nullptr, &t4);
            SDL_SetTextureAlphaMod(source, 255);
        }
    }

private:
    bool m_enabled{false};
    float m_radius{0.0f};
};

// ---------------------------------------------------------------------------
// 6. ColorGradingPass: Contrast, Brightness, Saturation & Tint
// ---------------------------------------------------------------------------
class ColorGradingPass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "ColorGrading"; return n; }
    bool is_enabled() const override { 
        return m_enabled && (m_tint.r != 255 || m_tint.g != 255 || m_tint.b != 255 || m_tint.a != 255 || m_shadow_tint.a > 0); 
    }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void set_tint(SDL_Color tint) { m_tint = tint; }
    SDL_Color get_tint() const { return m_tint; }

    void set_shadow_tint(SDL_Color shadow_tint) { m_shadow_tint = shadow_tint; }
    SDL_Color get_shadow_tint() const { return m_shadow_tint; }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, const PostProcessContext& context) override {
        // Base textured scene with tint color modulation
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(source, m_tint.r, m_tint.g, m_tint.b);
        SDL_SetTextureAlphaMod(source, m_tint.a);
        SDL_RenderTexture(renderer, source, nullptr, nullptr);

        // Shadow tint / cinematic ambient overlay
        if (m_shadow_tint.a > 0) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, m_shadow_tint.r, m_shadow_tint.g, m_shadow_tint.b, m_shadow_tint.a);
            SDL_FRect full_rect{0.0f, 0.0f, static_cast<float>(context.output_width), static_cast<float>(context.output_height)};
            SDL_RenderFillRect(renderer, &full_rect);
        }

        // Restore source
        SDL_SetTextureColorMod(source, 255, 255, 255);
        SDL_SetTextureAlphaMod(source, 255);
    }

    void reset() override {
        m_tint = SDL_Color{255, 255, 255, 255};
        m_shadow_tint = SDL_Color{0, 0, 0, 0};
    }

private:
    bool m_enabled{true};
    SDL_Color m_tint{255, 255, 255, 255};
    SDL_Color m_shadow_tint{0, 0, 0, 0};
};

// ---------------------------------------------------------------------------
// 7. FilmGrainPass: Procedural Film Noise & Cinematic Grain
// ---------------------------------------------------------------------------
class FilmGrainPass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "FilmGrain"; return n; }
    bool is_enabled() const override { return m_enabled && m_intensity > 0.005f; }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void set_intensity(float intensity) { m_intensity = std::clamp(intensity, 0.0f, 1.0f); }
    float get_intensity() const { return m_intensity; }

    void update(float delta_time) override {
        m_timer += delta_time * 60.0f;
        if (m_timer >= 1.0f) {
            m_timer = 0.0f;
            m_seed = (m_seed * 1664525u + 1013904223u);
        }
    }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, const PostProcessContext& context) override {
        SDL_RenderTexture(renderer, source, nullptr, nullptr);

        if (m_intensity > 0.005f) {
            float w = static_cast<float>(context.output_width);
            float h = static_cast<float>(context.output_height);

            uint8_t alpha = static_cast<uint8_t>(m_intensity * 22.0f);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);

            const float area_scale = context.horizontal_scale() * context.vertical_scale();
            int grain_count = static_cast<int>(static_cast<float>(context.authoring_width) * 0.08f * m_intensity * area_scale);
            const float grain_size = std::max(1.0f, context.pixel_scale());
            uint32_t s = m_seed;
            for (int i = 0; i < grain_count; ++i) {
                s = (s * 1103515245u + 12345u);
                float gx = static_cast<float>(s % static_cast<uint32_t>(w));
                s = (s * 1103515245u + 12345u);
                float gy = static_cast<float>(s % static_cast<uint32_t>(h));
                SDL_FRect pt{gx, gy, grain_size, grain_size};
                SDL_RenderFillRect(renderer, &pt);
            }
        }
    }

private:
    bool m_enabled{false};
    float m_intensity{0.0f};
    float m_timer{0.0f};
    uint32_t m_seed{1337};
};

// ---------------------------------------------------------------------------
// 8. ScanlinesCRTPass: Subtle Retro CRT Scanline Grid
// ---------------------------------------------------------------------------
class ScanlinesCRTPass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "ScanlinesCRT"; return n; }
    bool is_enabled() const override { return m_enabled && (m_scanline_intensity > 0.01f); }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void set_scanline_intensity(float intensity) { m_scanline_intensity = std::clamp(intensity, 0.0f, 1.0f); }
    float get_scanline_intensity() const { return m_scanline_intensity; }

    void set_scanline_spacing(float spacing) { m_scanline_spacing = std::clamp(spacing, 2.0f, 8.0f); }
    float get_scanline_spacing() const { return m_scanline_spacing; }

    void set_flicker(float flicker) { m_flicker = std::clamp(flicker, 0.0f, 0.2f); }
    float get_flicker() const { return m_flicker; }

    void update(float delta_time) override {
        m_time += delta_time;
    }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, const PostProcessContext& context) override {
        SDL_RenderTexture(renderer, source, nullptr, nullptr);

        float w = static_cast<float>(context.output_width);
        float h = static_cast<float>(context.output_height);

        // Draw fine, subtle horizontal raster scanlines
        if (m_scanline_intensity > 0.01f) {
            uint8_t alpha = static_cast<uint8_t>(m_scanline_intensity * 40.0f); // Max 40/255 opacity (~15%)

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);

            const float step = std::max(1.0f, m_scanline_spacing * context.vertical_scale());
            const float thickness = std::max(0.5f, context.vertical_scale());
            for (float y = 0; y < h; y += step) {
                SDL_FRect line_rect{0.0f, y, w, thickness};
                SDL_RenderFillRect(renderer, &line_rect);
            }
        }
    }

private:
    bool m_enabled{false};
    float m_scanline_intensity{0.0f};
    float m_scanline_spacing{3.0f};
    float m_flicker{0.015f};
    float m_time{0.0f};
};

} // namespace Engine
