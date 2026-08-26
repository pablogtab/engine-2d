#pragma once

#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <span>
#include <glm/glm.hpp>

namespace Engine {

class Texture;

// Returns the largest ordered alpha boundary in normalized frame coordinates.
[[nodiscard]] std::vector<glm::vec2> extract_alpha_silhouette(
    const Texture& texture,
    const SDL_FRect& source,
    float alpha_threshold,
    float tolerance_pixels
);
[[nodiscard]] std::vector<glm::vec2> extract_alpha_silhouette(
    std::span<const std::uint8_t> alpha,
    int texture_width,
    int texture_height,
    const SDL_FRect& source,
    float alpha_threshold,
    float tolerance_pixels
);

class ShadowSilhouetteCache {
public:
    const std::vector<glm::vec2>& get(
        const Texture& texture,
        const SDL_FRect& source,
        float alpha_threshold,
        float tolerance_pixels
    );
    void begin_frame();
    [[nodiscard]] std::size_t size() const { return m_entries.size(); }

private:
    struct Entry {
        std::vector<glm::vec2> vertices;
        std::uint64_t last_used_frame{0};
    };
    std::unordered_map<std::uint64_t, Entry> m_entries;
    std::uint64_t m_frame{0};
};

} // namespace Engine
