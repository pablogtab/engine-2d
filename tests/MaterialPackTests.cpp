#include "renderer/MaterialPack.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace {

std::unique_ptr<Engine::Texture> texture(int width, int height, std::vector<std::uint32_t> pixels) {
    return std::make_unique<Engine::Texture>(nullptr, width, height, pixels.data());
}

std::uint32_t at(const Engine::MaterialPackPixels& pack, int x, int y) {
    return pack.pixels[static_cast<std::size_t>(y) * pack.width + x];
}

} // namespace

int main() {
    auto albedo = texture(2, 2, {
        0x110000ffu, 0x220000ffu,
        0x330000ffu, 0x44000080u
    });
    auto normal = texture(2, 2, {
        0x8100ffffu, 0x8200ffffu,
        0x8300ffffu, 0x8400ffffu
    });
    auto emissive = texture(2, 2, {
        0xe10000ffu, 0xe20000ffu,
        0xe30000ffu, 0xe40000ffu
    });
    auto specular = texture(2, 2, {
        0xa10000ffu, 0xa20000ffu,
        0xa30000ffu, 0xa40000ffu
    });

    Engine::MaterialPackRequest request{
        albedo.get(), normal.get(), emissive.get(), specular.get(), {0, 0, 2, 2}
    };
    const auto packed = Engine::MaterialPackBuilder::build(request, 8);
    assert(packed.status == Engine::MaterialPackStatus::Ready);
    assert(packed.width == 4 && packed.height == 4);
    assert(at(packed, 0, 0) == 0x110000ffu);
    assert(at(packed, 1, 1) == 0x44000080u);
    assert(at(packed, 2, 0) == 0x8100ffffu);
    assert(at(packed, 3, 1) == 0x8400ffffu);
    assert(at(packed, 0, 2) == 0xe10000ffu);
    assert(at(packed, 1, 3) == 0xe40000ffu);
    assert(at(packed, 2, 2) == 0xa10000ffu);
    assert(at(packed, 3, 3) == 0xa40000ffu);

    Engine::MaterialPackRequest defaults{albedo.get(), nullptr, nullptr, nullptr, {0, 0, 2, 2}};
    const auto default_pack = Engine::MaterialPackBuilder::build(defaults, 8);
    assert(default_pack.status == Engine::MaterialPackStatus::Ready);
    assert(at(default_pack, 2, 0) == Engine::MaterialPackBuilder::FlatNormal);
    assert(at(default_pack, 0, 2) == Engine::MaterialPackBuilder::Black);
    assert(at(default_pack, 2, 2) == Engine::MaterialPackBuilder::Black);

    auto atlas = texture(4, 2, {
        0x010000ffu, 0x020000ffu, 0x030000ffu, 0x040000ffu,
        0x050000ffu, 0x060000ffu, 0x070000ffu, 0x080000ffu
    });
    auto frame_normal = texture(2, 2, {
        0x910000ffu, 0x920000ffu,
        0x930000ffu, 0x940000ffu
    });
    Engine::MaterialPackRequest frame{atlas.get(), frame_normal.get(), nullptr, nullptr, {2, 0, 2, 2}};
    const auto frame_pack = Engine::MaterialPackBuilder::build(frame, 8);
    assert(frame_pack.status == Engine::MaterialPackStatus::Ready);
    assert(at(frame_pack, 0, 0) == 0x030000ffu);
    assert(at(frame_pack, 1, 1) == 0x080000ffu);
    assert(at(frame_pack, 2, 0) == 0x910000ffu);
    assert(at(frame_pack, 3, 1) == 0x940000ffu);

    const auto first_key = Engine::MaterialPackBuilder::content_key(request);
    const auto same_key = Engine::MaterialPackBuilder::content_key(request);
    assert(first_key == same_key);
    Engine::MaterialPackRequest changed = request;
    changed.specular = nullptr;
    assert(first_key != Engine::MaterialPackBuilder::content_key(changed));
    auto changed_albedo = texture(2, 2, {
        0x110000ffu, 0x220000ffu,
        0x330000ffu, 0x45000080u
    });
    changed = request;
    changed.albedo = changed_albedo.get();
    assert(first_key != Engine::MaterialPackBuilder::content_key(changed));

    const auto too_large = Engine::MaterialPackBuilder::build(request, 3);
    assert(too_large.status == Engine::MaterialPackStatus::Unsupported);
    assert(too_large.pixels.empty());

    auto wrong_size = texture(3, 1, {0, 0, 0});
    Engine::MaterialPackRequest invalid_aux{albedo.get(), wrong_size.get(), nullptr, nullptr, {0, 0, 2, 2}};
    assert(Engine::MaterialPackBuilder::build(invalid_aux, 8).status == Engine::MaterialPackStatus::Unsupported);

    SDL_Surface* surface = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA8888);
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    assert(renderer);
    auto render_albedo = std::make_unique<Engine::Texture>(
        renderer, 2, 2, albedo->pixels().data()
    );
    const std::filesystem::path cache_directory =
        std::filesystem::temp_directory_path() / "engine2d-material-pack-tests";
    std::error_code filesystem_error;
    std::filesystem::remove_all(cache_directory, filesystem_error);
    Engine::MaterialPackCache cache(renderer, 8, 1024);
    cache.set_disk_cache_directory(cache_directory);
    Engine::MaterialPackRequest cached_request{
        render_albedo.get(), nullptr, nullptr, nullptr, {0, 0, 2, 2}
    };
    const auto cached_first = cache.request(cached_request);
    assert(cached_first.ready());
    assert(cache.metrics().misses == 1 && cache.metrics().disk_writes == 1);
    const auto cached_second = cache.request(cached_request);
    assert(cached_second.ready() && cached_second.texture == cached_first.texture);
    assert(cache.metrics().hits == 1);
    cache.clear();
    const auto disk_cached = cache.request(cached_request);
    assert(disk_cached.ready());
    assert(cache.metrics().disk_hits == 1);
    cache.set_memory_budget(1);
    assert(cache.metrics().resident_entries == 0);
    assert(cache.metrics().evictions == 1);
    assert(cache.request(cached_request).status == Engine::MaterialPackStatus::Unsupported);
    std::filesystem::remove_all(cache_directory, filesystem_error);
    render_albedo.reset();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);

    std::cout << "Material pack tests passed\n";
    return 0;
}
