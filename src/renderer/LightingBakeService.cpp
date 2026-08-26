#include "LightingBakeService.hpp"

#include "RenderLayer.hpp"
#include "ShadowSilhouette.hpp"
#include "stb/stb_image.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <format>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Engine {
namespace {

using Json = nlohmann::json;

struct Vec2 { float x{0.0f}; float y{0.0f}; };
Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 operator*(Vec2 value, float scalar) { return {value.x * scalar, value.y * scalar}; }
float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
float length(Vec2 value) { return std::sqrt(dot(value, value)); }
Vec2 normalize(Vec2 value) {
    const float size = length(value);
    return size > 0.0001f ? value * (1.0f / size) : Vec2{};
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return input ? std::string(std::istreambuf_iterator<char>(input), {}) : std::string{};
}

std::string fnv1a64_hex(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : value) { hash ^= byte; hash *= 1099511628211ull; }
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

std::filesystem::path scene_bake_root(
    const std::filesystem::path& project_root,
    const std::filesystem::path& scene_path
) {
    return project_root / "Library" / "LightingBakes" / scene_path.stem();
}

std::filesystem::path bake_directory(
    const std::filesystem::path& project_root,
    const std::filesystem::path& scene_path,
    std::string_view hash
) {
    return scene_bake_root(project_root, scene_path) / hash;
}

Vec2 json_vec2(const Json& value, Vec2 fallback = {}) {
    return value.is_array() && value.size() >= 2
        ? Vec2{value[0].get<float>(), value[1].get<float>()} : fallback;
}

struct WorldTransform {
    Vec2 position{};
    Vec2 scale{1.0f, 1.0f};
    float rotation{0.0f};
};

Vec2 transform_point(Vec2 point, const WorldTransform& transform) {
    point.x *= transform.scale.x;
    point.y *= transform.scale.y;
    const float radians = transform.rotation * 0.01745329251994329577f;
    const float cosine = std::cos(radians), sine = std::sin(radians);
    return {
        transform.position.x + point.x * cosine - point.y * sine,
        transform.position.y + point.x * sine + point.y * cosine
    };
}

struct BakeLight {
    Vec2 position{};
    std::string type{"Point"};
    float rotation{0.0f};
    float range{0.0f};
    float intensity{0.0f};
    float falloff{1.0f};
    float height{64.0f};
    float spot_inner{25.0f};
    float spot_outer{45.0f};
    Vec2 area_size{96.0f, 32.0f};
    float red{1.0f}, green{1.0f}, blue{1.0f}, alpha{1.0f};
    float shadow_strength{1.0f};
    float shadow_max_distance{1024.0f};
    RenderLayerMask affected_layers{ALL_RENDER_LAYERS};
    bool casts_shadows{false};
};

struct BakeCaster {
    std::vector<Vec2> vertices;
    RenderLayerMask affected_layers{ALL_RENDER_LAYERS};
    float height{32.0f};
    float opacity{1.0f};
};

struct BakeTileInput {
    Vec2 origin{};
    float texels_per_unit{1.0f};
    RenderLayer render_layer{0};
    int pixel_width{0};
    int pixel_height{0};
    int volume_index{0};
    int tile_x{0};
    int tile_y{0};
};

struct BakeInput {
    std::string hash;
    bool has_volume{false};
    std::vector<BakeTileInput> tiles;
    std::vector<BakeLight> lights;
    std::vector<BakeCaster> casters;
};

std::vector<Vec2> collider_vertices(const Json& shape) {
    const std::string type = shape.value("type", "Box");
    const Vec2 offset = json_vec2(shape.value("offset", Json::array()));
    if (type == "Circle") {
        constexpr int segments = 24;
        const float radius = std::max(0.0f, shape.value("radius", 0.0f));
        std::vector<Vec2> result;
        result.reserve(segments);
        for (int index = 0; index < segments; ++index) {
            const float angle = static_cast<float>(index) * 6.283185307179586f / segments;
            result.push_back(offset + Vec2{std::cos(angle) * radius, std::sin(angle) * radius});
        }
        return result;
    }
    if (type == "Polygon") {
        std::vector<Vec2> result;
        for (const Json& point : shape.value("vertices", Json::array())) {
            if (point.is_array() && point.size() == 2) result.push_back(offset + json_vec2(point));
        }
        return result;
    }
    const Vec2 size = json_vec2(shape.value("size", Json::array()));
    const Vec2 half{size.x * 0.5f, size.y * 0.5f};
    return {
        offset + Vec2{-half.x, -half.y}, offset + Vec2{half.x, -half.y},
        offset + Vec2{half.x, half.y}, offset + Vec2{-half.x, half.y}
    };
}

bool point_in_polygon(Vec2 point, const std::vector<Vec2>& polygon) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2 a = polygon[i], b = polygon[j];
        if (((a.y > point.y) != (b.y > point.y)) &&
            point.x < (b.x - a.x) * (point.y - a.y) / ((b.y - a.y) + 0.000001f) + a.x) inside = !inside;
    }
    return inside;
}

