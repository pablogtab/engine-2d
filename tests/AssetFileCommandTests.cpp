#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"
#include "editor/commands/AssetFileCommands.hpp"
#include "editor/commands/EditorCommandGroup.hpp"
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
    fs::create_directories(root / "Assets/scenes", error);

    Engine::AssetDatabase database(root);
    Engine::EditorCommandHistory history;
    const fs::path scene = root / "Assets/scenes/Created.scene";
    const fs::path renamed = root / "Assets/scenes/Renamed.scene";

    history.execute_command(std::make_unique<Engine::CreateAssetCommand>(
        &database, scene, "{\"scene_name\":\"Created\"}\n"));
    require(fs::exists(scene) && fs::exists(root / "ProjectSettings/AssetDatabase.json"), "create makes asset and central record");
    const auto* created = database.find_by_path(scene);
    require(created != nullptr, "create imports asset");
    const Engine::AssetHandle handle = created->handle;
    const std::string metadata = read(root / "ProjectSettings/AssetDatabase.json");

    history.undo();
    require(!fs::exists(scene), "undo create removes source asset");
    history.redo();
    require(fs::exists(scene) && fs::exists(root / "ProjectSettings/AssetDatabase.json"), "redo create restores source and central record");
    require(database.find_by_path(scene)->handle == handle, "redo create preserves handle");

    history.execute_command(std::make_unique<Engine::MoveAssetCommand>(&database, scene, renamed));
    require(!fs::exists(scene) && fs::exists(renamed), "move relocates source");
    require(database.resolve(handle) == renamed.string(), "move relocates central record");
    require(database.find(handle) != nullptr && database.resolve(handle) == renamed.string(), "move preserves handle resolution");
    history.undo();
    require(fs::exists(scene) && database.resolve(handle) == scene.string(), "undo move restores pair and handle");
    history.redo();

    const fs::path texture = root / "Assets/generated/texture.png";
    const std::vector<std::uint8_t> png_bytes{137, 80, 78, 71, 13, 10, 26, 10, 1, 2, 3, 4};
    history.execute_command(std::make_unique<Engine::CreateBinaryAssetCommand>(
        &database, texture, png_bytes
    ));
    require(fs::exists(texture), "binary create writes source asset");
    require(database.find_by_path(texture) != nullptr, "binary create imports asset");
    require(database.find_by_path(texture)->type == Engine::AssetType::Texture, "binary create detects texture type");
    const Engine::AssetHandle texture_handle = database.find_by_path(texture)->handle;
    history.undo();
    require(!fs::exists(texture), "undo binary create removes source asset");
    history.redo();
    require(Engine::FileSystem::read_bytes(texture.string()) == png_bytes, "redo binary create restores exact bytes");
    require(database.find_by_path(texture)->handle == texture_handle, "redo binary create preserves handle");

    const fs::path abandoned_texture = root / "Assets/generated/abandoned.png";
    {
        auto abandoned = std::make_unique<Engine::CreateBinaryAssetCommand>(
            &database, abandoned_texture, png_bytes
        );
        require(abandoned->execute(), "uncommitted binary create executes");
        require(abandoned->undo(), "uncommitted binary create rolls back");
        require(
            database.find_by_path(abandoned_texture) != nullptr,
            "rollback retains identity while command can still redo"
        );
    }
    require(
        database.find_by_path(abandoned_texture) == nullptr,
        "abandoned binary create removes its missing database record"
    );

    const std::string before_modify = read(renamed);
    const std::string after_modify = "{\"scene_name\":\"Modified\"}\n";
    int before_callbacks = 0;
    int after_callbacks = 0;
    history.execute_command(std::make_unique<Engine::ModifyAssetCommand>(
        &database,
        renamed,
        before_modify,
        after_modify,
        "Modify Scene Asset",
        [&]() { ++before_callbacks; },
        [&]() { ++after_callbacks; }
    ));
    require(read(renamed) == after_modify, "modify writes new asset content");
    require(after_callbacks == 1 && before_callbacks == 0, "modify applies after-state callback");
    require(database.find_by_path(renamed)->handle == handle, "modify preserves handle");
    history.undo();
    require(read(renamed) == before_modify, "undo modify restores previous content");
    require(before_callbacks == 1, "undo modify applies before-state callback");
    history.redo();
    require(read(renamed) == after_modify, "redo modify restores new content");
    require(after_callbacks == 2, "redo modify reapplies after-state callback");

    history.execute_command(std::make_unique<Engine::DeleteAssetCommand>(&database, renamed));
    require(!fs::exists(renamed), "delete removes source asset");
    history.undo();
    require(fs::exists(renamed), "undo delete restores source asset");
    require(fs::exists(root / "ProjectSettings/AssetDatabase.json"), "undo delete preserves central database");
    require(database.resolve(handle) == renamed.string(), "undo delete restores handle resolution");

    const fs::path bulk_source = root / "Assets/bulk_source";
    const fs::path bulk_destination = root / "Assets/bulk_destination";
    fs::create_directories(bulk_source, error);
    fs::create_directories(bulk_destination, error);
    const fs::path first = bulk_source / "First.scene";
    const fs::path second = bulk_source / "Second.scene";
    history.execute_command(std::make_unique<Engine::CreateAssetCommand>(
        &database, first, "{\"scene_name\":\"First\"}\n"
    ));
    history.execute_command(std::make_unique<Engine::CreateAssetCommand>(
        &database, second, "{\"scene_name\":\"Second\"}\n"
    ));
    const Engine::AssetHandle first_handle = database.find_by_path(first)->handle;
    const Engine::AssetHandle second_handle = database.find_by_path(second)->handle;

    auto bulk_move = std::make_unique<Engine::EditorCommandGroup>("Move Selected Assets");
    bulk_move->add_command(std::make_unique<Engine::MoveAssetCommand>(
        &database, first, bulk_destination / first.filename()
    ));
    bulk_move->add_command(std::make_unique<Engine::MoveAssetCommand>(
        &database, second, bulk_destination / second.filename()
    ));
    require(history.execute_command(std::move(bulk_move)), "bulk move executes atomically");
    require(!fs::exists(first) && !fs::exists(second), "bulk move removes both sources");
    require(
        database.resolve(first_handle) == (bulk_destination / first.filename()).string() &&
        database.resolve(second_handle) == (bulk_destination / second.filename()).string(),
        "bulk move preserves both handles"
    );
    history.undo();
    require(fs::exists(first) && fs::exists(second), "undo bulk move restores both assets");
    history.redo();
    require(
        fs::exists(bulk_destination / first.filename()) &&
        fs::exists(bulk_destination / second.filename()),
        "redo bulk move relocates both assets"
    );

    auto bulk_delete = std::make_unique<Engine::EditorCommandGroup>("Delete Selected Assets");
    bulk_delete->add_command(std::make_unique<Engine::DeleteAssetCommand>(
        &database, bulk_destination / first.filename()
    ));
    bulk_delete->add_command(std::make_unique<Engine::DeleteAssetCommand>(
        &database, bulk_destination / second.filename()
    ));
    require(history.execute_command(std::move(bulk_delete)), "bulk delete executes atomically");
    require(
        !fs::exists(bulk_destination / first.filename()) &&
        !fs::exists(bulk_destination / second.filename()),
        "bulk delete removes both assets"
    );
    history.undo();
    require(
        fs::exists(bulk_destination / first.filename()) &&
        fs::exists(bulk_destination / second.filename()),
        "undo bulk delete restores both assets"
    );

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
