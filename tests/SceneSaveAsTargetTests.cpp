#include "editor/state/SceneSaveAsTarget.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    const fs::path root = fs::temp_directory_path() / "engine2d_scene_save_as_target_tests";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    const auto empty = Engine::resolve_scene_save_as_target(root, "   ");
    assert(empty.issue == Engine::SceneSaveAsIssue::EmptyName);
    assert(!empty.can_save());

    assert(Engine::resolve_scene_save_as_target(root, "../Outside").issue ==
        Engine::SceneSaveAsIssue::InvalidName);
    assert(Engine::resolve_scene_save_as_target(root, "Folder/Scene").issue ==
        Engine::SceneSaveAsIssue::InvalidName);
    assert(Engine::resolve_scene_save_as_target(root, "Scene:Windows").issue ==
        Engine::SceneSaveAsIssue::InvalidName);
    assert(Engine::resolve_scene_save_as_target(root, "Scene?.scene").issue ==
        Engine::SceneSaveAsIssue::InvalidName);

    const auto valid = Engine::resolve_scene_save_as_target(root, "NewLevel");
    assert(valid.can_save());
    assert(valid.filename == "NewLevel.scene");
    assert(valid.path == root / "NewLevel.scene");

    std::ofstream(valid.path) << "existing scene";
    const auto existing = Engine::resolve_scene_save_as_target(root, "NewLevel.scene");
    assert(existing.issue == Engine::SceneSaveAsIssue::DestinationExists);
    assert(!existing.can_save());

    fs::remove_all(root, error);
    return 0;
}
