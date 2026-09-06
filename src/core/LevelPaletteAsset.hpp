#pragma once

#include "AssetHandle.hpp"
#include "../renderer/RenderLayer.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

class AssetDatabase;

enum class LevelPaletteEntryKind {
    SpriteRegion,
    Prefab
};

struct LevelPaletteAssetReference {
    AssetHandle handle{};
    std::string path;

    bool operator==(const LevelPaletteAssetReference&) const = default;
};

struct LevelPaletteCategory {
    std::string id;
    std::string label;

    bool operator==(const LevelPaletteCategory&) const = default;
};

struct LevelPalettePlacement {
    glm::vec2 snap{64.0f, 64.0f};
    float spacing{64.0f};
    float rotation_degrees{0.0f};
    float depth_z{0.0f};
    bool flip_x{false};
    bool flip_y{false};
    std::int16_t sorting_layer{0};
    std::int16_t order_in_layer{0};
    RenderLayer render_layer{DEFAULT_RENDER_LAYER};

    bool operator==(const LevelPalettePlacement&) const = default;
};

struct LevelPaletteSpriteRegion {
    LevelPaletteAssetReference texture;
    // x, y, width and height in source texture pixels.
    glm::ivec4 rect_px{0, 0, 64, 64};
    glm::vec2 world_size{64.0f, 64.0f};
    glm::vec2 pivot{0.5f, 0.5f};

    bool operator==(const LevelPaletteSpriteRegion&) const = default;
};

struct LevelPalettePrefab {
    LevelPaletteAssetReference prefab;

    bool operator==(const LevelPalettePrefab&) const = default;
};

struct LevelPaletteEntry {
    std::string id;
    std::string label;
    std::string category;
    LevelPaletteEntryKind kind{LevelPaletteEntryKind::SpriteRegion};
    LevelPaletteSpriteRegion sprite_region{};
    LevelPalettePrefab prefab{};
    LevelPalettePlacement placement{};

    bool operator==(const LevelPaletteEntry&) const = default;
};

struct LevelPaletteAsset {
    static constexpr std::uint32_t CURRENT_VERSION = 1;

    std::uint32_t version{CURRENT_VERSION};
    std::string name{"New Level Palette"};
    glm::vec2 grid_origin{0.0f, 0.0f};
    std::vector<LevelPaletteCategory> categories;
    std::vector<LevelPaletteEntry> entries;

    bool operator==(const LevelPaletteAsset&) const = default;

    [[nodiscard]] nlohmann::json to_json() const;
    [[nodiscard]] static std::optional<LevelPaletteAsset> from_json(
        const nlohmann::json& json,
        std::string* error = nullptr,
        const AssetDatabase* database = nullptr
    );
    [[nodiscard]] static std::optional<LevelPaletteAsset> load_from_file(
        const std::filesystem::path& path,
        std::string* error = nullptr,
        const AssetDatabase* database = nullptr
    );
    [[nodiscard]] bool save_to_file(
        const std::filesystem::path& path,
        std::string* error = nullptr,
        const AssetDatabase* database = nullptr
    ) const;
    [[nodiscard]] bool validate(
        std::string* error = nullptr,
        const AssetDatabase* database = nullptr
    ) const;
    [[nodiscard]] bool canonicalize_references(
        const AssetDatabase& database,
        std::string* error = nullptr
    );

    [[nodiscard]] const LevelPaletteEntry* find_entry(std::string_view id) const;
};

} // namespace Engine
