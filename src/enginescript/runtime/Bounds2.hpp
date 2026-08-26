#pragma once

#include <glm/glm.hpp>

#include <algorithm>

namespace EngineScript {

struct Bounds2 {
    glm::vec2 center{0.0f};
    glm::vec2 size{0.0f};
    constexpr Bounds2() = default;
    constexpr Bounds2(float x, float y, float width, float height) : center{x, y}, size{width, height} {}
};

struct Bounds2Ops {
    [[nodiscard]] static Bounds2 fromMinMax(const glm::vec2& minimum, const glm::vec2& maximum) {
        return {(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f, maximum.x - minimum.x, maximum.y - minimum.y};
    }
    [[nodiscard]] static glm::vec2 min(const Bounds2& value) { return value.center - glm::abs(value.size) * 0.5f; }
    [[nodiscard]] static glm::vec2 max(const Bounds2& value) { return value.center + glm::abs(value.size) * 0.5f; }
    [[nodiscard]] static bool contains(const Bounds2& value, const glm::vec2& point) { const auto lo = min(value); const auto hi = max(value); return point.x >= lo.x && point.x <= hi.x && point.y >= lo.y && point.y <= hi.y; }
    [[nodiscard]] static bool intersects(const Bounds2& left, const Bounds2& right) { const auto a = min(left); const auto b = max(left); const auto c = min(right); const auto d = max(right); return a.x < d.x && b.x > c.x && a.y < d.y && b.y > c.y; }
    [[nodiscard]] static glm::vec2 closestPoint(const Bounds2& value, const glm::vec2& point) { const auto lo = min(value); const auto hi = max(value); return {std::clamp(point.x, lo.x, hi.x), std::clamp(point.y, lo.y, hi.y)}; }
    [[nodiscard]] static Bounds2 encapsulate(const Bounds2& value, const glm::vec2& point) { const auto lo = glm::min(min(value), point); const auto hi = glm::max(max(value), point); return fromMinMax(lo, hi); }
    [[nodiscard]] static Bounds2 expand(const Bounds2& value, float amount) { Bounds2 result = value; result.size = glm::max(glm::abs(result.size) + glm::vec2(amount * 2.0f), glm::vec2(0.0f)); return result; }
};

} // namespace EngineScript
