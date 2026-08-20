#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "AssetDatabase test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void write_file(const std::filesystem::path& path, const std::string& text = "asset") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path);
    stream << text;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_asset_database_tests";
    std::error_code error;
    fs::remove_all(root, error);
    write_file(root / "assets/prefabs/Hero.prefab", "{}");
    write_file(root / "assets/textures/Hero.PNG");
    write_file(root / "assets/shaders/effect.frag.msl");

    Engine::AssetDatabase database(root);
    const auto initial = database.scan(root / "assets");
    require(initial.imported == 3, "scan imports source assets into central database");
    require(initial.ready == 3, "all imported assets become ready");
    require(fs::exists(root / "ProjectSettings/AssetDatabase.json"), "central asset database is created");

    const auto* prefab = database.find_by_path(root / "assets/prefabs/Hero.prefab");
    require(prefab != nullptr, "path resolves to record");
    require(prefab->type == Engine::AssetType::Prefab, "prefab type inferred");
    require(database.resolve(prefab->handle) == (root / "assets/prefabs/Hero.prefab").string(), "handle resolves source path");
    const auto parsed = Engine::AssetHandle::from_string(prefab->handle.to_string());
    require(parsed.has_value() && *parsed == prefab->handle, "handle string round trip");

    const Engine::AssetHandle stable_handle = prefab->handle;
    const auto again = database.scan(root / "assets");
    require(again.imported == 0 && again.ready == 3, "second scan reuses central metadata");
    require(database.find_by_path(root / "assets/prefabs/Hero.prefab")->handle == stable_handle, "central database keeps handle stable");

    write_file(root / "assets/audio/theme.wav");
    const auto imported_relative = database.import_asset("assets/audio/theme.wav");
    require(imported_relative.has_value(), "project-relative source paths resolve against project root");
    require(database.find(*imported_relative)->type == Engine::AssetType::Audio, "relative import infers asset type");

    const fs::path outside = root.parent_path() / "engine2d_asset_database_outside.prefab";
    write_file(outside, "{}");
    require(!database.import_asset(outside).has_value(), "outside project asset rejected");

    fs::remove(root / "assets/prefabs/Hero.prefab", error);
    const auto missing = database.scan(root / "assets");
    require(missing.ready == 3, "missing asset is retained but excluded from ready count");
    require(database.find(stable_handle)->status == Engine::AssetStatus::MissingSource, "lost source remains repairable by handle");

    fs::remove_all(root, error);
    fs::remove(outside, error);
    return EXIT_SUCCESS;
}
