#include "editor/inspector/AssetDropResolver.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "AssetDropResolver test failed: " << message << '\n';
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
    using namespace Engine;

    const fs::path root = fs::temp_directory_path() / "engine2d_asset_drop_resolver_tests";
    std::error_code error;
    fs::remove_all(root, error);
    write_file(root / "assets/textures/Hero.png");
    write_file(root / "assets/fonts/Interface.ttf");
    write_file(root / "assets/scripts/Player.e2s", "@behaviour\nexport class Player extends Behaviour {}\n");
    write_file(root / "assets/data/Raw.blob");

    AssetDatabase database(root);
    [[maybe_unused]] const AssetScanResult scan = database.scan(root / "assets");

    const auto texture = resolve_asset_drop(
        database,
        root / "assets/textures/Hero.png",
        std::array{AssetType::Texture}
    );
    require(texture.accepted(), "absolute path resolves a compatible texture");
    require(texture.record && texture.record->type == AssetType::Texture,
        "accepted result exposes the database record");

    const auto relative_texture = resolve_asset_drop(
        database,
        "assets/textures/Hero.png",
        std::array{AssetType::Texture}
    );
    require(relative_texture.accepted(), "project-relative path resolves");

    const auto wrong_type = resolve_asset_drop(
        database,
        root / "assets/fonts/Interface.ttf",
        std::array{AssetType::Texture}
    );
    require(wrong_type.status == AssetDropStatus::IncompatibleType,
        "typed target rejects an incompatible asset");

    const auto generic_script = resolve_asset_drop(
        database,
        root / "assets/scripts/Player.e2s"
    );
    require(generic_script.accepted(), "generic asset field accepts a ready known type");

    const auto unknown_type = resolve_asset_drop(
        database,
        root / "assets/data/Raw.blob"
    );
    require(unknown_type.status == AssetDropStatus::IncompatibleType,
        "generic asset field rejects unknown types");

    fs::remove(root / "assets/textures/Hero.png", error);
    [[maybe_unused]] const AssetScanResult missing_scan = database.rescan();
    const auto missing = resolve_asset_drop(
        database,
        root / "assets/textures/Hero.png",
        std::array{AssetType::Texture}
    );
    require(missing.status == AssetDropStatus::NotReady,
        "missing source cannot be assigned even when its type matches");

    const auto outside = resolve_asset_drop(
        database,
        root.parent_path() / "Outside.png",
        std::array{AssetType::Texture}
    );
    require(outside.status == AssetDropStatus::NotFound,
        "unregistered path cannot be assigned");
    require(resolve_asset_drop(database, {}).status == AssetDropStatus::EmptyPath,
        "empty payload is rejected safely");

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
