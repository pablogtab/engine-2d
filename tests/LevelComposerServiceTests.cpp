#include "editor/tools/LevelComposerService.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace Engine;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(glm::vec2 lhs, glm::vec2 rhs, float epsilon = 0.0001f) {
    return std::abs(lhs.x - rhs.x) <= epsilon && std::abs(lhs.y - rhs.y) <= epsilon;
}

void test_snap_uses_origin_and_supports_negative_coordinates() {
    require(
        near(LevelComposerService::snap_position({97.0f, -70.0f}, {64.0f, 32.0f}, {16.0f, -8.0f}),
             {80.0f, -72.0f}),
        "snap must be relative to the configured origin"
    );
    require(
        near(LevelComposerService::snap_position({-35.0f, -33.0f}, {32.0f, 32.0f}),
             {-32.0f, -32.0f}),
        "snap must round negative coordinates symmetrically"
    );
    require(
        near(LevelComposerService::snap_position({12.5f, 70.0f}, {0.0f, 32.0f}),
             {12.5f, 64.0f}),
        "a zero step must leave only that axis unsnapped"
    );
}

void test_line_spacing_and_endpoint_policy() {
    LevelComposerLineRequest request;
    request.start = {0.0f, 0.0f};
    request.end = {100.0f, 0.0f};
    request.spacing = 30.0f;
    request.include_end = true;
    std::string error = "stale";
    auto points = LevelComposerService::line_placements(request, &error);
    require(error.empty(), "successful line placement must clear stale errors");
    require(points.size() == 5, "line must contain start, spaced interior points and endpoint");
    require(near(points[0], {0.0f, 0.0f}) && near(points[1], {30.0f, 0.0f}) &&
            near(points[2], {60.0f, 0.0f}) && near(points[3], {90.0f, 0.0f}) &&
            near(points[4], {100.0f, 0.0f}),
            "line placement distances must be stable");

    request.include_end = false;
    points = LevelComposerService::line_placements(request, &error);
    require(points.size() == 4 && near(points.back(), {90.0f, 0.0f}),
            "endpoint exclusion must retain only start and interior points");

    request.end = request.start;
    points = LevelComposerService::line_placements(request, &error);
    require(points.size() == 1 && near(points.front(), request.start),
            "zero-length line must produce exactly one placement");
}

void test_snapped_lines_deduplicate_collapsed_samples() {
    LevelComposerLineRequest request;
    request.start = {1.0f, 0.0f};
    request.end = {100.0f, 0.0f};
    request.snap = {64.0f, 64.0f};
    request.spacing = 10.0f;
    request.include_end = true;
    std::string error;
    const auto points = LevelComposerService::line_placements(request, &error);
    require(error.empty(), "valid snapped line must succeed");
    require(points.size() == 3, "samples collapsed by snap must be deduplicated");
    require(near(points[0], {0.0f, 0.0f}) && near(points[1], {64.0f, 0.0f}) &&
            near(points[2], {128.0f, 0.0f}),
            "snapped line must use the same grid for every placement");

    request.include_end = false;
    const auto without_end = LevelComposerService::line_placements(request, &error);
    require(without_end.size() == 2 && near(without_end.back(), {64.0f, 0.0f}),
            "snapping must not reintroduce an excluded endpoint");
}

void test_diagonal_line_and_invalid_limits() {
    LevelComposerLineRequest request;
    request.start = {-10.0f, -10.0f};
    request.end = {20.0f, 30.0f};
    request.spacing = 25.0f;
    const auto points = LevelComposerService::line_placements(request);
    require(points.size() == 3, "3-4-5 diagonal must place start, midpoint and end");
    require(near(points[1], {5.0f, 10.0f}) && near(points[2], request.end),
            "diagonal spacing must follow normalized world direction");

    std::string error;
    request.spacing = 0.0f;
    require(LevelComposerService::line_placements(request, &error).empty() &&
                error.find("spacing") != std::string::npos,
            "zero spacing must fail without allocation");

    request.start = {0.0f, 0.0f};
    request.end = {1'000'000.0f, 0.0f};
    request.spacing = 0.001f;
    require(LevelComposerService::line_placements(request, &error).empty() &&
                error.find("4096") != std::string::npos,
            "unbounded placement counts must be rejected before allocation");

    request.end = {10.0f, 0.0f};
    request.spacing = 1.0f;
    request.snap = {-1.0f, 0.0f};
    require(LevelComposerService::line_placements(request, &error).empty() &&
                error.find("snap") != std::string::npos,
            "negative snap steps must be rejected");
}

} // namespace

int main() {
    try {
        test_snap_uses_origin_and_supports_negative_coordinates();
        test_line_spacing_and_endpoint_policy();
        test_snapped_lines_deduplicate_collapsed_samples();
        test_diagonal_line_and_invalid_limits();
        std::cout << "LevelComposerServiceTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "LevelComposerServiceTests failed: " << exception.what() << '\n';
        return 1;
    }
}
