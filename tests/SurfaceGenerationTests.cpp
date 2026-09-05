#include "editor/tools/SurfaceGenerationService.hpp"
#include "stb/stb_image.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>

namespace {

std::filesystem::path temporary_directory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("engine2d_surface_generation_" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

void write_source(const std::filesystem::path& path, int bias) {
    SDL_Surface* surface = SDL_CreateSurface(256, 256, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    for (int y = 0; y < surface->h; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            const int broad = ((x / 19 + y / 23 + bias) % 7) - 3;
            const int grain = ((x * 17 + y * 29 + bias * 11) % 13) - 6;
            const std::uint8_t value = static_cast<std::uint8_t>(
                std::clamp(92 + broad * 2 + grain, 0, 255)
            );
            assert(SDL_WriteSurfacePixel(surface, x, y, value, value, value, 255));
        }
    }
    assert(SDL_SavePNG(surface, path.string().c_str()));
    SDL_DestroySurface(surface);
}

Engine::SurfaceGenerationSettings settings_for(
    const std::filesystem::path& first,
    const std::filesystem::path& second
) {
    Engine::SurfaceGenerationSettings settings;
    settings.sources = {
        {first, 9.0f, true, true, false},
        {second, 1.0f, true, true, false}
    };
    settings.width = 512;
    settings.height = 384;
    settings.patch_size = 64;
    settings.overlap = 16;
    settings.candidate_count = 16;
    settings.repetition_radius = 3;
    settings.seed = 7319;
    settings.luminance_normalization = 0.25f;
    return settings;
}

void test_generation_is_deterministic_and_opaque(const std::filesystem::path& directory) {
    const auto first = directory / "source_a.png";
    const auto second = directory / "source_b.png";
    write_source(first, 0);
    write_source(second, 4);
    const auto settings = settings_for(first, second);

    const auto a = Engine::SurfaceGenerationService::generate(settings);
    const auto b = Engine::SurfaceGenerationService::generate(settings);
    assert(a.success && b.success);
    assert(a.content_hash == b.content_hash);
    assert(a.rgba == b.rgba);
    assert(a.width == settings.width && a.height == settings.height);
    assert(a.metrics.source_usage.size() == 2);
    assert(a.metrics.source_usage[0] > a.metrics.source_usage[1]);
    for (std::size_t index = 3; index < a.rgba.size(); index += 4) {
        assert(a.rgba[index] == 255);
    }

    auto changed = settings;
    changed.seed += 1;
    const auto c = Engine::SurfaceGenerationService::generate(changed);
    assert(c.success);
    assert(c.content_hash != a.content_hash);
}

void test_save_png_and_validation(const std::filesystem::path& directory) {
    const auto source = directory / "source_save.png";
    write_source(source, 2);
    auto settings = settings_for(source, source);
    settings.sources.resize(1);
    settings.width = 320;
    settings.height = 192;
    const auto result = Engine::SurfaceGenerationService::generate(settings);
    assert(result.success);

    const auto output = directory / "generated" / "surface.png";
    std::string error;
    assert(Engine::SurfaceGenerationService::save_png_atomic(output, result, error));
    assert(error.empty());
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load(output.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    assert(decoded);
    assert(width == settings.width && height == settings.height);
    assert(std::equal(result.rgba.begin(), result.rgba.end(), decoded));
    stbi_image_free(decoded);

    // Replacement retains exact bytes, and an invalid result cannot damage it.
    assert(Engine::SurfaceGenerationService::save_png_atomic(output, result, error));
    auto incomplete = result;
    incomplete.rgba.pop_back();
    assert(!Engine::SurfaceGenerationService::save_png_atomic(output, incomplete, error));
    decoded = stbi_load(output.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    assert(decoded && std::equal(result.rgba.begin(), result.rgba.end(), decoded));
    stbi_image_free(decoded);

    Engine::SurfaceGenerationSettings invalid = settings;
    invalid.overlap = invalid.patch_size;
    assert(!Engine::SurfaceGenerationService::generate(invalid).success);

    const auto directory_target = directory / "directory.png";
    std::filesystem::create_directory(directory_target);
    assert(!Engine::SurfaceGenerationService::save_png_atomic(directory_target, result, error));
    assert(std::filesystem::is_directory(directory_target));
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        assert(entry.path().filename().string().find(".surface-") == std::string::npos);
    }

    invalid = settings;
    invalid.sources.front().weight = std::numeric_limits<float>::quiet_NaN();
    assert(!Engine::SurfaceGenerationService::validate_settings(invalid, error));
    invalid = settings;
    invalid.luminance_normalization = std::numeric_limits<float>::infinity();
    assert(!Engine::SurfaceGenerationService::validate_settings(invalid, error));
    invalid = settings;
    invalid.repetition_radius = -1;
    assert(!Engine::SurfaceGenerationService::validate_settings(invalid, error));
    invalid = settings;
    invalid.sources.clear();
    assert(!Engine::SurfaceGenerationService::validate_settings(invalid, error));
    invalid = settings;
    invalid.patch_size = 512;
    assert(!Engine::SurfaceGenerationService::generate(invalid).success);
}

void test_cancel_during_generation(const std::filesystem::path& directory) {
    const auto source = directory / "source_live_cancel.png";
    write_source(source, 1);
    auto settings = settings_for(source, source);
    settings.width = 2048;
    settings.height = 2048;
    Engine::SurfaceGenerationControl control;
    Engine::SurfaceGenerationResult result;
    std::thread worker([&] { result = Engine::SurfaceGenerationService::generate(settings, &control); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (control.progress.load() == 0.0f && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    control.cancel.store(true);
    worker.join();
    assert(result.cancelled && !result.success && result.rgba.empty());
}

void test_cancel_before_generation(const std::filesystem::path& directory) {
    const auto source = directory / "source_cancel.png";
    write_source(source, 1);
    auto settings = settings_for(source, source);
    settings.sources.resize(1);
    Engine::SurfaceGenerationControl control;
    control.cancel.store(true);
    const auto result = Engine::SurfaceGenerationService::generate(settings, &control);
    assert(!result.success);
    assert(result.cancelled);
    assert(result.rgba.empty());
}

} // namespace

int main() {
    const auto directory = temporary_directory();
    test_generation_is_deterministic_and_opaque(directory);
    test_save_png_and_validation(directory);
    test_cancel_before_generation(directory);
    test_cancel_during_generation(directory);
    std::filesystem::remove_all(directory);
    std::cout << "SurfaceGenerationTests passed.\n";
    return 0;
}
