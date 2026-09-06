#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"
#include "core/LevelPaletteAsset.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using namespace Engine;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path temporary_project() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("engine2d_level_palette_" + std::to_string(stamp));
    std::filesystem::create_directories(path / "Assets/Textures");
    std::filesystem::create_directories(path / "Assets/Prefabs");
    std::filesystem::create_directories(path / "Assets/LevelPalettes");
    std::filesystem::create_directories(path / "ProjectSettings");
    return path;
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << contents;
    require(stream.good(), "test fixture must be writable");
}

LevelPaletteAsset make_palette(
    AssetHandle texture_handle = {},
    AssetHandle prefab_handle = {}
) {
    LevelPaletteAsset palette;
    palette.name = "Ruined Sanctuary";
    palette.grid_origin = {-32.0f, 16.0f};
    palette.categories = {
        {"floors", "Floors"},
        {"architecture", "Architecture"}
    };

    LevelPaletteEntry floor;
    floor.id = "floor.cracked";
    floor.label = "Cracked Floor";
    floor.category = "floors";
    floor.kind = LevelPaletteEntryKind::SpriteRegion;
    floor.sprite_region.texture = {
        texture_handle,
        "Assets/Textures/sanctuary.png"
    };
    floor.sprite_region.rect_px = {256, 0, 256, 256};
    floor.sprite_region.world_size = {128.0f, 128.0f};
    floor.sprite_region.pivot = {0.5f, 0.5f};
    floor.placement.snap = {128.0f, 128.0f};
    floor.placement.spacing = 128.0f;
    floor.placement.rotation_degrees = 90.0f;
    floor.placement.depth_z = -2.5f;
    floor.placement.flip_x = true;
    floor.placement.sorting_layer = -2;
    floor.placement.order_in_layer = 7;
    floor.placement.render_layer = 3;

    LevelPaletteEntry wall;
    wall.id = "wall.straight";
    wall.label = "Straight Wall";
    wall.category = "architecture";
    wall.kind = LevelPaletteEntryKind::Prefab;
    wall.prefab.prefab = {
        prefab_handle,
        "Assets/Prefabs/wall.prefab"
    };
    wall.placement.snap = {64.0f, 64.0f};
    wall.placement.spacing = 192.0f;
    wall.placement.rotation_degrees = 180.0f;
    wall.placement.depth_z = 1.25f;
    wall.placement.flip_y = true;
    wall.placement.sorting_layer = 1;
    wall.placement.order_in_layer = -4;
    wall.placement.render_layer = 5;

    palette.entries = {floor, wall};
    return palette;
}

void test_json_round_trip() {
    const LevelPaletteAsset expected = make_palette(AssetHandle{0x1234}, AssetHandle{0x5678});
    std::string error;
    require(expected.validate(&error), "valid palette must pass structural validation");
    const auto restored = LevelPaletteAsset::from_json(expected.to_json(), &error);
    require(restored.has_value(), "serialized palette must load");
    require(error.empty(), "valid palette must not leave an error");
    require(*restored == expected, "palette JSON round-trip must preserve every field");
    require(restored->find_entry("wall.straight") != nullptr, "entry lookup must find exact ids");
    require(restored->find_entry("wall") == nullptr, "entry lookup must not use partial matches");

    const auto document = expected.to_json();
    require(document.at("entries").at(0).contains("texture_handle"),
            "texture references must use dependency-discoverable handle keys");
    require(document.at("entries").at(1).contains("prefab_handle"),
            "prefab references must use dependency-discoverable handle keys");
    require(document.at("entries").at(0).at("placement").at("depth_z") == -2.5f,
            "depth_z must be serialized in placement data");

    auto legacy_v1 = document;
    legacy_v1.at("entries").at(0).at("placement").erase("depth_z");
    legacy_v1.at("entries").at(1).at("placement").erase("depth_z");
    const auto restored_legacy = LevelPaletteAsset::from_json(legacy_v1, &error);
    require(restored_legacy.has_value(), "v1 palettes without depth_z must remain compatible");
    require(restored_legacy->entries[0].placement.depth_z == 0.0f &&
                restored_legacy->entries[1].placement.depth_z == 0.0f,
            "missing depth_z must default to zero");
}

