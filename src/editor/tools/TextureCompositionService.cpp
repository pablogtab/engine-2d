#include "TextureCompositionService.hpp"

#include "stb/stb_image.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Engine {
namespace {

struct SourceImage {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
};

bool finite_layer(const TextureCompositionLayer& layer) {
    return std::isfinite(layer.anchor_x) && std::isfinite(layer.anchor_y) &&
        std::isfinite(layer.width) && std::isfinite(layer.height) &&
        std::isfinite(layer.pivot_x) && std::isfinite(layer.pivot_y) &&
        std::isfinite(layer.rotation_degrees) && std::isfinite(layer.source_rect.x) &&
        std::isfinite(layer.source_rect.y) && std::isfinite(layer.source_rect.w) &&
        std::isfinite(layer.source_rect.h);
}

bool load_source(const std::filesystem::path& path, SourceImage& image) {
    int channels = 0;
    stbi_uc* pixels = stbi_load(
        path.string().c_str(), &image.width, &image.height, &channels, STBI_rgb_alpha
    );
    if (!pixels) return false;
    image.rgba.assign(
        pixels, pixels + static_cast<std::size_t>(image.width) * image.height * 4
    );
    stbi_image_free(pixels);
    return true;
}

void blend_pixel(std::uint8_t* destination, const std::uint8_t* source, SDL_Color tint) {
    const float source_alpha = (source[3] / 255.0f) * (tint.a / 255.0f);
    if (source_alpha <= 0.0f) return;
    const float destination_alpha = destination[3] / 255.0f;
    const float output_alpha = source_alpha + destination_alpha * (1.0f - source_alpha);
    if (output_alpha <= 0.0f) return;
    for (int channel = 0; channel < 3; ++channel) {
        const std::uint8_t tint_channel = channel == 0 ? tint.r : channel == 1 ? tint.g : tint.b;
        const float source_color = (source[channel] / 255.0f) * (tint_channel / 255.0f);
        const float destination_color = destination[channel] / 255.0f;
        const float output_color = (
            source_color * source_alpha +
            destination_color * destination_alpha * (1.0f - source_alpha)
        ) / output_alpha;
        destination[channel] = static_cast<std::uint8_t>(
            std::clamp(std::lround(output_color * 255.0f), 0L, 255L)
        );
    }
    destination[3] = static_cast<std::uint8_t>(
        std::clamp(std::lround(output_alpha * 255.0f), 0L, 255L)
    );
}

} // namespace

bool TextureCompositionService::validate(
    const TextureCompositionSettings& settings, std::string& error
) {
    error.clear();
    if (settings.width < 1 || settings.height < 1 ||
        settings.width > 8192 || settings.height > 8192) {
        error = "Canvas dimensions must be between 1 and 8192 pixels";
        return false;
    }
    if (settings.layers.empty()) {
        error = "Add at least one texture layer";
        return false;
    }
    for (std::size_t index = 0; index < settings.layers.size(); ++index) {
        const auto& layer = settings.layers[index];
        if (layer.source_path.empty() || !finite_layer(layer) ||
            layer.width <= 0.0f || layer.height <= 0.0f ||
            layer.pivot_x < 0.0f || layer.pivot_x > 1.0f ||
            layer.pivot_y < 0.0f || layer.pivot_y > 1.0f ||
            (layer.use_source_rect && (layer.source_rect.w <= 0.0f || layer.source_rect.h <= 0.0f))) {
            error = "Layer " + std::to_string(index + 1) + " has invalid geometry";
            return false;
        }
    }
    return true;
}

