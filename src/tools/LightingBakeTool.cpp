#include "renderer/LightingBakeService.hpp"
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--bake-scene") {
        const std::filesystem::path project_root =
            std::filesystem::absolute(argv[2]).lexically_normal();
        std::filesystem::path scene_path = argv[3];
        if (scene_path.is_relative()) scene_path = project_root / scene_path;
        scene_path = std::filesystem::absolute(scene_path).lexically_normal();
        Engine::LightingBakeStatus status;
        std::string error;
        if (!Engine::LightingBakeService::bake(project_root, scene_path, &status, &error)) {
            std::cerr << "Lighting bake failed for " << scene_path << ": " << error << '\n';
            return 1;
        }
        std::cout << "Lighting bake completed for " << scene_path
                  << ": " << status.tile_count << " tile(s), "
                  << status.baked_light_count << " light(s), "
                  << status.baked_caster_count << " caster(s)\n";
        return 0;
    }
    if (argc != 3 || std::string(argv[1]) != "--validate-project") {
        std::cerr
            << "Usage: LightingBakeTool --validate-project <project-root>\n"
            << "       LightingBakeTool --bake-scene <project-root> <scene-path>\n";
        return 2;
    }
    const std::filesystem::path project_root = std::filesystem::absolute(argv[2]).lexically_normal();
    const std::filesystem::path scenes_root = project_root / "Assets" / "Scenes";
    std::error_code error;
    if (!std::filesystem::is_directory(scenes_root, error)) {
        std::cerr << "Scene directory is missing: " << scenes_root << '\n';
        return 1;
    }
    std::size_t required = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(scenes_root, error)) {
        if (error) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".scene") continue;
        const Engine::LightingBakeStatus status = Engine::LightingBakeService::status(project_root, entry.path());
        if (!status.has_bake_volume) continue;
        ++required;
        if (!status.current) {
            std::cerr << "Lighting bake is not current for " << entry.path() << ": " << status.reason << '\n';
            return 1;
        }
    }
    if (error) {
        std::cerr << "Cannot scan project scenes: " << error.message() << '\n';
        return 1;
    }
    std::cout << "Lighting bake validation passed for " << required << " scene(s)\n";
    return 0;
}
