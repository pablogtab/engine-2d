#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

struct SurfaceSourceSpec {
    std::filesystem::path path;
    float weight{1.0f};
    bool allow_flip_x{true};
    bool allow_flip_y{true};
    bool allow_quarter_turns{false};
};

struct SurfaceGenerationSettings {
    std::vector<SurfaceSourceSpec> sources;
    int width{2432};
    int height{1408};
    int patch_size{128};
    int overlap{32};
    int candidate_count{48};
    int repetition_radius{3};
    std::uint32_t seed{1337};
    float luminance_normalization{0.25f};
};

struct SurfaceGenerationMetrics {
    float average_overlap_error{0.0f};
    float repetition_score{0.0f};
    std::vector<float> source_usage;
};

struct SurfaceGenerationResult {
    bool success{false};
    bool cancelled{false};
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
    std::uint64_t content_hash{0};
    SurfaceGenerationMetrics metrics;
    std::vector<std::string> warnings;
    std::string error;
};

struct SurfaceGenerationControl {
    std::atomic<bool> cancel{false};
    std::atomic<float> progress{0.0f};
};

class SurfaceGenerationService {
public:
    [[nodiscard]] static bool validate_settings(
        const SurfaceGenerationSettings& settings, std::string& error
    );

    [[nodiscard]] static SurfaceGenerationResult generate(
        const SurfaceGenerationSettings& settings,
        SurfaceGenerationControl* control = nullptr
    );

    [[nodiscard]] static bool save_png_atomic(
        const std::filesystem::path& output_path,
        const SurfaceGenerationResult& result,
        std::string& error
    );
};

} // namespace Engine
