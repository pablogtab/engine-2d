#include "editor/tools/TextureCompositionService.hpp"

#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <filesystem>

namespace {

std::filesystem::path temporary_directory() {
    const auto path = std::filesystem::temp_directory_path() /
        ("engine2d_texture_composition_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
    return path;
}

void write_texture(
    const std::filesystem::path& path,
    int width,
    int height,
    const std::vector<SDL_Color>& colors
) {
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    assert(surface && colors.size() == static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto color = colors[static_cast<std::size_t>(y * width + x)];
            assert(SDL_WriteSurfacePixel(surface, x, y, color.r, color.g, color.b, color.a));
        }
    }
    assert(SDL_SavePNG(surface, path.string().c_str()));
    SDL_DestroySurface(surface);
}

const std::uint8_t* pixel(const Engine::TextureCompositionResult& result, int x, int y) {
    return result.rgba.data() + (static_cast<std::size_t>(y) * result.width + x) * 4;
}

void test_order_alpha_crop_and_flip(const std::filesystem::path& directory) {
    const auto base = directory / "base.png";
    const auto overlay = directory / "overlay.png";
    write_texture(base, 1, 1, {{255, 0, 0, 255}});
    write_texture(overlay, 2, 1, {{0, 255, 0, 255}, {0, 0, 255, 255}});

    Engine::TextureCompositionSettings settings;
    settings.width = 2;
    settings.height = 1;
    Engine::TextureCompositionLayer top;
    top.source_path = overlay;
    top.use_source_rect = true;
    top.source_rect = {0, 0, 2, 1};
    top.anchor_x = 1;
    top.anchor_y = 0.5f;
    top.width = 2;
    top.height = 1;
    top.flip_x = true;
    top.tint.a = 128;
    top.sort_key = 2;
    Engine::TextureCompositionLayer bottom;
    bottom.source_path = base;
    bottom.anchor_x = 1;
    bottom.anchor_y = 0.5f;
    bottom.width = 2;
    bottom.height = 1;
    bottom.sort_key = 1;
    settings.layers = {top, bottom}; // Deliberately reverse visual order.

    const auto result = Engine::TextureCompositionService::compose(settings);
    assert(result.success);
    const auto* left = pixel(result, 0, 0);
    const auto* right = pixel(result, 1, 0);
    assert(left[0] >= 126 && left[2] >= 127 && left[1] == 0 && left[3] == 255);
    assert(right[0] >= 126 && right[1] >= 127 && right[2] == 0 && right[3] == 255);
}

void test_rotation_and_transparent_canvas(const std::filesystem::path& directory) {
    const auto source = directory / "bar.png";
    write_texture(source, 2, 1, {{255, 255, 255, 255}, {255, 255, 255, 255}});
    Engine::TextureCompositionSettings settings;
    settings.width = 5;
    settings.height = 5;
    Engine::TextureCompositionLayer layer;
    layer.source_path = source;
    layer.anchor_x = 2.5f;
    layer.anchor_y = 2.5f;
    layer.width = 2;
    layer.height = 1;
    layer.rotation_degrees = 90;
    settings.layers = {layer};
    const auto result = Engine::TextureCompositionService::compose(settings);
    assert(result.success);
    assert(pixel(result, 2, 1)[3] == 255);
    assert(pixel(result, 2, 2)[3] == 255);
    assert(pixel(result, 0, 0)[3] == 0);
}

void test_validation_and_save(const std::filesystem::path& directory) {
    Engine::TextureCompositionSettings invalid;
    std::string error;
    assert(!Engine::TextureCompositionService::validate(invalid, error));
    const auto source = directory / "save_source.png";
    write_texture(source, 1, 1, {{1, 2, 3, 4}});
    Engine::TextureCompositionLayer layer;
    layer.source_path = source;
    layer.anchor_x = 0.5f;
    layer.anchor_y = 0.5f;
    layer.width = 1;
    layer.height = 1;
    invalid.width = 1;
    invalid.height = 1;
    invalid.layers = {layer};
    const auto result = Engine::TextureCompositionService::compose(invalid);
    assert(result.success);
    const auto output = directory / "out" / "composite.png";
    assert(Engine::TextureCompositionService::save_png_atomic(output, result, error));
    assert(std::filesystem::exists(output));
    assert(!std::filesystem::exists(output.string() + ".composing"));
}

} // namespace

int main() {
    const auto directory = temporary_directory();
    test_order_alpha_crop_and_flip(directory);
    test_rotation_and_transparent_canvas(directory);
    test_validation_and_save(directory);
    std::filesystem::remove_all(directory);
    return 0;
}
