#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"
#include "core/LevelPaletteAsset.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

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

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

Engine::LevelPaletteAsset make_texture_palette(
    Engine::AssetHandle texture_handle,
    std::string texture_path
) {
    Engine::LevelPaletteAsset palette;
    palette.name = "Sanctuary";
    palette.categories = {{"architecture", "Architecture"}};

    Engine::LevelPaletteEntry entry;
    entry.id = "wall.back";
    entry.label = "Back Wall";
    entry.category = "architecture";
    entry.kind = Engine::LevelPaletteEntryKind::SpriteRegion;
    entry.sprite_region.texture = {texture_handle, std::move(texture_path)};
    entry.sprite_region.rect_px = {0, 0, 64, 128};
    entry.sprite_region.world_size = {64.0f, 128.0f};
    entry.sprite_region.pivot = {0.5f, 1.0f};
    entry.placement.snap = {64.0f, 64.0f};
    entry.placement.spacing = 64.0f;
    palette.entries.push_back(std::move(entry));
    return palette;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    require(Engine::AssetDatabase::type_for_path("PlayerController.e2s") == Engine::AssetType::Script,
        "EngineScript files are recognized as script assets");
    require(Engine::AssetDatabase::type_for_path("Sanctuary.LEVELPALETTE") ==
            Engine::AssetType::LevelPalette,
        "level palette files are recognized case-insensitively");
    require(std::string(Engine::AssetDatabase::type_name(Engine::AssetType::LevelPalette)) ==
            "level_palette",
        "level palette type has a stable persisted name");
    const fs::path root = fs::temp_directory_path() / "engine2d_asset_database_tests";
    std::error_code error;
    fs::remove_all(root, error);
    write_file(root / "assets/prefabs/Hero.prefab", "{}");
    write_file(root / "assets/textures/Hero.PNG");
    write_file(root / "assets/fonts/Interface.ttf");
    write_file(root / "assets/shaders/effect.frag.msl");

    Engine::AssetDatabase database(root);
    const auto initial = database.scan(root / "assets");
    require(initial.imported == 4, "scan imports source assets into central database");
    require(initial.ready == 4, "all imported assets become ready");
    require(fs::exists(root / "ProjectSettings/AssetDatabase.json"), "central asset database is created");

    const auto* prefab = database.find_by_path(root / "assets/prefabs/Hero.prefab");
    require(prefab != nullptr, "path resolves to record");
    require(prefab->type == Engine::AssetType::Prefab, "prefab type inferred");
    require(database.find_by_path(root / "assets/fonts/Interface.ttf")->type == Engine::AssetType::Font,
        "font type inferred");
    require(database.resolve(prefab->handle) == (root / "assets/prefabs/Hero.prefab").string(), "handle resolves source path");
    const auto parsed = Engine::AssetHandle::from_string(prefab->handle.to_string());
    require(parsed.has_value() && *parsed == prefab->handle, "handle string round trip");

    const Engine::AssetHandle stable_handle = prefab->handle;
    const auto again = database.scan(root / "assets");
    require(again.imported == 0 && again.ready == 4, "second scan reuses central metadata");
    require(database.find_by_path(root / "assets/prefabs/Hero.prefab")->handle == stable_handle, "central database keeps handle stable");

    write_file(root / "assets/audio/theme.wav");
    const auto imported_relative = database.import_asset("assets/audio/theme.wav");
    require(imported_relative.has_value(), "project-relative source paths resolve against project root");
    require(database.find(*imported_relative)->type == Engine::AssetType::Audio, "relative import infers asset type");

    const fs::path moved_theme = root / "assets/audio/music/theme.wav";
    fs::create_directories(moved_theme.parent_path());
    fs::rename(root / "assets/audio/theme.wav", moved_theme, error);
    require(!error && database.move_asset("assets/audio/theme.wav", "assets/audio/music/theme.wav"),
        "moving an asset preserves its central record");
    require(database.find(*imported_relative)->source_path == "assets/audio/music/theme.wav",
        "moved asset keeps its handle while changing source path");
    Engine::AssetDatabase reloaded_database(root);
    [[maybe_unused]] const auto moved_rescan = reloaded_database.scan(root / "assets");
    require(reloaded_database.find_by_path("assets/audio/music/theme.wav")->handle == *imported_relative,
        "moved asset handle persists after database reload");
    Engine::AssetImportSettings import_settings;
    import_settings.filter = "nearest";
    import_settings.pixels_per_unit = 32.0f;
    import_settings.pivot_x = 0.25f;
    import_settings.compression = "lossless";
    import_settings.platform_overrides.emplace("windows", "bc7");
    require(reloaded_database.update_import_settings(*imported_relative, import_settings), "asset import settings save atomically");
    Engine::AssetDatabase settings_reloaded_database(root);
    [[maybe_unused]] const auto settings_rescan = settings_reloaded_database.scan(root / "assets");
    const auto* configured_theme = settings_reloaded_database.find(*imported_relative);
    require(configured_theme && configured_theme->import_settings.filter == "nearest" &&
        configured_theme->import_settings.pixels_per_unit == 32.0f &&
        configured_theme->import_settings.platform_overrides.at("windows") == "bc7",
        "import settings persist across reload");

    const fs::path outside = root.parent_path() / "engine2d_asset_database_outside.prefab";
    write_file(outside, "{}");
    require(!database.import_asset(outside).has_value(), "outside project asset rejected");

    fs::remove(root / "assets/prefabs/Hero.prefab", error);
    const auto missing = database.scan(root / "assets");
    require(missing.ready == 4, "missing asset is retained but excluded from ready count");
    require(database.find(stable_handle)->status == Engine::AssetStatus::MissingSource, "lost source remains repairable by handle");

    fs::remove_all(root, error);
    fs::remove(outside, error);

    const fs::path readonly_root = fs::temp_directory_path() / "engine2d_readonly_asset_database_tests";
    fs::remove_all(readonly_root, error);
    write_file(readonly_root / "Assets/Scenes/Startup.scene", "{}");
    Engine::AssetDatabase readonly_database(readonly_root);
    [[maybe_unused]] const auto writable_scan = readonly_database.scan("Assets");
    const fs::path readonly_database_path = readonly_database.database_path();
    const std::string original_database = read_file(readonly_database_path);
    write_file(readonly_root / "Assets/Textures/New.png");
    readonly_database.set_read_only(true);
    const auto readonly_scan = readonly_database.scan("Assets");
    require(readonly_scan.imported == 1, "read-only scan still discovers packaged assets in memory");
    require(readonly_database.find_by_path("Assets/Textures/New.png") != nullptr,
        "read-only scan exposes newly discovered assets");
    require(read_file(readonly_database_path) == original_database,
        "read-only scan does not rewrite packaged asset metadata");
    fs::remove_all(readonly_root, error);

    const fs::path dependency_root = fs::temp_directory_path() / "engine2d_asset_dependency_tests";
    fs::remove_all(dependency_root, error);
    write_file(dependency_root / "assets/shaders/Test.msl", "fragment float4 main0() { return float4(1); }");
    Engine::AssetDatabase dependency_database(dependency_root);
    [[maybe_unused]] const auto dependency_scan = dependency_database.scan(dependency_root / "assets");
    const auto* source = dependency_database.find_by_path("assets/shaders/Test.msl");
    require(source != nullptr, "shader source is registered before descriptor import");
    const Engine::AssetHandle source_handle = source->handle;
    const std::string shader_json =
        "{\"name\":\"Test\",\"sampler_count\":1,\"uniform_buffers\":[],\"variants\":[{\"format\":\"msl\",\"source_handle\":\"" +
        source_handle.to_string() + "\",\"entry_point\":\"main0\"}]}";
    write_file(dependency_root / "assets/shaders/Test.shader", shader_json);
    const auto shader = dependency_database.import_asset("assets/shaders/Test.shader");
    require(shader.has_value(), "shader descriptor imports");
    write_file(dependency_root / "assets/materials/Test.material",
        "{\"name\":\"Test\",\"shader_handle\":\"" + shader->to_string() + "\",\"uniforms\":[]}");
    const auto material = dependency_database.import_asset("assets/materials/Test.material");
    require(material.has_value(), "material imports");
    require(dependency_database.dependencies(*material).size() == 1, "material exposes shader dependency");
    require(dependency_database.dependencies(*shader).size() == 1, "shader exposes source dependency");
    require(dependency_database.dependents(source_handle).size() == 1, "source exposes descriptor dependent");

    write_file(dependency_root / "assets/prefabs/MaterialUser.prefab",
        "{\"entities\":[{\"sprite\":{\"material_handle\":\"" + material->to_string() + "\"}}]}");
    const auto dependent_prefab = dependency_database.import_asset("assets/prefabs/MaterialUser.prefab");
    require(dependent_prefab.has_value(), "prefab imports for generic dependency discovery");
    write_file(dependency_root / "assets/scenes/PrefabUser.scene",
        "{\"entities\":[{\"prefab_link\":{\"asset_handle\":\"" + dependent_prefab->to_string() + "\"}}]}");
    const auto scene = dependency_database.import_asset("assets/scenes/PrefabUser.scene");
    require(scene.has_value(), "scene imports for generic dependency discovery");
    require(dependency_database.dependencies(*dependent_prefab).size() == 1,
        "prefab discovers nested component asset handles");
    require(dependency_database.dependencies(*scene).size() == 1,
        "scene discovers prefab link asset handles");
    require(dependency_database.dependents(*dependent_prefab).size() == 1,
        "prefab reports the scene that uses it");

    write_file(dependency_root / "assets/textures/Sanctuary.png");
    const auto palette_texture = dependency_database.import_asset("assets/textures/Sanctuary.png");
    require(palette_texture.has_value(), "level palette texture imports");
    const Engine::LevelPaletteAsset path_only_palette = make_texture_palette(
        {}, "assets/textures/Sanctuary.png");
    write_file(dependency_root / "assets/palettes/Sanctuary.levelpalette",
        path_only_palette.to_json().dump());
    const auto level_palette = dependency_database.import_asset(
        "assets/palettes/Sanctuary.levelpalette");
    require(level_palette.has_value(), "path-only level palette imports");
    require(dependency_database.find(*level_palette)->type == Engine::AssetType::LevelPalette,
        "level palette type is inferred");
    require(dependency_database.find(*level_palette)->status == Engine::AssetStatus::Ready,
        "path-only level palette resolves to a ready asset");
    require(dependency_database.dependencies(*level_palette) ==
            std::vector<Engine::AssetHandle>{*palette_texture},
        "path-only level palette discovers the resolved asset handle");
    require(dependency_database.dependents(*palette_texture) ==
            std::vector<Engine::AssetHandle>{*level_palette},
        "referenced asset reports its level palette dependent");

    Engine::AssetHandle stale_texture_handle{1};
    while (dependency_database.find(stale_texture_handle)) ++stale_texture_handle.value;
    const Engine::LevelPaletteAsset stale_handle_palette = make_texture_palette(
        stale_texture_handle, "assets/textures/Sanctuary.png");
    write_file(dependency_root / "assets/palettes/StaleHandle.levelpalette",
        stale_handle_palette.to_json().dump());
    const auto stale_level_palette = dependency_database.import_asset(
        "assets/palettes/StaleHandle.levelpalette");
    require(stale_level_palette.has_value(), "stale-handle level palette imports");
    require(dependency_database.find(*stale_level_palette)->status == Engine::AssetStatus::Ready,
        "valid diagnostic path recovers a stale level palette handle");
    require(dependency_database.dependencies(*stale_level_palette) ==
            std::vector<Engine::AssetHandle>{*palette_texture},
        "stale handle is replaced by the asset resolved from its valid path");

    write_file(dependency_root / "assets/palettes/Malformed.levelpalette", "{not-json");
    const auto malformed_palette = dependency_database.import_asset(
        "assets/palettes/Malformed.levelpalette");
    require(malformed_palette.has_value(), "malformed palette keeps a repairable asset record");
    require(dependency_database.find(*malformed_palette)->status == Engine::AssetStatus::FailedImport,
        "malformed level palette JSON is marked as a failed import");
    require(dependency_database.dependencies(*malformed_palette).empty(),
        "malformed level palette does not publish dependency edges");

    write_file(dependency_root / "assets/palettes/Malformed.levelpalette",
        path_only_palette.to_json().dump());
    require(dependency_database.import_asset("assets/palettes/Malformed.levelpalette") ==
            malformed_palette,
        "reimporting a repaired palette preserves its handle");
    require(dependency_database.find(*malformed_palette)->status == Engine::AssetStatus::Ready,
        "reimporting valid JSON repairs a failed level palette");
    require(dependency_database.dependencies(*malformed_palette) ==
            std::vector<Engine::AssetHandle>{*palette_texture},
        "repaired palette publishes its dependency graph");
    Engine::AssetDatabase reloaded_palette_database(dependency_root);
    [[maybe_unused]] const auto reloaded_palette_scan =
        reloaded_palette_database.scan(dependency_root / "assets");
    const auto* reloaded_palette = reloaded_palette_database.find(*level_palette);
    require(reloaded_palette && reloaded_palette->type == Engine::AssetType::LevelPalette,
        "level palette type persists across database reload");
    require(reloaded_palette_database.dependencies(*level_palette) ==
            std::vector<Engine::AssetHandle>{*palette_texture},
        "level palette dependencies persist across database reload");

    fs::remove(dependency_root / "assets/textures/Sanctuary.png", error);
    [[maybe_unused]] const auto missing_palette_dependency_scan = dependency_database.rescan();
    require(dependency_database.find(*level_palette)->status == Engine::AssetStatus::MissingDependency,
        "missing referenced asset invalidates level palette");
    write_file(dependency_root / "assets/textures/Sanctuary.png");
    [[maybe_unused]] const auto repaired_palette_dependency_scan = dependency_database.rescan();
    require(dependency_database.find(*level_palette)->status == Engine::AssetStatus::Ready,
        "restored referenced asset repairs level palette");

    fs::remove(dependency_root / "assets/shaders/Test.msl", error);
    [[maybe_unused]] const auto missing_dependency_scan = dependency_database.rescan();
    require(dependency_database.find(*shader)->status == Engine::AssetStatus::MissingDependency,
        "missing variant source invalidates shader descriptor");
    require(dependency_database.find(*material)->status == Engine::AssetStatus::MissingDependency,
        "missing shader invalidates material");
    require(dependency_database.find(*dependent_prefab)->status == Engine::AssetStatus::MissingDependency,
        "missing material invalidates referencing prefab");
    require(dependency_database.find(*scene)->status == Engine::AssetStatus::MissingDependency,
        "missing prefab invalidates referencing scene");

    write_file(dependency_root / "assets/shaders/Test.msl", "fragment float4 main0() { return float4(1); }");
    [[maybe_unused]] const auto repaired_dependency_scan = dependency_database.rescan();
    require(dependency_database.find(*shader)->status == Engine::AssetStatus::Ready,
        "restored variant source repairs shader descriptor");
    require(dependency_database.find(*material)->status == Engine::AssetStatus::Ready,
        "restored shader repairs material");
    require(dependency_database.find(*dependent_prefab)->status == Engine::AssetStatus::Ready,
        "restored material repairs prefab");
    require(dependency_database.find(*scene)->status == Engine::AssetStatus::Ready,
        "restored prefab repairs scene");
    fs::remove_all(dependency_root, error);
    return EXIT_SUCCESS;
}
