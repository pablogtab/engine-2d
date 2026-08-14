#pragma once

// ParticlePresets.hpp
// ---------------------------------------------------------------------------
// Optional, header-only preset library for ParticleSystem2D.
//
// Design: These are game-context recipes — free functions that accept a
// ParticleSystem2D& and call emit() with pre-configured ParticleProps.
// They are NOT part of the engine core. Any game can ignore this file,
// extend it, or replace it with its own preset library.
//
// Usage:
//   #include "particles/ParticlePresets.hpp"
//   Engine::Presets::explosion(m_particles, pos);
// ---------------------------------------------------------------------------

#include "ParticleSystem2D.hpp"
#include <cmath>
#include <random>

namespace Engine::Presets {

namespace detail {
    inline thread_local std::mt19937 rng{std::random_device{}()};
    inline thread_local std::uniform_real_distribution<float> dist01{0.0f, 1.0f};
    inline float rnd(float lo, float hi) {
        return lo + (hi - lo) * dist01(rng);
    }
}

// ---------------------------------------------------------------------------
// Radial burst of fast point-light particles with world collision / bouncing,
// followed by a slower expanding soft-disc cloud.
// ---------------------------------------------------------------------------
inline void explosion(ParticleSystem2D& ps, const glm::vec2& pos,
                      int spark_count = 45, int smoke_count = 18) {
    using namespace detail;

    Texture* spark_tex = ps.get_default_spark();
    Texture* smoke_tex = ps.get_default_soft_circle();

    // 1. Fast outward point-light sparks with terrain bounce
    for (int i = 0; i < spark_count; ++i) {
        float angle = rnd(0.0f, 6.28318f);
        float speed = rnd(180.0f, 580.0f);

        ParticleProps p;
        p.position           = pos;
        p.texture            = spark_tex;
        p.velocity           = glm::vec2(std::cos(angle) * speed, std::sin(angle) * speed);
        p.gravity            = glm::vec2(0.0f, -650.0f);
        p.collide_with_world = true;
        p.bounciness         = rnd(0.45f, 0.70f);
        p.color_start        = SDL_Color{255, 230, 60, 255};
        p.color_end          = SDL_Color{255, 50, 0, 0};
        p.size_start         = rnd(7.0f, 12.0f);
        p.size_end           = 1.0f;
        p.lifetime           = rnd(0.45f, 0.95f);
        p.rotation_speed     = rnd(-360.0f, 360.0f);
        ps.emit(p, 1);
    }

    // 2. Slow expanding soft-disc cloud
    for (int i = 0; i < smoke_count; ++i) {
        float angle = rnd(0.0f, 6.28318f);
        float speed = rnd(30.0f, 120.0f);

        ParticleProps p;
        p.position       = pos + glm::vec2(rnd(-12.0f, 12.0f), rnd(-12.0f, 12.0f));
        p.texture        = smoke_tex;
        p.velocity       = glm::vec2(std::cos(angle) * speed, std::sin(angle) * speed + 35.0f);
        p.gravity        = glm::vec2(0.0f, 20.0f);
        p.color_start    = SDL_Color{70, 70, 75, 200};
        p.color_end      = SDL_Color{25, 25, 30, 0};
        p.size_start     = rnd(14.0f, 22.0f);
        p.size_end       = rnd(38.0f, 54.0f);
        p.lifetime       = rnd(0.6f, 1.1f);
        p.rotation_speed = rnd(-60.0f, 60.0f);
        ps.emit(p, 1);
    }
}

// ---------------------------------------------------------------------------
// Radial burst of glittering point-light particles.
// ---------------------------------------------------------------------------
inline void sparkle(ParticleSystem2D& ps, const glm::vec2& pos, int count = 16,
                    SDL_Color color_start = {255, 255, 200, 255},
                    SDL_Color color_end   = {255, 190, 0, 0}) {
    using namespace detail;

    Texture* tex = ps.get_default_spark();
    for (int i = 0; i < count; ++i) {
        float angle = rnd(0.0f, 6.28318f);
        float speed = rnd(90.0f, 260.0f);

        ParticleProps p;
        p.position       = pos;
        p.texture        = tex;
        p.velocity       = glm::vec2(std::cos(angle) * speed, std::sin(angle) * speed);
        p.gravity        = glm::vec2(0.0f, -120.0f);
        p.color_start    = color_start;
        p.color_end      = color_end;
        p.size_start     = rnd(8.0f, 13.0f);
        p.size_end       = 0.0f;
        p.lifetime       = rnd(0.35f, 0.65f);
        p.rotation_speed = rnd(-240.0f, 240.0f);
        ps.emit(p, 1);
    }
}

// ---------------------------------------------------------------------------
// Downward angled ricochet spray of high-speed point-light particles.
// Angle range: [-135 deg, -45 deg] (downward cone).
// ---------------------------------------------------------------------------
inline void impact_sparks(ParticleSystem2D& ps, const glm::vec2& pos, int count = 12) {
    using namespace detail;

    Texture* tex = ps.get_default_spark();
    for (int i = 0; i < count; ++i) {
        float angle = rnd(-2.35f, -0.78f);
        float speed = rnd(120.0f, 320.0f);

        ParticleProps p;
        p.position       = pos;
        p.texture        = tex;
        p.velocity       = glm::vec2(std::cos(angle) * speed, std::sin(angle) * speed);
        p.gravity        = glm::vec2(0.0f, -700.0f);
        p.color_start    = SDL_Color{255, 240, 120, 255};
        p.color_end      = SDL_Color{255, 100, 0, 0};
        p.size_start     = rnd(5.0f, 9.0f);
        p.size_end       = 0.5f;
        p.lifetime       = rnd(0.2f, 0.45f);
        ps.emit(p, 1);
    }
}

// ---------------------------------------------------------------------------
// Low-speed bidirectional lateral soft-disc puff along the ground plane.
// ---------------------------------------------------------------------------
inline void dust_puff(ParticleSystem2D& ps, const glm::vec2& pos, int count = 8) {
    using namespace detail;

    Texture* tex = ps.get_default_soft_circle();
    for (int i = 0; i < count; ++i) {
        float dir_x   = rnd(-1.0f, 1.0f);
        float speed_x = dir_x * rnd(40.0f, 110.0f);
        float speed_y = rnd(15.0f, 55.0f);

        ParticleProps p;
        p.position       = pos + glm::vec2(rnd(-8.0f, 8.0f), 0.0f);
        p.texture        = tex;
        p.velocity       = glm::vec2(speed_x, speed_y);
        p.gravity        = glm::vec2(0.0f, -30.0f);
        p.color_start    = SDL_Color{210, 200, 185, 180};
        p.color_end      = SDL_Color{160, 150, 140, 0};
        p.size_start     = rnd(6.0f, 10.0f);
        p.size_end       = rnd(16.0f, 24.0f);
        p.lifetime       = rnd(0.25f, 0.45f);
        ps.emit(p, 1);
    }
}

// ---------------------------------------------------------------------------
// Continuous Campfire: Rising incandescent flame tongues, floating sparks,
// ---------------------------------------------------------------------------
// Continuous Campfire: Rising incandescent flame tongues, floating sparks,
// and gentle dark smoke wisps. Scalable via scale parameter.
// ---------------------------------------------------------------------------
inline void campfire(ParticleSystem2D& ps, const glm::vec2& pos, float scale = 1.0f) {
    using namespace detail;

    Texture* spark_tex = ps.get_default_spark();
    Texture* soft_tex  = ps.get_default_soft_circle();

    // 1. Rising Flame Core Tongues (High density, short life, upward buoyancy)
    int flame_count = (dist01(rng) < 0.85f) ? 2 : 1;
    for (int i = 0; i < flame_count; ++i) {
        float offset_x = rnd(-9.0f, 9.0f) * scale;
        float speed_y = rnd(65.0f, 140.0f) * scale;
        float speed_x = rnd(-14.0f, 14.0f) * scale;

        ParticleProps p;
        p.position       = pos + glm::vec2(offset_x, rnd(-2.0f, 6.0f) * scale);
        p.texture        = soft_tex;
        p.z_layer        = -0.1f; // Behind player
        p.velocity       = glm::vec2(speed_x, speed_y);
        p.gravity        = glm::vec2(0.0f, 45.0f * scale);
        p.color_start    = SDL_Color{255, static_cast<uint8_t>(rnd(200.0f, 255.0f)), static_cast<uint8_t>(rnd(40.0f, 90.0f)), 245};
        p.color_end      = SDL_Color{230, static_cast<uint8_t>(rnd(30.0f, 70.0f)), 0, 0};
        p.size_start     = rnd(13.0f, 20.0f) * scale;
        p.size_end       = rnd(2.0f, 5.0f) * scale;
        p.lifetime       = rnd(0.28f, 0.50f);
        p.rotation_speed = rnd(-90.0f, 90.0f);
        ps.emit(p, 1);
    }

    // 2. Glowing Embers / Air Sparks (Occasional spark flying high)
    if (dist01(rng) < 0.45f) {
        float speed_y = rnd(120.0f, 240.0f) * scale;
        float speed_x = rnd(-35.0f, 35.0f) * scale;

        ParticleProps p;
        p.position       = pos + glm::vec2(rnd(-10.0f, 10.0f) * scale, rnd(4.0f, 12.0f) * scale);
        p.texture        = spark_tex;
        p.z_layer        = -0.05f; // Behind player
        p.velocity       = glm::vec2(speed_x, speed_y);
        p.gravity        = glm::vec2(rnd(-15.0f, 15.0f) * scale, 55.0f * scale);
        p.color_start    = SDL_Color{255, 240, 150, 255};
        p.color_end      = SDL_Color{255, 80, 0, 0};
        p.size_start     = rnd(4.0f, 7.0f) * scale;
        p.size_end       = 0.5f;
        p.lifetime       = rnd(0.6f, 1.2f);
        p.rotation_speed = rnd(-200.0f, 200.0f);
        ps.emit(p, 1);
    }

    // 3. Wispy Smoke (Drifts high into the night sky)
    if (dist01(rng) < 0.30f) {
        float speed_y = rnd(45.0f, 90.0f) * scale;
        float speed_x = rnd(-18.0f, 18.0f) * scale;

        ParticleProps p;
        p.position       = pos + glm::vec2(rnd(-6.0f, 6.0f) * scale, rnd(14.0f, 22.0f) * scale);
        p.texture        = soft_tex;
        p.z_layer        = -0.05f; // Behind player
        p.velocity       = glm::vec2(speed_x, speed_y);
        p.gravity        = glm::vec2(rnd(-10.0f, 10.0f) * scale, 15.0f * scale);
        p.color_start    = SDL_Color{70, 65, 75, 120};
        p.color_end      = SDL_Color{25, 25, 30, 0};
        p.size_start     = rnd(8.0f, 14.0f) * scale;
        p.size_end       = rnd(30.0f, 46.0f) * scale;
        p.lifetime       = rnd(0.8f, 1.6f);
        p.rotation_speed = rnd(-45.0f, 45.0f);
        ps.emit(p, 1);
    }
}

} // namespace Engine::Presets