float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

bool segments_intersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const Vec2 r = b - a, s = d - c;
    const float denominator = cross(r, s);
    if (std::abs(denominator) < 0.000001f) return false;
    const float t = cross(c - a, s) / denominator;
    const float u = cross(c - a, r) / denominator;
    return t > 0.0001f && t < 0.9999f && u >= 0.0f && u <= 1.0f;
}

bool segment_hits_polygon(Vec2 from, Vec2 to, const std::vector<Vec2>& polygon) {
    if (polygon.size() < 3) return false;
    if (point_in_polygon(to, polygon)) return true;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        if (segments_intersect(from, to, polygon[index], polygon[(index + 1) % polygon.size()])) return true;
    }
    return false;
}

WorldTransform resolve_world_transform(
    std::uint64_t id,
    const std::unordered_map<std::uint64_t, const Json*>& entities,
    std::unordered_map<std::uint64_t, WorldTransform>& cache
) {
    if (const auto cached = cache.find(id); cached != cache.end()) return cached->second;
    const auto found = entities.find(id);
    if (found == entities.end()) return {};
    const Json& entity = *found->second;
    const Json transform = entity.value("transform", Json::object());
    WorldTransform local{
        json_vec2(transform.value("position", Json::array())),
        json_vec2(transform.value("scale", Json::array()), {1.0f, 1.0f}),
        transform.value("rotation", 0.0f)
    };
    const std::uint64_t parent = entity.value("hierarchy", Json::object()).value("parent", 0ull);
    if (parent == 0 || parent == id || !entities.contains(parent)) {
        cache[id] = local;
        return local;
    }
    const WorldTransform parent_transform = resolve_world_transform(parent, entities, cache);
    WorldTransform world{
        transform_point(local.position, parent_transform),
        {parent_transform.scale.x * local.scale.x, parent_transform.scale.y * local.scale.y},
        parent_transform.rotation + local.rotation
    };
    cache[id] = world;
    return world;
}

