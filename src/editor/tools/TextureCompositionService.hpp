#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

struct TextureCompositionLayer {
    std::string name;
    std::filesystem::path source_path;
    SDL_FRect source_rect{0.0f, 0.0f, 0.0f, 0.0f};
    bool use_source_rect{false};
    float anchor_x{0.0f};
    float anchor_y{0.0f};
    float width{64.0f};
    float height{64.0f};
    float pivot_x{0.5f};
    float pivot_y{0.5f};
    float rotation_degrees{0.0f};
    bool flip_x{false};
    bool flip_y{false};
    SDL_Color tint{255, 255, 255, 255};
    std::uint64_t sort_key{0};
    bool visible{true};
};

struct TextureCompositionSettings {
    int width{512};
    int height{512};
    std::vector<TextureCompositionLayer> layers;
};

struct TextureCompositionResult {
    bool success{false};
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
    std::vector<std::string> warnings;
    std::string error;
};

class TextureCompositionService {
public:
    [[nodiscard]] static bool validate(
        const TextureCompositionSettings& settings, std::string& error
    );
    [[nodiscard]] static TextureCompositionResult compose(
        const TextureCompositionSettings& settings
    );
    [[nodiscard]] static bool save_png_atomic(
        const std::filesystem::path& output_path,
        const TextureCompositionResult& result,
        std::string& error
    );
};

} // namespace Engine
