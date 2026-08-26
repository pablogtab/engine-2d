#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace EngineScript {

struct VectorOps {
    [[nodiscard]] static constexpr glm::vec2 zero() { return {0.0f, 0.0f}; }
    [[nodiscard]] static constexpr glm::vec2 one() { return {1.0f, 1.0f}; }
    [[nodiscard]] static constexpr glm::vec2 up() { return {0.0f, 1.0f}; }
    [[nodiscard]] static constexpr glm::vec2 right() { return {1.0f, 0.0f}; }
    [[nodiscard]] static float length(const glm::vec2& value) { return glm::length(value); }
    [[nodiscard]] static constexpr float lengthSquared(const glm::vec2& value) { return glm::dot(value, value); }
    [[nodiscard]] static glm::vec2 normalized(const glm::vec2& value) {
        const float squared = lengthSquared(value);
        return squared <= 0.0f ? zero() : value / std::sqrt(squared);
    }
    [[nodiscard]] static float distance(const glm::vec2& a, const glm::vec2& b) { return glm::length(b - a); }
    [[nodiscard]] static constexpr float dot(const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); }
    [[nodiscard]] static constexpr float cross(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }
    [[nodiscard]] static constexpr glm::vec2 lerp(const glm::vec2& a, const glm::vec2& b, float t) { return a + (b - a) * t; }
    [[nodiscard]] static glm::vec2 moveTowards(const glm::vec2& current, const glm::vec2& target, float max_distance) {
        const glm::vec2 difference = target - current;
        const float distance_squared = lengthSquared(difference);
        if (distance_squared == 0.0f || max_distance <= 0.0f) return current;
        const float distance = std::sqrt(distance_squared);
        return distance <= max_distance ? target : current + difference * (max_distance / distance);
    }
    [[nodiscard]] static glm::vec2 fromAngle(float radians) { return {std::cos(radians), std::sin(radians)}; }
    [[nodiscard]] static constexpr glm::vec2 reflect(const glm::vec2& value, const glm::vec2& normal) {
        return value - 2.0f * dot(value, normal) * normal;
    }
    [[nodiscard]] static glm::vec2 project(const glm::vec2& value, const glm::vec2& onto) {
        const float denominator = lengthSquared(onto);
        return denominator <= 0.0f ? zero() : onto * (dot(value, onto) / denominator);
    }
    [[nodiscard]] static float angle(const glm::vec2& from, const glm::vec2& to) {
        const float denominator = std::sqrt(lengthSquared(from) * lengthSquared(to));
        return denominator <= 0.0f ? 0.0f : std::acos(std::clamp(dot(from, to) / denominator, -1.0f, 1.0f));
    }
    [[nodiscard]] static float signedAngle(const glm::vec2& from, const glm::vec2& to) {
        return std::atan2(cross(from, to), dot(from, to));
    }
    [[nodiscard]] static glm::vec2 clampMagnitude(const glm::vec2& value, float maximum) {
        const float squared = lengthSquared(value);
        const float limit = std::max(0.0f, maximum);
        return squared > limit * limit ? normalized(value) * limit : value;
    }
    [[nodiscard]] static constexpr glm::vec3 zero3() { return {0.0f, 0.0f, 0.0f}; }
    [[nodiscard]] static constexpr glm::vec3 one3() { return {1.0f, 1.0f, 1.0f}; }
    [[nodiscard]] static float length3(const glm::vec3& value) { return glm::length(value); }
    [[nodiscard]] static glm::vec3 normalized3(const glm::vec3& value) {
        const float squared = glm::dot(value, value);
        return squared <= 0.0f ? zero3() : value / std::sqrt(squared);
    }
    [[nodiscard]] static constexpr float dot3(const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); }
    [[nodiscard]] static constexpr glm::vec3 cross3(const glm::vec3& a, const glm::vec3& b) { return glm::cross(a, b); }
    [[nodiscard]] static constexpr glm::vec3 lerp3(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; }
    [[nodiscard]] static constexpr float lengthSquared3(const glm::vec3& value) { return glm::dot(value, value); }
    [[nodiscard]] static float distance3(const glm::vec3& a, const glm::vec3& b) { return glm::length(b - a); }
    [[nodiscard]] static glm::vec3 project3(const glm::vec3& value, const glm::vec3& onto) {
        const float denominator = lengthSquared3(onto);
        return denominator <= 0.0f ? zero3() : onto * (dot3(value, onto) / denominator);
    }
    [[nodiscard]] static constexpr glm::vec3 reflect3(const glm::vec3& value, const glm::vec3& normal) {
        return value - 2.0f * dot3(value, normal) * normal;
    }
    [[nodiscard]] static glm::vec3 clampMagnitude3(const glm::vec3& value, float maximum) {
        const float squared = lengthSquared3(value);
        const float limit = std::max(0.0f, maximum);
        return squared > limit * limit ? normalized3(value) * limit : value;
    }
    [[nodiscard]] static constexpr glm::vec4 zero4() { return {0.0f, 0.0f, 0.0f, 0.0f}; }
    [[nodiscard]] static constexpr glm::vec4 one4() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    [[nodiscard]] static float length4(const glm::vec4& value) { return glm::length(value); }
    [[nodiscard]] static glm::vec4 normalized4(const glm::vec4& value) {
        const float squared = glm::dot(value, value);
        return squared <= 0.0f ? zero4() : value / std::sqrt(squared);
    }
    [[nodiscard]] static constexpr float dot4(const glm::vec4& a, const glm::vec4& b) { return glm::dot(a, b); }
    [[nodiscard]] static constexpr glm::vec4 lerp4(const glm::vec4& a, const glm::vec4& b, float t) { return a + (b - a) * t; }
    [[nodiscard]] static constexpr glm::vec4 blackColor() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    [[nodiscard]] static constexpr glm::vec4 whiteColor() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    [[nodiscard]] static constexpr glm::vec4 clearColor() { return zero4(); }
    [[nodiscard]] static constexpr glm::vec4 withAlpha(const glm::vec4& value, float alpha) { return {value.x, value.y, value.z, alpha}; }
    [[nodiscard]] static glm::vec4 srgbToLinear(const glm::vec4& value) {
        const auto convert = [](float channel) { return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f); };
        return {convert(value.x), convert(value.y), convert(value.z), value.w};
    }
    [[nodiscard]] static glm::vec4 linearToSrgb(const glm::vec4& value) {
        const auto convert = [](float channel) { return channel <= 0.0031308f ? channel * 12.92f : 1.055f * std::pow(channel, 1.0f / 2.4f) - 0.055f; };
        return {convert(value.x), convert(value.y), convert(value.z), value.w};
    }
};

} // namespace EngineScript