void test_validation_rejects_invalid_contracts() {
    std::string error;
    LevelPaletteAsset palette = make_palette();

    auto duplicate_category = palette;
    duplicate_category.categories.push_back(duplicate_category.categories.front());
    require(!duplicate_category.validate(&error) && error.find("duplicated") != std::string::npos,
            "duplicate category ids must be rejected");

    auto duplicate_entry = palette;
    duplicate_entry.entries.push_back(duplicate_entry.entries.front());
    require(!duplicate_entry.validate(&error) && error.find("duplicated") != std::string::npos,
            "duplicate entry ids must be rejected");

    auto missing_category = palette;
    missing_category.entries.front().category = "missing";
    require(!missing_category.validate(&error) && error.find("unknown category") != std::string::npos,
            "entries must reference declared categories");

    auto bad_rect = palette;
    bad_rect.entries.front().sprite_region.rect_px.z = 0;
    require(!bad_rect.validate(&error) && error.find("rect") != std::string::npos,
            "empty sprite rectangles must be rejected");

    auto bad_pivot = palette;
    bad_pivot.entries.front().sprite_region.pivot.x = 1.5f;
    require(!bad_pivot.validate(&error) && error.find("pivot") != std::string::npos,
            "out-of-range pivots must be rejected");

    auto traversal = palette;
    traversal.entries.front().sprite_region.texture.path = "Assets/../secret.png";
    require(!traversal.validate(&error) && error.find("project-relative") != std::string::npos,
            "asset path traversal must be rejected");

    auto invalid_layer = palette;
    invalid_layer.entries.front().placement.render_layer = 32;
    require(!invalid_layer.validate(&error) && error.find("placement") != std::string::npos,
            "render layers outside 0..31 must be rejected");

    auto invalid_depth = palette;
    invalid_depth.entries.front().placement.depth_z =
        std::numeric_limits<float>::quiet_NaN();
    require(!invalid_depth.validate(&error) && error.find("placement") != std::string::npos,
            "non-finite depth_z must be rejected");

    auto excessive_depth = palette;
    excessive_depth.entries.front().placement.depth_z = 1'000'001.0f;
    require(!excessive_depth.validate(&error) && error.find("placement") != std::string::npos,
            "depth_z outside the supported world range must be rejected");

    auto future = palette.to_json();
    future["version"] = 2;
    require(!LevelPaletteAsset::from_json(future, &error) && error.find("version") != std::string::npos,
            "future schema versions must be rejected");

    auto missing_placement = palette.to_json();
    missing_placement["entries"][0]["placement"].erase("spacing");
    require(!LevelPaletteAsset::from_json(missing_placement, &error) &&
                error.find("spacing") != std::string::npos,
            "missing required placement fields must be rejected");
}

