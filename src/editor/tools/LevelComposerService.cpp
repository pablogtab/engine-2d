#include "LevelComposerService.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine {

namespace {

constexpr std::size_t MAX_LINE_PLACEMENTS = 4'096;
constexpr float POSITION_EPSILON = 0.0001f;

bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool same_position(glm::vec2 lhs, glm::vec2 rhs) {
    const glm::vec2 difference = lhs - rhs;
    return glm::dot(difference, difference) <= POSITION_EPSILON * POSITION_EPSILON;
}

void append_unique(std::vector<glm::vec2>& output, glm::vec2 position) {
    if (output.empty() || !same_position(output.back(), position)) output.push_back(position);
}

} // namespace

glm::vec2 LevelComposerService::snap_position(
    glm::vec2 position,
    glm::vec2 step,
    glm::vec2 origin
) {
    for (int axis = 0; axis < 2; ++axis) {
        if (!std::isfinite(position[axis]) || !std::isfinite(step[axis]) ||
            !std::isfinite(origin[axis]) || step[axis] <= 0.0f) {
            continue;
        }
        position[axis] = origin[axis] +
            std::round((position[axis] - origin[axis]) / step[axis]) * step[axis];
    }
    return position;
}

std::vector<glm::vec2> LevelComposerService::line_placements(
    const LevelComposerLineRequest& request,
    std::string* error
) {
    if (error) error->clear();
    if (!finite(request.start) || !finite(request.end) || !finite(request.snap) ||
        !finite(request.origin) || request.snap.x < 0.0f || request.snap.y < 0.0f ||
        !std::isfinite(request.spacing) || request.spacing <= 0.0f) {
        if (error) *error = "Line placement inputs must be finite; snap cannot be negative and spacing must be positive";
        return {};
    }

    const glm::vec2 start = snap_position(request.start, request.snap, request.origin);
    const glm::vec2 end = snap_position(request.end, request.snap, request.origin);
    const glm::vec2 delta = end - start;
    const float distance = glm::length(delta);
    if (!std::isfinite(distance)) {
        if (error) *error = "Line placement distance is not finite";
        return {};
    }
    if (distance <= POSITION_EPSILON) return {start};

    const double interior_count = std::floor(
        static_cast<double>(distance) / static_cast<double>(request.spacing)
    );
    if (!std::isfinite(interior_count) || interior_count + 2.0 > MAX_LINE_PLACEMENTS) {
        if (error) *error = "Line placement would exceed 4096 instances";
        return {};
    }

    std::vector<glm::vec2> result;
    result.reserve(static_cast<std::size_t>(interior_count) + 2);
    append_unique(result, start);

    const glm::vec2 direction = delta / distance;
    for (std::size_t index = 1; index <= static_cast<std::size_t>(interior_count); ++index) {
        const float travel = static_cast<float>(index) * request.spacing;
        if (travel >= distance - POSITION_EPSILON) break;
        const glm::vec2 sample = snap_position(start + direction * travel, request.snap, request.origin);
        // Snapping can collapse an interior sample onto the endpoint. Keep the
        // endpoint policy authoritative instead of accidentally placing it.
        if (same_position(sample, end)) break;
        append_unique(result, sample);
    }
    if (request.include_end) append_unique(result, end);
    return result;
}

} // namespace Engine
