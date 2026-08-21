#include "editor/state/RecentSceneList.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

std::filesystem::path make_test_directory() {
    const auto path = std::filesystem::temp_directory_path() / "engine2d_recent_scene_list_tests";
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path, error);
    assert(!error);
    return path;
}

void create_scene_file(const std::filesystem::path& path) {
    std::ofstream file(path);
    assert(file.is_open());
    file << "{}\n";
}

void test_records_most_recent_unique_paths() {
    const auto directory = make_test_directory();
    const auto first = directory / "first.scene";
    const auto second = directory / "second.scene";
    create_scene_file(first);
    create_scene_file(second);

    Engine::RecentSceneList recent;
    recent.record(first.string());
    recent.record(second.string());
    recent.record(first.string());

    assert(recent.paths().size() == 2);
    assert(recent.paths()[0] == first.lexically_normal().generic_string());
    assert(recent.paths()[1] == second.lexically_normal().generic_string());
}

void test_persists_and_discards_missing_paths() {
    const auto directory = make_test_directory();
    const auto existing = directory / "existing.scene";
    const auto missing = directory / "missing.scene";
    const auto state = directory / "recent.json";
    create_scene_file(existing);

    Engine::RecentSceneList recent;
    recent.record(missing.string());
    recent.record(existing.string());
    assert(recent.save(state.string()));

    Engine::RecentSceneList loaded;
    assert(loaded.load(state.string()));
    assert(loaded.remove_missing_files());
    assert(loaded.paths().size() == 1);
    assert(loaded.paths()[0] == existing.lexically_normal().generic_string());

    const auto newer = directory / "newer.scene";
    create_scene_file(newer);
    loaded.record(newer.string());
    assert(loaded.save(state.string()));

    Engine::RecentSceneList reloaded;
    assert(reloaded.load(state.string()));
    assert(reloaded.paths().size() == 2);
    assert(reloaded.paths()[0] == newer.lexically_normal().generic_string());
    assert(reloaded.paths()[1] == existing.lexically_normal().generic_string());
}

void test_limits_the_list_to_maximum_entries() {
    Engine::RecentSceneList recent;
    for (std::size_t index = 0; index < Engine::RecentSceneList::MAX_ENTRIES + 3; ++index) {
        recent.record("assets/scenes/level_" + std::to_string(index) + ".scene");
    }

    assert(recent.paths().size() == Engine::RecentSceneList::MAX_ENTRIES);
    assert(recent.paths().front() == "assets/scenes/level_12.scene");
    assert(recent.paths().back() == "assets/scenes/level_3.scene");
}

} // namespace

int main() {
    test_records_most_recent_unique_paths();
    test_persists_and_discards_missing_paths();
    test_limits_the_list_to_maximum_entries();
    std::cout << "RecentSceneListTests passed\n";
    return 0;
}