void test_file_io_and_handle_canonicalization(const std::filesystem::path& root) {
    const auto texture = root / "Assets/Textures/sanctuary.png";
    const auto prefab = root / "Assets/Prefabs/wall.prefab";
    write_file(texture, "texture fixture");
    write_file(prefab, "{}\n");

    AssetDatabase database(root);
    const auto texture_handle = database.import_asset(texture);
    const auto prefab_handle = database.import_asset(prefab);
    require(texture_handle && prefab_handle, "fixture assets must import");

    LevelPaletteAsset palette = make_palette({}, {});
    const auto palette_path = root / "Assets/LevelPalettes/Sanctuary.levelpalette";
    std::string error;
    require(palette.save_to_file(palette_path, &error, &database),
            "palette save must resolve paths to stable handles");
    require(error.empty(), "successful save must clear the error");

    auto loaded = LevelPaletteAsset::load_from_file(palette_path, &error, &database);
    require(loaded.has_value(), "saved palette must load with database validation");
    require(loaded->entries[0].sprite_region.texture.handle == *texture_handle,
            "path-backed texture must be upgraded to a stable handle");
    require(loaded->entries[1].prefab.prefab.handle == *prefab_handle,
            "path-backed prefab must be upgraded to a stable handle");

    AssetHandle stale_texture_handle{1};
    while (database.find(stale_texture_handle)) ++stale_texture_handle.value;
    LevelPaletteAsset stale_handle_palette = make_palette(
        stale_texture_handle, *prefab_handle);
    require(stale_handle_palette.validate(&error, &database),
            "a stale handle remains valid when its diagnostic path resolves");
    require(stale_handle_palette.canonicalize_references(database, &error),
            "canonicalization must recover a stale handle through its valid path");
    require(stale_handle_palette.entries[0].sprite_region.texture.handle == *texture_handle,
            "canonicalization replaces a stale handle with the resolved asset handle");
    require(stale_handle_palette.entries[0].sprite_region.texture.path ==
                "Assets/Textures/sanctuary.png",
            "canonicalization preserves the resolved project-relative path");

    const auto moved_texture = root / "Assets/Textures/Environment/sanctuary.png";
    std::filesystem::create_directories(moved_texture.parent_path());
    std::error_code move_error;
    std::filesystem::rename(texture, moved_texture, move_error);
    require(!move_error, "fixture texture must move on disk before updating the database");
    require(database.move_asset(texture, moved_texture), "fixture texture move must succeed");
    loaded = LevelPaletteAsset::load_from_file(palette_path, &error, &database);
    require(loaded.has_value(), "handle-backed palette must survive an asset move");
    require(loaded->entries[0].sprite_region.texture.handle == *texture_handle,
            "asset move must preserve the texture handle");
    require(loaded->entries[0].sprite_region.texture.path ==
                "Assets/Textures/Environment/sanctuary.png",
            "load must refresh the diagnostic path from the stable handle");

    auto wrong_type = *loaded;
    wrong_type.entries[0].sprite_region.texture.handle = *prefab_handle;
    wrong_type.entries[0].sprite_region.texture.path.clear();
    require(!wrong_type.validate(&error, &database) && error.find("wrong asset type") != std::string::npos,
            "database validation must reject a prefab used as a texture");

    LevelPaletteAsset transactional = make_palette({}, {});
    transactional.entries[1].prefab.prefab.path = "Assets/Prefabs/missing.prefab";
    const LevelPaletteAsset before_failed_canonicalization = transactional;
    const auto repairable_path = root / "Assets/LevelPalettes/Repairable.levelpalette";
    write_file(repairable_path, transactional.to_json().dump());
    require(LevelPaletteAsset::load_from_file(repairable_path, &error).has_value(),
            "authoring load without a database must preserve repairable missing references");
    require(!LevelPaletteAsset::load_from_file(repairable_path, &error, &database),
            "strict database load must reject unresolved references");
    require(!transactional.canonicalize_references(database, &error),
            "missing references must fail canonicalization");
    require(transactional == before_failed_canonicalization,
            "failed canonicalization must leave every reference unchanged");

    require(!palette.save_to_file(root / "Assets/LevelPalettes/invalid.json", &error),
            "palette save must enforce its extension");

    const auto wrong_extension = root / "Assets/LevelPalettes/invalid.json";
    write_file(wrong_extension, palette.to_json().dump());
    require(!LevelPaletteAsset::load_from_file(wrong_extension, &error),
            "palette load must enforce its extension");

    const auto malformed_path = root / "Assets/LevelPalettes/Malformed.levelpalette";
    write_file(malformed_path, "{not-json");
    require(!LevelPaletteAsset::load_from_file(malformed_path, &error) &&
                error.find("parsing") != std::string::npos,
            "malformed level palette JSON must be rejected without throwing");
}

} // namespace

int main() {
    const auto root = temporary_project();
    try {
        FileSystem::init("Engine2D", "LevelPaletteTests", true, root);
        test_json_round_trip();
        test_validation_rejects_invalid_contracts();
        test_file_io_and_handle_canonicalization(root);
        std::filesystem::remove_all(root);
        std::cout << "LevelPaletteTests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root);
        std::cerr << "LevelPaletteTests failed: " << exception.what() << '\n';
        return 1;
    }
}
