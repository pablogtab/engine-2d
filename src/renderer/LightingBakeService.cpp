#include "LightingBakeService.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <format>
#include <iomanip>
#include <sstream>
#include <vector>

namespace Engine {
namespace {

using Json = nlohmann::json;

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return input ? std::string(std::istreambuf_iterator<char>(input), {}) : std::string{};
}

std::string fnv1a64_hex(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::filesystem::path normalized_scene_path(
    const std::filesystem::path& project_root,
    const std::filesystem::path& scene_path
) {
    return scene_path.is_absolute() ? scene_path : project_root / scene_path;
}

std::filesystem::path bake_directory(
    const std::filesystem::path& project_root,
    const std::filesystem::path& scene_path,
    std::string_view input_hash
) {
    return project_root / "Library" / "LightingBakes" / scene_path.stem() / input_hash;
}

struct BakeInput {
    std::string hash;
    bool has_volume{false};
    int tile_count{0};
    struct Tile { float origin_x{0.0f}; float origin_y{0.0f}; float texels_per_unit{1.0f}; };
    struct Light { float x{0.0f}; float y{0.0f}; float range{0.0f}; float intensity{0.0f}; float r{0.0f}; float g{0.0f}; float b{0.0f}; bool ambient{false}; };
    std::vector<Tile> tiles;
    std::vector<Light> lights;
};

BakeInput parse_input(const std::filesystem::path& project_root, const std::filesystem::path& scene_path) {
    const std::string scene_text = read_file(scene_path);
    const std::string settings_text = read_file(project_root / "ProjectSettings" / "Project.json");
    BakeInput input;
    input.hash = fnv1a64_hex(std::string(LightingBakeService::k_baker_version) + "\n" + scene_text + "\n" + settings_text);
    const Json scene = Json::parse(scene_text, nullptr, false);
    if (scene.is_discarded() || !scene.is_object()) return input;
    const Json entities = scene.value("entities", Json::array());
    for (const Json& entity : entities) {
        const auto transform = entity.value("transform", Json::object());
        const auto position = transform.value("position", std::vector<float>{});
        const float entity_x = position.size() >= 2 ? position[0] : 0.0f;
        const float entity_y = position.size() >= 2 ? position[1] : 0.0f;
        const auto light = entity.find("light_2d");
        if (light != entity.end() && light->is_object() && light->value("enabled", true) &&
            light->value("mobility", std::string{"Realtime"}) == "Baked") {
            const auto color = light->value("color", std::vector<int>{255, 255, 255, 255});
            input.lights.push_back({
                entity_x, entity_y, std::max(0.0f, light->value("range", 0.0f)),
                std::max(0.0f, light->value("intensity", 0.0f)),
                color.size() > 0 ? color[0] / 255.0f : 1.0f,
                color.size() > 1 ? color[1] / 255.0f : 1.0f,
                color.size() > 2 ? color[2] / 255.0f : 1.0f,
                light->value("type", std::string{"Point"}) == "Ambient"
            });
        }
        const auto volume = entity.find("lighting_bake_volume_2d");
        if (volume == entity.end() || !volume->is_object() || !volume->value("enabled", true)) continue;
        const auto size = volume->value("size", std::vector<float>{});
        if (size.size() != 2 || size[0] <= 0.0f || size[1] <= 0.0f) continue;
        const float texels_per_unit = std::max(0.01f, volume->value("texels_per_unit", 1.0f));
        const int width = static_cast<int>(std::ceil(size[0] * texels_per_unit));
        const int height = static_cast<int>(std::ceil(size[1] * texels_per_unit));
        input.has_volume = true;
        const int tiles_x = std::max(1, (width + LightingBakeService::k_tile_size - 1) / LightingBakeService::k_tile_size);
        const int tiles_y = std::max(1, (height + LightingBakeService::k_tile_size - 1) / LightingBakeService::k_tile_size);
        for (int tile_y = 0; tile_y < tiles_y; ++tile_y) for (int tile_x = 0; tile_x < tiles_x; ++tile_x) {
            input.tiles.push_back({
                entity_x - size[0] * 0.5f + static_cast<float>(tile_x * LightingBakeService::k_tile_size) / texels_per_unit,
                entity_y - size[1] * 0.5f + static_cast<float>(tile_y * LightingBakeService::k_tile_size) / texels_per_unit,
                texels_per_unit
            });
        }
        input.tile_count = static_cast<int>(input.tiles.size());
    }
    return input;
}

bool write_tile(
    const std::filesystem::path& path,
    const BakeInput::Tile& tile,
    const std::vector<BakeInput::Light>& lights,
    std::string* error
) {
    SDL_Surface* surface = SDL_CreateSurface(LightingBakeService::k_tile_size, LightingBakeService::k_tile_size, SDL_PIXELFORMAT_RGBA32);
    if (!surface) { if (error) *error = SDL_GetError(); return false; }
    auto* pixels = static_cast<std::uint8_t*>(surface->pixels);
    for (int y = 0; y < surface->h; ++y) for (int x = 0; x < surface->w; ++x) {
        const float world_x = tile.origin_x + static_cast<float>(x) / tile.texels_per_unit;
        const float world_y = tile.origin_y + static_cast<float>(y) / tile.texels_per_unit;
        float red = 0.0f, green = 0.0f, blue = 0.0f;
        for (const auto& light : lights) {
            float amount = light.intensity;
            if (!light.ambient) {
                const float dx = world_x - light.x, dy = world_y - light.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                amount = light.range <= 0.0f ? light.intensity :
                    (distance < light.range
                        ? light.intensity * std::pow(1.0f - distance / light.range, 2.0f) : 0.0f);
            }
            red += light.r * amount; green += light.g * amount; blue += light.b * amount;
        }
        auto* pixel = pixels + static_cast<std::size_t>(y) * surface->pitch + static_cast<std::size_t>(x) * 4;
        pixel[0] = static_cast<std::uint8_t>(std::clamp(red, 0.0f, 1.0f) * 255.0f);
        pixel[1] = static_cast<std::uint8_t>(std::clamp(green, 0.0f, 1.0f) * 255.0f);
        pixel[2] = static_cast<std::uint8_t>(std::clamp(blue, 0.0f, 1.0f) * 255.0f);
        pixel[3] = 255;
    }
    const bool saved = SDL_SavePNG(surface, path.string().c_str());
    if (!saved && error) *error = SDL_GetError();
    SDL_DestroySurface(surface);
    return saved;
}

} // namespace

LightingBakeStatus LightingBakeService::status(
    const std::filesystem::path& project_root,
    const std::filesystem::path& requested_scene_path
) {
    const std::filesystem::path scene_path = normalized_scene_path(project_root, requested_scene_path);
    const BakeInput input = parse_input(project_root, scene_path);
    LightingBakeStatus result;
    result.input_hash = input.hash;
    result.has_bake_volume = input.has_volume;
    result.tile_count = static_cast<std::uint32_t>(input.tile_count);
    result.directory = bake_directory(project_root, scene_path, input.hash);
    if (!input.has_volume) { result.reason = "Scene has no enabled Lighting Bake Volume 2D"; return result; }
    const std::filesystem::path manifest_path = result.directory / "manifest.json";
    const Json manifest = Json::parse(read_file(manifest_path), nullptr, false);
    if (manifest.is_discarded() || manifest.value("input_hash", std::string{}) != input.hash) {
        result.reason = "Bake is missing or stale";
        return result;
    }
    for (std::uint32_t index = 0; index < result.tile_count; ++index) {
        if (!std::filesystem::is_regular_file(result.directory / std::format("layer-0-tile-{}-0.png", index))) {
            result.reason = "Bake tile is missing";
            return result;
        }
    }
    result.current = true;
    result.reason = "Bake is current";
    return result;
}

bool LightingBakeService::bake(
    const std::filesystem::path& project_root,
    const std::filesystem::path& requested_scene_path,
    LightingBakeStatus* result,
    std::string* error
) {
    const std::filesystem::path scene_path = normalized_scene_path(project_root, requested_scene_path);
    const BakeInput input = parse_input(project_root, scene_path);
    if (!input.has_volume) {
        if (error) *error = "Scene has no enabled Lighting Bake Volume 2D";
        if (result) *result = status(project_root, scene_path);
        return false;
    }
    const std::filesystem::path directory = bake_directory(project_root, scene_path, input.hash);
    std::error_code filesystem_error;
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) { if (error) *error = filesystem_error.message(); return false; }
    for (int index = 0; index < input.tile_count; ++index) {
        if (!write_tile(directory / std::format("layer-0-tile-{}-0.png", index), input.tiles[static_cast<std::size_t>(index)], input.lights, error)) return false;
    }
    const Json manifest{{"baker_version", k_baker_version}, {"input_hash", input.hash}, {"tile_size", k_tile_size}, {"tile_count", input.tile_count}, {"baked_light_count", input.lights.size()}, {"scene", scene_path.filename().string()}};
    std::ofstream output(directory / "manifest.json", std::ios::binary | std::ios::trunc);
    if (!output) { if (error) *error = "Cannot write lighting bake manifest"; return false; }
    output << manifest.dump(2) << '\n';
    if (!output) { if (error) *error = "Cannot finalize lighting bake manifest"; return false; }
    output.close();
    if (result) *result = status(project_root, scene_path);
    return true;
}

} // namespace Engine
