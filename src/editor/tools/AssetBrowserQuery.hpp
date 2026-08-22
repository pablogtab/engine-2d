#pragma once

#include "../../core/AssetDatabase.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

enum class AssetBrowserScope {
    CurrentFolder,
    Project
};

enum class AssetBrowserTypeFilter {
    All,
    Folders,
    Scenes,
    Prefabs,
    Textures,
    Fonts,
    Audio,
    Shaders,
    Materials,
    Scripts,
    Profiles,
    Data,
    Other
};

enum class AssetBrowserStatusFilter {
    All,
    Ready,
    Issues
};

enum class AssetBrowserSort {
    Name,
    Type,
    Modified,
    Size
};

struct AssetBrowserEntry {
    std::filesystem::path absolute_path;
    std::string project_path;
    std::string name;
    std::string extension;
    bool is_directory{false};
    AssetHandle handle{};
    AssetType type{AssetType::Unknown};
    AssetStatus status{AssetStatus::Ready};
    std::uintmax_t size{0};
    std::filesystem::file_time_type modified{};
};

struct AssetBrowserQueryOptions {
    std::string text;
    AssetBrowserScope scope{AssetBrowserScope::CurrentFolder};
    AssetBrowserTypeFilter type{AssetBrowserTypeFilter::All};
    AssetBrowserStatusFilter status{AssetBrowserStatusFilter::All};
    AssetBrowserSort sort{AssetBrowserSort::Name};
    bool ascending{true};
};

class AssetBrowserQuery {
public:
    [[nodiscard]] static std::vector<AssetBrowserEntry> collect(
        const AssetDatabase& database,
        const std::filesystem::path& assets_root,
        const std::filesystem::path& current_directory,
        const AssetBrowserQueryOptions& options
    );

    [[nodiscard]] static bool matches_text(
        const AssetBrowserEntry& entry,
        const std::string& query
    );

    [[nodiscard]] static const char* type_filter_name(AssetBrowserTypeFilter filter);
    [[nodiscard]] static const char* status_filter_name(AssetBrowserStatusFilter filter);
    [[nodiscard]] static const char* sort_name(AssetBrowserSort sort);
    [[nodiscard]] static const char* status_name(AssetStatus status);
};

} // namespace Engine
