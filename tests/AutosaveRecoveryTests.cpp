#include "editor/state/AutosaveRecovery.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    const fs::path root = fs::temp_directory_path() / "engine2d_autosave_recovery_tests";
    std::error_code error;
    fs::remove_all(root, error);

    Engine::AutosaveRecovery recovery(root.string());
    assert(recovery.discover().empty());
    assert(recovery.write_snapshot("assets/scenes/Test.scene", "assets/scenes/Test.scene", "Test", "{\"version\": 3}"));

    const auto records = recovery.discover();
    assert(records.size() == 1);
    assert(records.front().identity == "assets/scenes/Test.scene");
    assert(records.front().source_path == "assets/scenes/Test.scene");
    assert(records.front().scene_name == "Test");
    assert(records.front().snapshot_json == "{\"version\": 3}");

    const fs::path malformed = root / "malformed.autosave";
    std::ofstream(malformed) << "not json";
    assert(recovery.discover().size() == 1);

    assert(recovery.remove("assets/scenes/Test.scene"));
    assert(recovery.discover().empty());
    fs::remove_all(root, error);
    return 0;
}
