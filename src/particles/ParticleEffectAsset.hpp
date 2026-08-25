#pragma once

#include "../core/AssetHandle.hpp"
#include "../renderer/RenderLayer.hpp"
#include "../physics/PhysicsSystem.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Engine {

class AssetDatabase;

enum class ParticleBlendMode : uint8_t {
    Alpha = 0,
    Additive = 1
};

struct ParticleAppearance {
    SDL_Color start_color{255, 255, 255, 255};
    SDL_Color end_color{255, 255, 255, 0};
    float start_size{10.0f};
    float end_size{2.0f};
    float size_variation{0.0f};

    bool operator==(const ParticleAppearance& other) const {
        return start_color.r == other.start_color.r && start_color.g == other.start_color.g &&
               start_color.b == other.start_color.b && start_color.a == other.start_color.a &&
               end_color.r == other.end_color.r && end_color.g == other.end_color.g &&
               end_color.b == other.end_color.b && end_color.a == other.end_color.a &&
               start_size == other.start_size && end_size == other.end_size &&
               size_variation == other.size_variation;
    }
};

struct ParticleMotion {
    glm::vec2 initial_velocity{0.0f, -50.0f};
    glm::vec2 velocity_variation{20.0f, 10.0f};
    glm::vec2 acceleration{0.0f, 0.0f};
    glm::vec2 gravity{0.0f, 0.0f};

    float initial_rotation{0.0f};
    float rotation_variation{0.0f};
    float rotation_speed{0.0f};
    float rotation_speed_variation{0.0f};

    bool operator==(const ParticleMotion&) const = default;
};

struct ParticleLifetime {
    float duration{1.0f};
    float variation{0.2f};

    bool operator==(const ParticleLifetime&) const = default;
};

struct ParticleCollisionSettings {
    bool enabled{false};
    uint16_t mask_bits{static_cast<uint16_t>(CollisionCategory::GROUND)};
    float bounciness{0.5f};

    bool operator==(const ParticleCollisionSettings&) const = default;
};

struct ParticleEmissionDefaults {
    float continuous_rate{20.0f};
    int burst_count{10};
    float duration{0.0f};
    float start_delay{0.0f};
    bool loop{true};

    bool operator==(const ParticleEmissionDefaults&) const = default;
};

struct ParticleRenderingSettings {
    RenderLayer render_layer{DEFAULT_RENDER_LAYER};
    float z_layer{-0.05f};
    ParticleBlendMode blend_mode{ParticleBlendMode::Alpha};

    bool operator==(const ParticleRenderingSettings&) const = default;
};

struct ParticleEffectAsset {
    static constexpr uint32_t CURRENT_VERSION = 1;

    uint32_t version{CURRENT_VERSION};
    std::string name{"New Particle Effect"};
    AssetHandle texture_handle{};

    ParticleAppearance appearance{};
    ParticleMotion motion{};
    ParticleLifetime lifetime{};
    ParticleCollisionSettings collision{};
    ParticleEmissionDefaults emission{};
    ParticleRenderingSettings rendering{};

    bool operator==(const ParticleEffectAsset&) const = default;

    [[nodiscard]] nlohmann::json to_json() const;
    [[nodiscard]] static std::optional<ParticleEffectAsset> from_json(const nlohmann::json& json, std::string* error = nullptr);
    [[nodiscard]] bool validate(std::string* error = nullptr) const;

    [[nodiscard]] static std::optional<ParticleEffectAsset> load_asset(AssetHandle handle, const AssetDatabase& database);
    [[nodiscard]] static std::optional<ParticleEffectAsset> load_from_file(const std::string& path, std::string* error = nullptr);
    [[nodiscard]] bool save_asset(const std::string& path, AssetDatabase& database, std::string* error = nullptr) const;
};

} // namespace Engine