TextureCompositionResult TextureCompositionService::compose(
    const TextureCompositionSettings& settings
) {
    TextureCompositionResult result;
    if (!validate(settings, result.error)) return result;
    result.width = settings.width;
    result.height = settings.height;
    result.rgba.assign(static_cast<std::size_t>(result.width) * result.height * 4, 0);

    std::vector<std::size_t> order(settings.layers.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        return settings.layers[lhs].sort_key < settings.layers[rhs].sort_key;
    });

    for (const std::size_t layer_index : order) {
        const auto& layer = settings.layers[layer_index];
        if (!layer.visible) continue;
        SourceImage source;
        if (!load_source(layer.source_path, source)) {
            result.error = "Could not load layer texture: " + layer.source_path.string();
            result.rgba.clear();
            return result;
        }

        const float source_x = layer.use_source_rect ? layer.source_rect.x : 0.0f;
        const float source_y = layer.use_source_rect ? layer.source_rect.y : 0.0f;
        const float source_width = layer.use_source_rect ? layer.source_rect.w : static_cast<float>(source.width);
        const float source_height = layer.use_source_rect ? layer.source_rect.h : static_cast<float>(source.height);
        if (source_x < 0.0f || source_y < 0.0f || source_x + source_width > source.width ||
            source_y + source_height > source.height) {
            result.error = "Layer source rectangle is outside the texture: " + layer.source_path.string();
            result.rgba.clear();
            return result;
        }

        const float radians = layer.rotation_degrees * 0.01745329251994329577f;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const float local_left = -layer.pivot_x * layer.width;
        const float local_top = -layer.pivot_y * layer.height;
        const float local_right = local_left + layer.width;
        const float local_bottom = local_top + layer.height;
        const float radius = std::hypot(layer.width, layer.height);
        const int min_x = std::max(0, static_cast<int>(std::floor(layer.anchor_x - radius)));
        const int max_x = std::min(result.width - 1, static_cast<int>(std::ceil(layer.anchor_x + radius)));
        const int min_y = std::max(0, static_cast<int>(std::floor(layer.anchor_y - radius)));
        const int max_y = std::min(result.height - 1, static_cast<int>(std::ceil(layer.anchor_y + radius)));

        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const float relative_x = x + 0.5f - layer.anchor_x;
                const float relative_y = y + 0.5f - layer.anchor_y;
                const float local_x = cosine * relative_x + sine * relative_y;
                const float local_y = -sine * relative_x + cosine * relative_y;
                if (local_x < local_left || local_x >= local_right ||
                    local_y < local_top || local_y >= local_bottom) continue;
                float u = (local_x - local_left) / layer.width;
                float v = (local_y - local_top) / layer.height;
                if (layer.flip_x) u = 1.0f - u;
                if (layer.flip_y) v = 1.0f - v;
                const int sample_x = std::clamp(
                    static_cast<int>(source_x + u * source_width),
                    static_cast<int>(source_x), static_cast<int>(source_x + source_width) - 1
                );
                const int sample_y = std::clamp(
                    static_cast<int>(source_y + v * source_height),
                    static_cast<int>(source_y), static_cast<int>(source_y + source_height) - 1
                );
                const auto source_offset = (static_cast<std::size_t>(sample_y) * source.width + sample_x) * 4;
                const auto destination_offset = (static_cast<std::size_t>(y) * result.width + x) * 4;
                blend_pixel(result.rgba.data() + destination_offset,
                            source.rgba.data() + source_offset, layer.tint);
            }
        }
    }
    result.success = true;
    return result;
}

bool TextureCompositionService::save_png_atomic(
    const std::filesystem::path& output_path,
    const TextureCompositionResult& result,
    std::string& error
) {
    error.clear();
    if (!result.success || result.width <= 0 || result.height <= 0 ||
        result.rgba.size() != static_cast<std::size_t>(result.width) * result.height * 4) {
        error = "Texture composition result is incomplete";
        return false;
    }
    if (output_path.extension() != ".png") {
        error = "Texture composition output must use the .png extension";
        return false;
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
    if (filesystem_error) {
        error = "Could not create the output directory";
        return false;
    }
    SDL_Surface* surface = SDL_CreateSurface(result.width, result.height, SDL_PIXELFORMAT_RGBA32);
    if (!surface) { error = SDL_GetError(); return false; }
    for (int y = 0; y < result.height; ++y) {
        std::memcpy(
            static_cast<std::uint8_t*>(surface->pixels) + static_cast<std::size_t>(y) * surface->pitch,
            result.rgba.data() + static_cast<std::size_t>(y) * result.width * 4,
            static_cast<std::size_t>(result.width) * 4
        );
    }
    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto staging = std::filesystem::path(output_path.string() + ".compose-" +
        std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1)));
    if (!std::filesystem::create_directory(staging, filesystem_error)) {
        SDL_DestroySurface(surface);
        error = "Could not reserve a temporary output directory";
        return false;
    }
    struct TemporaryOutput {
        std::filesystem::path directory;
        ~TemporaryOutput() {
            std::error_code ignored;
            std::filesystem::remove(directory / "composite.png", ignored);
            std::filesystem::remove(directory, ignored);
        }
    } cleanup{staging};
    const auto temporary = staging / "composite.png";
    const bool saved = SDL_SavePNG(surface, temporary.string().c_str());
    SDL_DestroySurface(surface);
    if (!saved) { error = SDL_GetError(); return false; }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), output_path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Could not finalize the composed PNG (Windows error " +
            std::to_string(GetLastError()) + ")";
        return false;
    }
#else
    std::filesystem::rename(temporary, output_path, filesystem_error);
    if (filesystem_error) {
        error = "Could not finalize the composed PNG: " + filesystem_error.message();
        std::filesystem::remove(staging);
        return false;
    }
#endif
    return true;
}

} // namespace Engine
