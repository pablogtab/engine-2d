#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <optional>

namespace EngineScript {

struct Rect {
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 size{0.0f, 0.0f};

    constexpr Rect() = default;
    constexpr Rect(float x, float y, float width, float height) : position{x, y}, size{width, height} {}
};

struct RectOps {
    [[nodiscard]] static Rect fromMinMax(const glm::vec2& minimum, const glm::vec2& maximum) {
        return {minimum.x, minimum.y, maximum.x - minimum.x, maximum.y - minimum.y};
    }
    [[nodiscard]] static Rect normalized(const Rect& rect) {
        const float minimum_x = std::min(rect.position.x, rect.position.x + rect.size.x);
        const float minimum_y = std::min(rect.position.y, rect.position.y + rect.size.y);
        const float maximum_x = std::max(rect.position.x, rect.position.x + rect.size.x);
        const float maximum_y = std::max(rect.position.y, rect.position.y + rect.size.y);
        return {minimum_x, minimum_y, maximum_x - minimum_x, maximum_y - minimum_y};
    }
    [[nodiscard]] static glm::vec2 center(const Rect& rect) {
        const Rect value = normalized(rect);
        return value.position + value.size * 0.5f;
    }
    [[nodiscard]] static glm::vec2 min(const Rect& rect) { return normalized(rect).position; }
    [[nodiscard]] static glm::vec2 max(const Rect& rect) { const Rect value = normalized(rect); return value.position + value.size; }
    [[nodiscard]] static bool contains(const Rect& rect, const glm::vec2& point) {
        const Rect value = normalized(rect);
        const glm::vec2 maximum = value.position + value.size;
        return point.x >= value.position.x && point.x <= maximum.x && point.y >= value.position.y && point.y <= maximum.y;
    }
    [[nodiscard]] static bool overlaps(const Rect& left, const Rect& right) {
        const Rect a = normalized(left);
        const Rect b = normalized(right);
        return a.position.x < b.position.x + b.size.x && a.position.x + a.size.x > b.position.x &&
            a.position.y < b.position.y + b.size.y && a.position.y + a.size.y > b.position.y;
    }
    [[nodiscard]] static Rect unite(const Rect& left, const Rect& right) {
        const Rect a = normalized(left);
        const Rect b = normalized(right);
        const glm::vec2 minimum{std::min(a.position.x, b.position.x), std::min(a.position.y, b.position.y)};
        const glm::vec2 maximum{std::max(a.position.x + a.size.x, b.position.x + b.size.x), std::max(a.position.y + a.size.y, b.position.y + b.size.y)};
        return fromMinMax(minimum, maximum);
    }
    [[nodiscard]] static std::optional<Rect> intersection(const Rect& left, const Rect& right) {
        const glm::vec2 minimum{std::max(min(left).x, min(right).x), std::max(min(left).y, min(right).y)};
        const glm::vec2 maximum{std::min(max(left).x, max(right).x), std::min(max(left).y, max(right).y)};
        if (maximum.x <= minimum.x || maximum.y <= minimum.y) return std::nullopt;
        return fromMinMax(minimum, maximum);
    }
    [[nodiscard]] static glm::vec2 closestPoint(const Rect& rect, const glm::vec2& point) {
        const Rect value = normalized(rect);
        const glm::vec2 maximum = value.position + value.size;
        return {std::clamp(point.x, value.position.x, maximum.x), std::clamp(point.y, value.position.y, maximum.y)};
    }
    [[nodiscard]] static Rect expand(const Rect& rect, float amount) {
        return normalized({rect.position.x - amount, rect.position.y - amount, rect.size.x + amount * 2.0f, rect.size.y + amount * 2.0f});
    }
};

} // namespace EngineScript
