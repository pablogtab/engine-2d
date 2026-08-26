#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

struct LightingBakeStatus {
    std::string input_hash;
    std::filesystem::path directory;
    bool current{false};
    bool has_bake_volume{false};
    std::uint32_t tile_count{0};
    std::string reason;
};

struct LightingBakeTile {
    std::filesystem::path image_path;
    float origin_x{0.0f};
    float origin_y{0.0f};
    float texels_per_unit{1.0f};
    std::uint32_t affected_layers{0xffffffffu};
};

struct LightingBakeManifest {
    LightingBakeStatus status;
    std::vector<LightingBakeTile> tiles;
};

// Offline, deterministic bake receipt. This intentionally never writes scene
// data; the scene remains the input and Library remains the generated output.
class LightingBakeService {
public:
    static constexpr int k_tile_size = 1024;
    static constexpr std::string_view k_baker_version = "lighting-baker-v1";

    [[nodiscard]] static LightingBakeStatus status(
        const std::filesystem::path& project_root,
        const std::filesystem::path& scene_path
    );
    [[nodiscard]] static std::optional<LightingBakeManifest> load_current(
        const std::filesystem::path& project_root,
        const std::filesystem::path& scene_path
    );
    [[nodiscard]] static bool bake(
        const std::filesystem::path& project_root,
        const std::filesystem::path& scene_path,
        LightingBakeStatus* result,
        std::string* error
    );
};

} // namespace Engine
