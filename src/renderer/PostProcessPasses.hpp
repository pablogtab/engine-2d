#pragma once

#include "IPostProcessPass.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

namespace Engine {

// ---------------------------------------------------------------------------
// 1. ScreenFlashPass: Full-screen damage/explosion light flash & tint
// ---------------------------------------------------------------------------
class ScreenFlashPass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "ScreenFlash"; return n; }
    bool is_enabled() const override { return m_enabled && (m_current_color.a > 0 || m_target_color.a > 0); }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void trigger_flash(SDL_Color color, float duration = 0.25f) {
        m_start_color = color;
        m_current_color = color;
        m_target_color = SDL_Color{color.r, color.g, color.b, 0};
        m_duration = std::max(0.01f, duration);
        m_elapsed = 0.0f;
    }

    void update(float delta_time) {
        if (m_elapsed < m_duration) {
            m_elapsed += delta_time;
            float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
            m_current_color.a = static_cast<uint8_t>(std::lerp(static_cast<float>(m_start_color.a), 0.0f, t));
        } else {
            m_current_color.a = 0;
        }
    }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, int width, int height) override {
        // Draw original scene
        SDL_RenderTexture(renderer, source, nullptr, nullptr);

        // Overlay flash tint
        if (m_current_color.a > 0) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, m_current_color.r, m_current_color.g, m_current_color.b, m_current_color.a);
            SDL_FRect screen_rect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)};
            SDL_RenderFillRect(renderer, &screen_rect);
        }
    }

private:
    bool m_enabled{true};
    SDL_Color m_start_color{255, 255, 255, 255};
    SDL_Color m_current_color{255, 255, 255, 0};
    SDL_Color m_target_color{255, 255, 255, 0};
    float m_duration{0.25f};
    float m_elapsed{1.0f};
};

// ---------------------------------------------------------------------------
// 2. VignettePass: Smooth radial corner darkening / cinematic framing
// ---------------------------------------------------------------------------
class VignettePass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "Vignette"; return n; }
    bool is_enabled() const override { return m_enabled && m_intensity > 0.0f; }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void set_intensity(float intensity) { m_intensity = std::clamp(intensity, 0.0f, 1.0f); }
    float get_intensity() const { return m_intensity; }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, int width, int height) override {
        // Draw scene base
        SDL_RenderTexture(renderer, source, nullptr, nullptr);

        float w = static_cast<float>(width);
        float h = static_cast<float>(height);

        if (source) {
            float src_w = 0.0f, src_h = 0.0f;
            SDL_GetTextureSize(source, &src_w, &src_h);
            if (src_w > 0.0f && src_h > 0.0f) {
                w = src_w;
                h = src_h;
            }
        }

        float cx = w * 0.5f;
        float cy = h * 0.5f;

        // Draw 4-quad corner dark gradient mesh
        SDL_FColor corner_col{0.0f, 0.0f, 0.0f, m_intensity * 0.75f};
        SDL_FColor center_col{0.0f, 0.0f, 0.0f, 0.0f};

        SDL_Vertex vertices[9] = {
            {{cx, cy}, center_col, {0.5f, 0.5f}},       // 0: Center
            {{0.0f, 0.0f}, corner_col, {0.0f, 0.0f}},   // 1: Top-Left
            {{cx, 0.0f}, center_col, {0.5f, 0.0f}},     // 2: Top-Mid
            {{w, 0.0f}, corner_col, {1.0f, 0.0f}},      // 3: Top-Right
            {{w, cy}, center_col, {1.0f, 0.5f}},        // 4: Mid-Right
            {{w, h}, corner_col, {1.0f, 1.0f}},         // 5: Bot-Right
            {{cx, h}, center_col, {0.5f, 1.0f}},        // 6: Bot-Mid
            {{0.0f, h}, corner_col, {0.0f, 1.0f}},      // 7: Bot-Left
            {{0.0f, cy}, center_col, {0.0f, 0.5f}}      // 8: Mid-Left
        };

        int indices[24] = {
            0, 1, 2,  0, 2, 3,
            0, 3, 4,  0, 4, 5,
            0, 5, 6,  0, 6, 7,
            0, 7, 8,  0, 8, 1
        };

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(renderer, nullptr, vertices, 9, indices, 24);
    }

private:
    bool m_enabled{true};
    float m_intensity{0.65f};
};

// ---------------------------------------------------------------------------
// 3. ChromaticAberrationPass: High-speed RGB color fringe offset
// ---------------------------------------------------------------------------
class ChromaticAberrationPass : public IPostProcessPass {
public:
    const std::string& get_name() const override { static std::string n = "ChromaticAberration"; return n; }
    bool is_enabled() const override { return m_enabled && m_offset > 0.1f; }
    void set_enabled(bool enabled) override { m_enabled = enabled; }

    void trigger(float offset_pixels, float duration = 0.3f) {
        m_start_offset = offset_pixels;
        m_offset = offset_pixels;
        m_duration = std::max(0.01f, duration);
        m_elapsed = 0.0f;
    }

    void update(float delta_time) {
        if (m_elapsed < m_duration) {
            m_elapsed += delta_time;
            float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);
            m_offset = std::lerp(m_start_offset, 0.0f, t);
        } else {
            m_offset = 0.0f;
        }
    }

    void apply(SDL_Renderer* renderer, SDL_Texture* source, SDL_Texture*, int width, int height) override {
        float w = static_cast<float>(width);
        float h = static_cast<float>(height);

        // 1. Draw base full scene
        SDL_SetTextureBlendMode(source, SDL_BLENDMODE_BLEND);
        SDL_SetTextureColorMod(source, 255, 255, 255);
        SDL_SetTextureAlphaMod(source, 255);
        SDL_FRect dst_full{0.0f, 0.0f, w, h};
        SDL_RenderTexture(renderer, source, nullptr, &dst_full);

        // 2. Overlay subtle RGB chromatic fringe
        if (m_offset > 0.2f) {
            float alpha_factor = std::clamp(m_offset / std::max(0.01f, m_start_offset), 0.0f, 1.0f);
            uint8_t a = static_cast<uint8_t>(110.0f * alpha_factor);

            SDL_SetTextureBlendMode(source, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(source, a);

            // Red shifted left
            SDL_SetTextureColorMod(source, 255, 0, 0);
            SDL_FRect dst_red{-m_offset, 0.0f, w, h};
            SDL_RenderTexture(renderer, source, nullptr, &dst_red);

            // Cyan shifted right
            SDL_SetTextureColorMod(source, 0, 180, 255);
            SDL_FRect dst_blue{m_offset, 0.0f, w, h};
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
};

} // namespace Engine
