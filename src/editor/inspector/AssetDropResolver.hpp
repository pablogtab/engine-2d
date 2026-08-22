#pragma once

#include "../../core/AssetDatabase.hpp"

#include <filesystem>
#include <span>

namespace Engine {

inline constexpr const char* kContentBrowserAssetPayload = "CONTENT_BROWSER_ASSET";

enum class AssetDropStatus {
    Accepted,
    EmptyPath,
    NotFound,
    NotReady,
    IncompatibleType
};

struct AssetDropResolution {
    AssetDropStatus status{AssetDropStatus::EmptyPath};
    const AssetRecord* record{nullptr};

    [[nodiscard]] bool accepted() const {
        return status == AssetDropStatus::Accepted && record != nullptr;
    }
};

// An empty accepted_types span accepts every ready, known asset type. This is
// used by generic Luau Asset fields while typed component fields pass one or
// more explicit types.
[[nodiscard]] AssetDropResolution resolve_asset_drop(
    const AssetDatabase& database,
    const std::filesystem::path& source_path,
    std::span<const AssetType> accepted_types = {}
);

[[nodiscard]] const char* asset_drop_status_message(AssetDropStatus status);

} // namespace Engine
