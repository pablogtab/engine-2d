#include "core/AssetDatabase.hpp"
#include "editor/commands/AssetFileCommands.hpp"
#include "editor/commands/EditorCommandHistory.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "AssetFileCommand test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_asset_file_command_tests";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "assets/scenes", error);

    Engine::AssetDatabase database(root);
    Engine::EditorCommandHistory history;
    const fs::path scene = root / "assets/scenes/Created.scene";
    const fs::path renamed = root / "assets/scenes/Renamed.scene";

    history.execute_command(std::make_unique<Engine::CreateAssetCommand>(
        &database, scene, "{\"scene_name\":\"Created\"}\n"));
    require(fs::exists(scene) && fs::exists(root / "Library/AssetDatabase.json"), "create makes asset and central record");
    const auto* created = database.find_by_path(scene);
    require(created != nullptr, "create imports asset");
    const Engine::AssetHandle handle = created->handle;
    const std::string metadata = read(root / "Library/AssetDatabase.json");

    history.undo();
    require(!fs::exists(scene), "undo create removes source asset");
    history.redo();
    require(fs::exists(scene) && fs::exists(root / "Library/AssetDatabase.json"), "redo create restores source and central record");
    require(database.find_by_path(scene)->handle == handle, "redo create preserves handle");

    history.execute_command(std::make_unique<Engine::MoveAssetCommand>(&database, scene, renamed));
    require(!fs::exists(scene) && fs::exists(renamed), "move relocates source");
    require(database.resolve(handle) == renamed.string(), "move relocates central record");
    require(database.find(handle) != nullptr && database.resolve(handle) == renamed.string(), "move preserves handle resolution");
    history.undo();
    require(fs::exists(scene) && database.resolve(handle) == scene.string(), "undo move restores pair and handle");
    history.redo();

    history.execute_command(std::make_unique<Engine::DeleteAssetCommand>(&database, renamed));
    require(!fs::exists(renamed), "delete removes source asset");
    history.undo();
    require(fs::exists(renamed), "undo delete restores source asset");
    require(fs::exists(root / "Library/AssetDatabase.json"), "undo delete preserves central database");
    require(database.resolve(handle) == renamed.string(), "undo delete restores handle resolution");

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
