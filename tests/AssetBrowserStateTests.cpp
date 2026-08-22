#include "editor/tools/AssetBrowserState.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    namespace fs = std::filesystem;
    using namespace Engine;

    std::vector<AssetHandle> favorites;
    assert(AssetBrowserState::toggle_favorite(favorites, AssetHandle{11}));
    assert(AssetBrowserState::contains(favorites, AssetHandle{11}));
    assert(!AssetBrowserState::toggle_favorite(favorites, AssetHandle{11}));
    assert(favorites.empty());

    std::vector<AssetHandle> recent;
    AssetBrowserState::record_recent(recent, AssetHandle{1}, 3);
    AssetBrowserState::record_recent(recent, AssetHandle{2}, 3);
    AssetBrowserState::record_recent(recent, AssetHandle{1}, 3);
    AssetBrowserState::record_recent(recent, AssetHandle{3}, 3);
    AssetBrowserState::record_recent(recent, AssetHandle{4}, 3);
    assert((recent == std::vector<AssetHandle>{{4}, {3}, {1}}));

    const fs::path root = fs::temp_directory_path() / "engine2d_asset_browser_state_tests";
    std::error_code error;
    fs::remove_all(root, error);
    AssetBrowserPersistentState expected;
    expected.view_mode = AssetBrowserViewMode::Details;
    expected.collection = AssetBrowserCollection::Favorites;
    expected.scope = AssetBrowserScope::Project;
    expected.type_filter = AssetBrowserTypeFilter::Materials;
    expected.status_filter = AssetBrowserStatusFilter::Issues;
    expected.sort = AssetBrowserSort::Size;
    expected.sort_ascending = false;
    expected.thumbnail_size = 96.0f;
    expected.favorites = {AssetHandle{100}, AssetHandle{200}};
    expected.recent = {AssetHandle{300}};
    const fs::path preferences = root / "ProjectSettings/AssetBrowser.json";
    assert(AssetBrowserState::save(preferences, expected));
    const AssetBrowserPersistentState actual = AssetBrowserState::load(preferences);
    assert(actual.view_mode == expected.view_mode);
    assert(actual.collection == expected.collection);
    assert(actual.scope == expected.scope);
    assert(actual.type_filter == expected.type_filter);
    assert(actual.status_filter == expected.status_filter);
    assert(actual.sort == expected.sort);
    assert(actual.sort_ascending == expected.sort_ascending);
    assert(actual.thumbnail_size == expected.thumbnail_size);
    assert(actual.favorites == expected.favorites);
    assert(actual.recent == expected.recent);

    std::ofstream(preferences) << R"({"version":1,"view_mode":99,"thumbnail_size":999})";
    const AssetBrowserPersistentState sanitized = AssetBrowserState::load(preferences);
    assert(sanitized.view_mode == AssetBrowserViewMode::Grid);
    assert(sanitized.thumbnail_size == 112.0f);

    std::ofstream(preferences) << R"({"version":"invalid","view_mode":2})";
    const AssetBrowserPersistentState malformed = AssetBrowserState::load(preferences);
    assert(malformed.view_mode == AssetBrowserViewMode::Grid);
    assert(malformed.collection == AssetBrowserCollection::All);
    fs::remove_all(root, error);
    return 0;
}
