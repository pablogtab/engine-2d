#pragma once

#include "AssetBrowserQuery.hpp"

#include <filesystem>
#include <vector>

namespace Engine {

enum class AssetBrowserViewMode {
    Grid,
    List,
    Details
};

enum class AssetBrowserCollection {
    All,
    Favorites,
    Recent
};

struct AssetBrowserPersistentState {
    AssetBrowserViewMode view_mode{AssetBrowserViewMode::Grid};
    AssetBrowserCollection collection{AssetBrowserCollection::All};
    AssetBrowserScope scope{AssetBrowserScope::CurrentFolder};
    AssetBrowserTypeFilter type_filter{AssetBrowserTypeFilter::All};
    AssetBrowserStatusFilter status_filter{AssetBrowserStatusFilter::All};
    AssetBrowserSort sort{AssetBrowserSort::Name};
    bool sort_ascending{true};
    float thumbnail_size{72.0f};
    std::vector<AssetHandle> favorites;
    std::vector<AssetHandle> recent;
};

class AssetBrowserState {
public:
    [[nodiscard]] static AssetBrowserPersistentState load(const std::filesystem::path& path);
    [[nodiscard]] static bool save(
        const std::filesystem::path& path,
        const AssetBrowserPersistentState& state
    );

    [[nodiscard]] static bool contains(
        const std::vector<AssetHandle>& handles,
        AssetHandle handle
    );
    static bool toggle_favorite(std::vector<AssetHandle>& favorites, AssetHandle handle);
    static void record_recent(
        std::vector<AssetHandle>& recent,
        AssetHandle handle,
        std::size_t maximum = 32
    );
    static void prune_missing(
        std::vector<AssetHandle>& handles,
        const AssetDatabase& database
    );
};

} // namespace Engine
