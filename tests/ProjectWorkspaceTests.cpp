#include "core/ProjectContext.hpp"
#include "core/ProjectWorkspace.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() / ("engine2d-project-workspace-tests-" + std::to_string(nonce));
        fs::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

void test_creates_complete_valid_project() {
    TemporaryDirectory temporary;
    std::string error;
    const auto created = Engine::ProjectWorkspace::create(temporary.path(), "First Game", &error);
    assert(created.has_value());
    assert(error.empty());
    assert(fs::is_regular_file(*created / "project.engine2d"));
    assert(fs::is_regular_file(*created / "Assets/Scenes/Main.scene"));
    assert(fs::is_regular_file(*created / "ProjectSettings/Project.json"));
    assert(fs::is_regular_file(*created / "ProjectSettings/AssetDatabase.json"));
    assert(fs::is_directory(*created / "Assets/Scripts"));
    assert(fs::is_directory(*created / "Library"));
    assert(fs::is_directory(*created / "Saves"));

    const auto project = Engine::ProjectContext::open(*created, &error);
    assert(project.has_value());
    assert(project->descriptor().name == "First Game");
    assert(project->descriptor().startup_scene == "Assets/Scenes/Main.scene");
}

void test_rejects_unsafe_or_existing_destination() {
    TemporaryDirectory temporary;
    std::string error;
    assert(!Engine::ProjectWorkspace::create(temporary.path(), "../escape", &error));
    assert(!error.empty());

    error.clear();
    fs::create_directories(temporary.path() / "Existing");
    std::ofstream(temporary.path() / "Existing/keep.txt") << "keep";
    assert(!Engine::ProjectWorkspace::create(temporary.path(), "Existing", &error));
    assert(fs::is_regular_file(temporary.path() / "Existing/keep.txt"));
}

void test_registry_deduplicates_persists_and_prunes() {
    TemporaryDirectory temporary;
    const fs::path first = temporary.path() / "First";
    const fs::path second = temporary.path() / "Second";
    fs::create_directories(first);
    fs::create_directories(second);
    std::ofstream(first / "project.engine2d") << "{}";
    std::ofstream(second / "project.engine2d") << "{}";

    Engine::ProjectRegistry registry;
    registry.record({"First", first});
    registry.record({"Second", second});
    registry.record({"First Renamed", first / "."});
    assert(registry.projects().size() == 2);
    assert(registry.projects().front().name == "First Renamed");

    const fs::path registry_path = temporary.path() / "preferences/projects.json";
    std::string error;
    assert(registry.save(registry_path, &error));

    Engine::ProjectRegistry loaded;
    assert(loaded.load(registry_path, &error));
    assert(loaded.projects().size() == 2);
    assert(loaded.projects().front().name == "First Renamed");

    fs::remove(first / "project.engine2d");
    assert(loaded.remove_missing_projects());
    assert(loaded.projects().size() == 1);
    assert(loaded.projects().front().name == "Second");
    loaded.remove(second);
    assert(loaded.projects().empty());
}

} // namespace

int main() {
    test_creates_complete_valid_project();
    test_rejects_unsafe_or_existing_destination();
    test_registry_deduplicates_persists_and_prunes();
    std::cout << "ProjectWorkspaceTests passed\n";
    return 0;
}
