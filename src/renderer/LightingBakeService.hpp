#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Engine {

struct LightingBakeStatus {
    std::string input_hash;
    std::filesystem::path directory;
    bool current{false};
    bool has_bake_volume{false};
    std::uint32_t tile_count{0};
    std::string reason;
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
    [[nodiscard]] static bool bake(
        const std::filesystem::path& project_root,
        const std::filesystem::path& scene_path,
        LightingBakeStatus* result,
        std::string* error
    );
};

} // namespace Engine
