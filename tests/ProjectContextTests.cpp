#include "core/ProjectContext.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void write(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << contents;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_project_context_tests";
    std::error_code error;
    fs::remove_all(root, error);
    write(root / "Assets/Scenes/Start.scene", "{}\n");
    write(root / "project.engine2d", R"({
  "version": 1,
  "name": "Test Project",
  "startup_scene": "Assets/Scenes/Start.scene"
})");

    std::string open_error;
    const auto project = Engine::ProjectContext::open(root, &open_error);
    assert(project && open_error.empty());
    assert(project->descriptor().name == "Test Project");
    assert(project->resolve("Assets/Scenes/Start.scene") == root / "Assets/Scenes/Start.scene");
    assert(fs::is_directory(project->settings_directory()));
    assert(fs::is_directory(project->library_directory()));

    write(root / "project.engine2d", R"({
  "version": 1,
  "name": "Invalid",
  "startup_scene": "../Outside.scene"
})");
    assert(!Engine::ProjectContext::open(root, &open_error));

    fs::remove_all(root, error);
    std::cout << "ProjectContextTests passed\n";
    return 0;
}