BakeInput parse_input(const std::filesystem::path& project_root, const std::filesystem::path& scene_path) {
    const std::string scene_text = read_file(scene_path);
    const std::string settings_text = read_file(project_root / "ProjectSettings" / "Project.json");
    const std::string asset_database_text = read_file(project_root / "ProjectSettings" / "AssetDatabase.json");
    std::string hash_material = std::string(LightingBakeService::k_baker_version) + "\n" +
        scene_text + "\n" + settings_text + "\n" + asset_database_text;
    BakeInput input;
    const Json scene = Json::parse(scene_text, nullptr, false);
    if (scene.is_discarded() || !scene.is_object()) {
        input.hash = fnv1a64_hex(hash_material);
        return input;
    }
    std::unordered_map<std::string, std::filesystem::path> texture_paths;
    const Json asset_database = Json::parse(asset_database_text, nullptr, false);
    if (!asset_database.is_discarded()) {
        for (const Json& asset : asset_database.value("assets", Json::array())) {
            if (asset.value("type", "") == "texture" && asset.value("status", "") == "ready") {
                texture_paths[asset.value("handle", "")] = project_root / asset.value("path", "");
            }
        }
    }
    const Json entities_json = scene.value("entities", Json::array());
    std::unordered_map<std::uint64_t, const Json*> entities;
    for (const Json& entity : entities_json) entities.emplace(entity.value("id", 0ull), &entity);
    std::unordered_map<std::uint64_t, WorldTransform> transforms;
    RenderLayerMask receiver_layers = 0;

    for (const Json& entity : entities_json) {
        const std::uint64_t id = entity.value("id", 0ull);
        const WorldTransform transform = resolve_world_transform(id, entities, transforms);
        const Json sprite_lighting = entity.value("sprite_lighting_2d", Json::object());
        if (sprite_lighting.value("enabled", false) && sprite_lighting.value("baked_receiver", false)) {
            const RenderLayer layer = std::min<RenderLayer>(
                entity.value("sprite", Json::object()).value("render_layer", DEFAULT_RENDER_LAYER), 31);
            receiver_layers |= render_layer_mask(layer);
        }

        const Json light = entity.value("light_2d", Json::object());
        if (!light.empty() && light.value("enabled", true) && light.value("mobility", "Realtime") == "Baked") {
            const auto color = light.value("color", std::vector<int>{255, 255, 255, 255});
            input.lights.push_back({
                transform.position, light.value("type", "Point"), transform.rotation,
                std::max(0.0f, light.value("range", 0.0f)), std::max(0.0f, light.value("intensity", 0.0f)),
                std::clamp(light.value("falloff", 1.0f), 0.1f, 8.0f), std::max(0.0f, light.value("height", 64.0f)),
                light.value("spot_inner_angle", 25.0f), light.value("spot_outer_angle", 45.0f),
                json_vec2(light.value("area_size", Json::array()), {96.0f, 32.0f}),
                color.size() > 0 ? color[0] / 255.0f : 1.0f,
                color.size() > 1 ? color[1] / 255.0f : 1.0f,
                color.size() > 2 ? color[2] / 255.0f : 1.0f,
                color.size() > 3 ? color[3] / 255.0f : 1.0f,
                std::clamp(light.value("shadow_strength", 1.0f), 0.0f, 1.0f),
                std::max(0.0f, light.value("shadow_max_distance", 1024.0f)),
                light.value("affected_layers", ALL_RENDER_LAYERS), light.value("shadow_mode", "Hard") != "None"
            });
        }

        const Json caster = entity.value("shadow_caster_2d", Json::object());
        if (caster.empty() || !caster.value("enabled", true) || caster.value("mobility", "Realtime") != "Baked") continue;
        std::vector<std::vector<Vec2>> polygons;
        const std::string source = caster.value("source", "StaticPolygon");
        if (source == "Collider") {
            for (const Json& shape : entity.value("collider", Json::array())) polygons.push_back(collider_vertices(shape));
        } else if (source == "SpriteAlphaContour") {
            const Json sprite = entity.value("sprite", Json::object());
            const Vec2 size = json_vec2(sprite.value("size", Json::array()), {32.0f, 32.0f});
            const auto texture_path = texture_paths.find(sprite.value("texture_handle", ""));
            std::vector<Vec2> silhouette;
            if (texture_path != texture_paths.end()) {
                const std::string texture_bytes = read_file(texture_path->second);
                hash_material.append("\n").append(texture_bytes);
                int texture_width = 0, texture_height = 0, channels = 0;
                stbi_uc* pixels = stbi_load(
                    texture_path->second.string().c_str(), &texture_width, &texture_height, &channels, 4
                );
                if (pixels) {
                    std::vector<std::uint8_t> alpha(
                        static_cast<std::size_t>(texture_width) * texture_height
                    );
                    for (std::size_t pixel = 0; pixel < alpha.size(); ++pixel) {
                        alpha[pixel] = pixels[pixel * 4 + 3];
                    }
                    const auto source_rect = sprite.value("src_rect", std::vector<float>{
                        0.0f, 0.0f, static_cast<float>(texture_width), static_cast<float>(texture_height)
                    });
                    const SDL_FRect frame = source_rect.size() == 4
                        ? SDL_FRect{source_rect[0], source_rect[1], source_rect[2], source_rect[3]}
                        : SDL_FRect{0, 0, static_cast<float>(texture_width), static_cast<float>(texture_height)};
                    const auto normalized = extract_alpha_silhouette(
                        alpha, texture_width, texture_height, frame,
                        caster.value("alpha_threshold", 0.5f),
                        caster.value("contour_tolerance", 1.0f)
                    );
                    const bool flip_x = sprite.value("flip_x", false);
                    const bool flip_y = sprite.value("flip_y", false);
                    const Vec2 pivot = json_vec2(
                        entity.value("transform", Json::object()).value("pivot", Json::array()),
                        {0.5f, 0.5f}
                    );
                    silhouette.reserve(normalized.size());
                    for (const glm::vec2 point : normalized) {
                        const float u = flip_x ? 1.0f - point.x : point.x;
                        const float v = flip_y ? 1.0f - point.y : point.y;
                        silhouette.push_back({(u - pivot.x) * size.x, (v - pivot.y) * size.y});
                    }
                    stbi_image_free(pixels);
                }
            }
            if (silhouette.size() < 3) {
                silhouette = {{-size.x * 0.5f, -size.y * 0.5f}, {size.x * 0.5f, -size.y * 0.5f},
                              {size.x * 0.5f, size.y * 0.5f}, {-size.x * 0.5f, size.y * 0.5f}};
            }
            polygons.push_back(std::move(silhouette));
        } else {
            std::vector<Vec2> polygon;
            for (const Json& vertex : caster.value("vertices", Json::array())) {
                if (vertex.is_array() && vertex.size() == 2) polygon.push_back(json_vec2(vertex));
            }
            polygons.push_back(std::move(polygon));
        }
        for (auto& polygon : polygons) {
            if (polygon.size() < 3) continue;
            for (Vec2& point : polygon) point = transform_point(point, transform);
            input.casters.push_back({
                std::move(polygon), caster.value("affected_layers", ALL_RENDER_LAYERS),
                std::max(0.0f, caster.value("height", 32.0f)),
                std::clamp(caster.value("opacity", 1.0f), 0.0f, 1.0f)
            });
        }
    }

    int volume_index = 0;
    for (const Json& entity : entities_json) {
        const Json volume = entity.value("lighting_bake_volume_2d", Json::object());
        if (volume.empty() || !volume.value("enabled", true)) continue;
        const Vec2 size = json_vec2(volume.value("size", Json::array()));
        if (size.x <= 0.0f || size.y <= 0.0f) continue;
        input.has_volume = true;
        const WorldTransform transform = resolve_world_transform(entity.value("id", 0ull), entities, transforms);
        const float texels_per_unit = std::max(0.01f, volume.value("texels_per_unit", 1.0f));
        const float world_width = size.x * std::abs(transform.scale.x);
        const float world_height = size.y * std::abs(transform.scale.y);
        const int width = static_cast<int>(std::ceil(world_width * texels_per_unit));
        const int height = static_cast<int>(std::ceil(world_height * texels_per_unit));
        RenderLayerMask layers = volume.value("affected_layers", ALL_RENDER_LAYERS) & receiver_layers;
        for (RenderLayer layer = 0; layer < 32; ++layer) {
            if (!renders_layer(layers, layer)) continue;
            const int tiles_x = std::max(1, (width + LightingBakeService::k_tile_size - 1) / LightingBakeService::k_tile_size);
            const int tiles_y = std::max(1, (height + LightingBakeService::k_tile_size - 1) / LightingBakeService::k_tile_size);
            for (int tile_y = 0; tile_y < tiles_y; ++tile_y) for (int tile_x = 0; tile_x < tiles_x; ++tile_x) {
                input.tiles.push_back({
                    {transform.position.x - world_width * 0.5f + static_cast<float>(tile_x * LightingBakeService::k_tile_size) / texels_per_unit,
                     transform.position.y - world_height * 0.5f + static_cast<float>(tile_y * LightingBakeService::k_tile_size) / texels_per_unit},
                    texels_per_unit, layer,
                    std::min(LightingBakeService::k_tile_size, width - tile_x * LightingBakeService::k_tile_size),
                    std::min(LightingBakeService::k_tile_size, height - tile_y * LightingBakeService::k_tile_size),
                    volume_index, tile_x, tile_y
                });
            }
        }
        ++volume_index;
    }
    input.hash = fnv1a64_hex(hash_material);
    return input;
}

