#include "AssetBrowserState.hpp"

#include "../../core/FileSystem.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace Engine {
namespace {

template<typename Enum>
Enum enum_value(const nlohmann::json& root, const char* key, Enum fallback, int maximum) {
    const int value = root.value(key, static_cast<int>(fallback));
    return value >= 0 && value <= maximum ? static_cast<Enum>(value) : fallback;
}

std::vector<AssetHandle> read_handles(const nlohmann::json& root, const char* key) {
    std::vector<AssetHandle> result;
    const auto values = root.value(key, nlohmann::json::array());
    if (!values.is_array()) return result;
    for (const auto& value : values) {
        if (!value.is_string()) continue;
        const auto handle = AssetHandle::from_string(value.get<std::string>());
        if (handle && !AssetBrowserState::contains(result, *handle)) result.push_back(*handle);
    }
    return result;
}

nlohmann::json write_handles(const std::vector<AssetHandle>& handles) {
    nlohmann::json result = nlohmann::json::array();
    for (const AssetHandle handle : handles) {
        if (handle.is_valid()) result.push_back(handle.to_string());
    }
    return result;
}

} // namespace

AssetBrowserPersistentState AssetBrowserState::load(const std::filesystem::path& path) {
    AssetBrowserPersistentState state;
    if (!std::filesystem::is_regular_file(path)) return state;
    const nlohmann::json root = nlohmann::json::parse(
        FileSystem::read_text(path.string()), nullptr, false
    );
    if (root.is_discarded() || !root.is_object()) return state;
    try {
        if (root.value("version", 0) != 1) return state;
        state.view_mode = enum_value(root, "view_mode", state.view_mode, 2);
        state.collection = enum_value(root, "collection", state.collection, 2);
        state.scope = enum_value(root, "scope", state.scope, 1);
        state.type_filter = enum_value(root, "type_filter", state.type_filter, 12);
        state.status_filter = enum_value(root, "status_filter", state.status_filter, 2);
        state.sort = enum_value(root, "sort", state.sort, 3);
        state.sort_ascending = root.value("sort_ascending", state.sort_ascending);
        state.thumbnail_size = std::clamp(
            root.value("thumbnail_size", state.thumbnail_size), 48.0f, 112.0f
        );
        state.favorites = read_handles(root, "favorites");
        state.recent = read_handles(root, "recent");
    } catch (const nlohmann::json::exception&) {
        return {};
    }
    if (state.recent.size() > 32) state.recent.resize(32);
    return state;
}

bool AssetBrowserState::save(
    const std::filesystem::path& path,
    const AssetBrowserPersistentState& state
) {
    const nlohmann::json root{
        {"version", 1},
        {"view_mode", static_cast<int>(state.view_mode)},
        {"collection", static_cast<int>(state.collection)},
        {"scope", static_cast<int>(state.scope)},
        {"type_filter", static_cast<int>(state.type_filter)},
        {"status_filter", static_cast<int>(state.status_filter)},
        {"sort", static_cast<int>(state.sort)},
        {"sort_ascending", state.sort_ascending},
        {"thumbnail_size", std::clamp(state.thumbnail_size, 48.0f, 112.0f)},
        {"favorites", write_handles(state.favorites)},
        {"recent", write_handles(state.recent)}
    };
    return FileSystem::write_text_atomically(path.string(), root.dump(2) + "\n");
}

bool AssetBrowserState::contains(
    const std::vector<AssetHandle>& handles,
    AssetHandle handle
) {
    return handle.is_valid() && std::find(handles.begin(), handles.end(), handle) != handles.end();
}

bool AssetBrowserState::toggle_favorite(
    std::vector<AssetHandle>& favorites,
    AssetHandle handle
) {
    if (!handle.is_valid()) return false;
    const auto found = std::find(favorites.begin(), favorites.end(), handle);
    if (found == favorites.end()) {
        favorites.push_back(handle);
        return true;
    }
    favorites.erase(found);
    return false;
}

void AssetBrowserState::record_recent(
    std::vector<AssetHandle>& recent,
    AssetHandle handle,
    std::size_t maximum
) {
    if (!handle.is_valid() || maximum == 0) return;
    std::erase(recent, handle);
    recent.insert(recent.begin(), handle);
    if (recent.size() > maximum) recent.resize(maximum);
}

void AssetBrowserState::prune_missing(
    std::vector<AssetHandle>& handles,
    const AssetDatabase& database
) {
    std::erase_if(handles, [&database](AssetHandle handle) {
        return database.find(handle) == nullptr;
    });
}

} // namespace Engine
