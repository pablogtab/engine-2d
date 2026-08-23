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
    require(Engine::AssetDatabase::type_for_path("PlayerController.e2s") == Engine::AssetType::Script,
        "EngineScript files are recognized as script assets");
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

    fs::remove(dependency_root / "assets/shaders/Test.msl", error);
    [[maybe_unused]] const auto missing_dependency_scan = dependency_database.rescan();
    require(dependency_database.find(*shader)->status == Engine::AssetStatus::MissingDependency,
        "missing variant source invalidates shader descriptor");
    require(dependency_database.find(*material)->status == Engine::AssetStatus::MissingDependency,
        "missing shader invalidates material");

    write_file(dependency_root / "assets/shaders/Test.msl", "fragment float4 main0() { return float4(1); }");
    [[maybe_unused]] const auto repaired_dependency_scan = dependency_database.rescan();
    require(dependency_database.find(*shader)->status == Engine::AssetStatus::Ready,
        "restored variant source repairs shader descriptor");
    require(dependency_database.find(*material)->status == Engine::AssetStatus::Ready,
        "restored shader repairs material");
    fs::remove_all(dependency_root, error);
    return EXIT_SUCCESS;
}