float light_amount(const BakeLight& light, Vec2 point) {
    if (light.type == "Ambient" || light.type == "Directional") return light.intensity * light.alpha;
    Vec2 delta = point - light.position;
    float distance = length(delta);
    if (light.type == "Area") {
        const float radians = light.rotation * 0.01745329251994329577f;
        const Vec2 axis{std::cos(radians), std::sin(radians)};
        const float along = std::clamp(dot(delta, axis), -light.area_size.x * 0.5f, light.area_size.x * 0.5f);
        delta = point - (light.position + axis * along);
        distance = length(delta);
    }
    if (light.range <= 0.0f || distance >= light.range) return 0.0f;
    float cone = 1.0f;
    if (light.type == "Spot") {
        const float radians = light.rotation * 0.01745329251994329577f;
        const Vec2 forward{std::cos(radians), std::sin(radians)};
        const float angle = std::acos(std::clamp(dot(forward, normalize(delta)), -1.0f, 1.0f)) * 57.29577951308232f;
        const float inner = std::max(0.0f, light.spot_inner * 0.5f);
        const float outer = std::max(inner, light.spot_outer * 0.5f);
        if (angle >= outer) return 0.0f;
        cone = outer <= inner ? 1.0f : std::clamp((outer - angle) / (outer - inner), 0.0f, 1.0f);
    }
    return light.intensity * light.alpha * cone * std::pow(1.0f - distance / light.range, light.falloff);
}

float shadow_visibility(const BakeLight& light, Vec2 point, RenderLayer layer, const std::vector<BakeCaster>& casters) {
    if (!light.casts_shadows || light.type == "Ambient") return 1.0f;
    Vec2 ray_start = light.position;
    if (light.type == "Directional") {
        const float radians = light.rotation * 0.01745329251994329577f;
        ray_start = point + Vec2{std::cos(radians), std::sin(radians)} * std::max(light.shadow_max_distance, 1.0f);
    } else if (length(point - light.position) > light.shadow_max_distance) {
        return 1.0f;
    }
    float visibility = 1.0f;
    for (const BakeCaster& caster : casters) {
        if (!renders_layer(caster.affected_layers, layer) || !segment_hits_polygon(ray_start, point, caster.vertices)) continue;
        const float height_factor = light.height > 0.0001f ? std::clamp(caster.height / light.height, 0.0f, 1.0f) : 1.0f;
        visibility *= 1.0f - caster.opacity * height_factor * light.shadow_strength;
        if (visibility <= 0.001f) return 0.0f;
    }
    return std::clamp(visibility, 0.0f, 1.0f);
}

bool write_tile(
    const std::filesystem::path& path,
    const BakeTileInput& tile,
    const std::vector<BakeLight>& lights,
    const std::vector<BakeCaster>& casters,
    std::string* error
) {
    SDL_Surface* surface = SDL_CreateSurface(tile.pixel_width, tile.pixel_height, SDL_PIXELFORMAT_RGBA32);
    if (!surface) { if (error) *error = SDL_GetError(); return false; }
    auto* pixels = static_cast<std::uint8_t*>(surface->pixels);
    for (int y = 0; y < surface->h; ++y) for (int x = 0; x < surface->w; ++x) {
        const Vec2 world{tile.origin.x + (static_cast<float>(x) + 0.5f) / tile.texels_per_unit,
                         tile.origin.y + (static_cast<float>(y) + 0.5f) / tile.texels_per_unit};
        // A baked lightmap is multiplied into the sprite albedo. With no
        // baked lights the neutral result is therefore white, not black.
        const float neutral = lights.empty() ? 1.0f : 0.0f;
        float red = neutral, green = neutral, blue = neutral;
        for (const BakeLight& light : lights) {
            if (!renders_layer(light.affected_layers, tile.render_layer)) continue;
            const float amount = light_amount(light, world) * shadow_visibility(light, world, tile.render_layer, casters);
            red += light.red * amount; green += light.green * amount; blue += light.blue * amount;
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

std::optional<LightingBakeManifest> load_manifest_directory(
    const std::filesystem::path& directory,
    LightingBakeStatus status
) {
    const Json manifest = Json::parse(read_file(directory / "manifest.json"), nullptr, false);
    if (manifest.is_discarded() || manifest.value("baker_version", "") != LightingBakeService::k_baker_version ||
        !manifest.contains("tiles") || !manifest["tiles"].is_array()) return std::nullopt;
    LightingBakeManifest result;
    result.status = std::move(status);
    result.status.directory = directory;
    result.status.input_hash = manifest.value("input_hash", "");
    result.status.tile_count = manifest.value("tile_count", 0u);
    result.status.baked_light_count = manifest.value("baked_light_count", 0u);
    result.status.baked_caster_count = manifest.value("baked_caster_count", 0u);
    for (const Json& item : manifest["tiles"]) {
        const auto origin = item.value("origin", std::vector<float>{});
        const std::string file = item.value("file", "");
        if (file.empty() || origin.size() != 2) return std::nullopt;
        const auto image_path = directory / file;
        if (!std::filesystem::is_regular_file(image_path)) return std::nullopt;
        const RenderLayer layer = std::min<RenderLayer>(item.value("render_layer", 0u), 31);
        result.tiles.push_back({
            image_path, origin[0], origin[1], std::max(0.01f, item.value("texels_per_unit", 1.0f)),
            item.value("affected_layers", render_layer_mask(layer)), layer,
            item.value("pixel_width", LightingBakeService::k_tile_size),
            item.value("pixel_height", LightingBakeService::k_tile_size)
        });
    }
    if (result.tiles.size() != result.status.tile_count) return std::nullopt;
    return result;
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
    result.tile_count = static_cast<std::uint32_t>(input.tiles.size());
    result.baked_light_count = static_cast<std::uint32_t>(input.lights.size());
    result.baked_caster_count = static_cast<std::uint32_t>(input.casters.size());
    result.directory = bake_directory(project_root, scene_path, input.hash);
    if (!input.has_volume) { result.reason = "Scene has no enabled Lighting Bake Volume 2D"; return result; }
    auto manifest = load_manifest_directory(result.directory, result);
    if (!manifest || manifest->status.input_hash != input.hash || manifest->status.tile_count != result.tile_count) {
        result.reason = "Bake is missing or stale";
        return result;
    }
    result.current = true;
    result.reason = "Bake is current";
    return result;
}

std::optional<LightingBakeManifest> LightingBakeService::load_current(
    const std::filesystem::path& project_root,
    const std::filesystem::path& requested_scene_path
) {
    LightingBakeStatus current_status = status(project_root, requested_scene_path);
    if (!current_status.current) return std::nullopt;
    const std::filesystem::path directory = current_status.directory;
    return load_manifest_directory(directory, std::move(current_status));
}

std::optional<LightingBakeManifest> LightingBakeService::load_latest_valid(
    const std::filesystem::path& project_root,
    const std::filesystem::path& requested_scene_path
) {
    const std::filesystem::path scene_path = normalized_scene_path(project_root, requested_scene_path);
    LightingBakeStatus current_status = status(project_root, scene_path);
    if (!current_status.has_bake_volume) return std::nullopt;
    if (current_status.current) {
        return load_manifest_directory(current_status.directory, std::move(current_status));
    }
    const std::filesystem::path root = scene_bake_root(project_root, scene_path);
    std::error_code error;
    std::filesystem::file_time_type newest{};
    std::optional<LightingBakeManifest> best;
    if (!std::filesystem::is_directory(root, error)) return std::nullopt;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error || !entry.is_directory()) continue;
        LightingBakeStatus candidate_status;
        candidate_status.reason = "Using previous valid bake while current input is stale";
        auto candidate = load_manifest_directory(entry.path(), candidate_status);
        if (!candidate) continue;
        const auto time = std::filesystem::last_write_time(entry.path() / "manifest.json", error);
        if (error) { error.clear(); continue; }
        if (!best || time > newest) { newest = time; best = std::move(candidate); }
    }
    return best;
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
    Json tiles = Json::array();
    for (const BakeTileInput& tile : input.tiles) {
        const std::string filename = std::format(
            "volume-{}-layer-{}-tile-{}-{}.png",
            tile.volume_index, tile.render_layer, tile.tile_x, tile.tile_y
        );
        if (!write_tile(directory / filename, tile, input.lights, input.casters, error)) return false;
        tiles.push_back({
            {"file", filename}, {"origin", {tile.origin.x, tile.origin.y}},
            {"texels_per_unit", tile.texels_per_unit}, {"render_layer", tile.render_layer},
            {"affected_layers", render_layer_mask(tile.render_layer)},
            {"pixel_width", tile.pixel_width}, {"pixel_height", tile.pixel_height}
        });
    }
    const Json manifest{
        {"baker_version", k_baker_version}, {"input_hash", input.hash},
        {"tile_size", k_tile_size}, {"tile_count", input.tiles.size()},
        {"baked_light_count", input.lights.size()}, {"baked_caster_count", input.casters.size()},
        {"scene", scene_path.filename().string()}, {"tiles", std::move(tiles)}
    };
    const std::filesystem::path temporary = directory / "manifest.json.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) { if (error) *error = "Cannot write lighting bake manifest"; return false; }
        output << manifest.dump(2) << '\n';
        if (!output) { if (error) *error = "Cannot finalize lighting bake manifest"; return false; }
    }
    std::filesystem::rename(temporary, directory / "manifest.json", filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(directory / "manifest.json", filesystem_error);
        filesystem_error.clear();
        std::filesystem::rename(temporary, directory / "manifest.json", filesystem_error);
    }
    if (filesystem_error) { if (error) *error = filesystem_error.message(); return false; }
    if (result) *result = status(project_root, scene_path);
    return true;
}

} // namespace Engine
