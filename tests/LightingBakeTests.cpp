#include "renderer/BakedLighting2DSystem.hpp"
#include "renderer/LightingBakeService.hpp"
#include "renderer/Renderer2D.hpp"
#include "renderer/Texture.hpp"
#include "scene/Camera2D.hpp"
#include "stb/stb_image.h"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

using Json = nlohmann::json;

void write_json(const std::filesystem::path& path, const Json& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << value.dump(2);
    assert(output.good());
}

void write_alpha_texture(const std::filesystem::path& path, bool extended) {
    std::filesystem::create_directories(path.parent_path());
    SDL_Surface* surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_ClearSurface(surface, 0, 0, 0, 0);
    for (int y = 1; y < 7; ++y) {
        for (int x = 1; x < 3; ++x) assert(SDL_WriteSurfacePixel(surface, x, y, 255, 255, 255, 255));
    }
    for (int y = 5; y < 7; ++y) {
        for (int x = 1; x < (extended ? 8 : 7); ++x) {
            assert(SDL_WriteSurfacePixel(surface, x, y, 255, 255, 255, 255));
        }
    }
    assert(SDL_SavePNG(surface, path.string().c_str()));
    SDL_DestroySurface(surface);
}

Json transform(float x, float y) {
    return {{"position", {x, y, 0.0f}}, {"scale", {1.0f, 1.0f}}, {"rotation", 0.0f}, {"pivot", {0.5f, 0.5f}}};
}

Json fixture_scene(float intensity) {
    return {
        {"scene_name", "BakeFixture"}, {"version", 1}, {"component_versions", Json::object()},
        {"settings", {{"physics_enabled", true}, {"gravity", {0.0f, -980.0f}}}},
        {"entities", Json::array({
            {{"id", 1}, {"transform", transform(0, 0)},
             {"lighting_bake_volume_2d", {{"size", {8.0f, 4.0f}}, {"texels_per_unit", 1.0f},
                                           {"affected_layers", 0xffffffffu}, {"enabled", true}}}},
            {{"id", 2}, {"transform", transform(0, 0)},
             {"sprite", {{"render_layer", 2}, {"size", {2.0f, 2.0f}}}},
             {"sprite_lighting_2d", {{"enabled", true}, {"baked_receiver", true}}}},
            {{"id", 3}, {"transform", transform(0, 0)},
             {"light_2d", {{"type", "Directional"}, {"mobility", "Baked"}, {"enabled", true},
                            {"color", {255, 255, 255, 255}}, {"intensity", intensity}, {"range", 16.0f},
                            {"falloff", 1.0f}, {"height", 64.0f}, {"affected_layers", 1u << 2},
                            {"shadow_mode", "Hard"}, {"shadow_strength", 1.0f},
                            {"shadow_max_distance", 32.0f}}}},
            {{"id", 4}, {"transform", transform(0, 0)},
             {"shadow_caster_2d", {{"source", "StaticPolygon"}, {"mobility", "Baked"},
                                    {"vertices", {{-0.5f, -2.0f}, {0.5f, -2.0f}, {0.5f, 2.0f}, {-0.5f, 2.0f}}},
                                    {"affected_layers", 1u << 2}, {"height", 64.0f}, {"opacity", 1.0f},
                                    {"enabled", true}}}}
        })}
    };
}

class BakedMaskProbe final : public Engine::ILightingPass {
public:
    const char* name() const override { return "BakedMaskProbe"; }
    bool is_enabled() const override { return true; }
    Engine::RenderLayerMask affected_layers() const override { return Engine::render_layer_mask(2); }
    Engine::LightingCompositionPhase composition_phase() const override {
        return Engine::LightingCompositionPhase::BakedMultiply;
    }
    bool requires_baked_receiver_mask() const override { return true; }
    bool apply(const Engine::LightingContext& context) override {
        SDL_Texture* previous = SDL_GetRenderTarget(context.renderer);
        assert(SDL_SetRenderTarget(context.renderer, context.baked_receiver_mask));
        for (int index = 0; index < 2; ++index) {
            const SDL_Rect rect{index == 0 ? 8 : 24, 8, 1, 1};
            SDL_Surface* sample = SDL_RenderReadPixels(context.renderer, &rect);
            assert(sample);
            std::uint8_t red = 0, green = 0, blue = 0;
            assert(SDL_ReadSurfacePixel(sample, 0, 0, &red, &green, &blue, &alpha[index]));
            SDL_DestroySurface(sample);
        }
        SDL_SetRenderTarget(context.renderer, previous);
        return true;
    }
    std::uint8_t alpha[2]{};
};

void test_baked_composition_only_affects_receivers(
    const Engine::LightingBakeManifest& manifest
) {
    SDL_Surface* tile = SDL_CreateSurface(8, 4, SDL_PIXELFORMAT_RGBA32);
    assert(tile);
    for (int y = 0; y < tile->h; ++y) {
        for (int x = 0; x < tile->w; ++x) {
            assert(SDL_WriteSurfacePixel(tile, x, y, 64, 64, 64, 255));
        }
    }
    assert(SDL_SavePNG(tile, manifest.tiles[0].image_path.string().c_str()));
    SDL_DestroySurface(tile);

    SDL_Surface* output = SDL_CreateSurface(32, 16, SDL_PIXELFORMAT_RGBA32);
    assert(output);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(output);
    assert(sdl_renderer);
    {
        Engine::Renderer2D renderer(sdl_renderer);
        Engine::Camera2D camera(32.0f, 16.0f);
        camera.set_zoom(4.0f);
        std::unique_ptr<Engine::Texture> white(
            Engine::Texture::create_solid(sdl_renderer, 2, 2, 255, 255, 255, 255)
        );
        auto pass = std::make_shared<Engine::BakedLighting2DPass>();
        auto probe = std::make_shared<BakedMaskProbe>();
        assert(pass->load(manifest, sdl_renderer));
        renderer.add_lighting_pass(probe);
        renderer.add_lighting_pass(pass);

        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {-2.0f, 0.0f, 0.0f}, {2.0f, 2.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 2, {}, 0, SDL_BLENDMODE_BLEND,
            true, nullptr, true
        );
        renderer.submit_sprite(
            white.get(), {2.0f, 0.0f, 0.0f}, {2.0f, 2.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 2, {}, 0, SDL_BLENDMODE_BLEND,
            false, nullptr, true
        );
        renderer.end_scene();
        assert(renderer.present());

        std::uint8_t receiver_red = 0, green = 0, blue = 0, alpha = 0;
        std::uint8_t rejected_red = 0;
        assert(SDL_ReadSurfacePixel(output, 8, 8, &receiver_red, &green, &blue, &alpha));
        assert(SDL_ReadSurfacePixel(output, 24, 8, &rejected_red, &green, &blue, &alpha));
        assert(probe->alpha[0] > 250 && probe->alpha[1] < 5);
        assert(receiver_red >= 60 && receiver_red <= 68);
        assert(rejected_red >= 250);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(output);
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "engine2d-lighting-bake-v2-tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    write_json(root / "ProjectSettings/Project.json", Json{{"lighting", Json::object()}});
    const auto scene = root / "Assets/Scenes/BakeFixture.scene";
    write_json(scene, fixture_scene(1.0f));

    const Engine::LightingBakeStatus missing = Engine::LightingBakeService::status(root, scene);
    assert(missing.has_bake_volume && !missing.current);
    Engine::LightingBakeStatus baked;
    std::string bake_error;
    assert(Engine::LightingBakeService::bake(root, scene, &baked, &bake_error));
    assert(baked.current && baked.tile_count == 1);
    assert(baked.baked_light_count == 1 && baked.baked_caster_count == 1);

    const auto manifest = Engine::LightingBakeService::load_current(root, scene);
    assert(manifest && manifest->tiles.size() == 1);
    assert(manifest->tiles[0].render_layer == 2);
    assert(manifest->tiles[0].affected_layers == (1u << 2));
    assert(manifest->tiles[0].pixel_width == 8 && manifest->tiles[0].pixel_height == 4);
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(manifest->tiles[0].image_path.string().c_str(), &width, &height, &channels, 4);
    assert(pixels && width == 8 && height == 4);
    const auto red_at = [&](int x, int y) { return pixels[(y * width + x) * 4]; };
    assert(red_at(1, 2) < 8);
    assert(red_at(7, 2) > 245);
    stbi_image_free(pixels);

    test_baked_composition_only_affects_receivers(*manifest);

    write_json(scene, fixture_scene(0.5f));
    const Engine::LightingBakeStatus stale = Engine::LightingBakeService::status(root, scene);
    assert(!stale.current && stale.reason.find("stale") != std::string::npos);
    const auto previous = Engine::LightingBakeService::load_latest_valid(root, scene);
    assert(previous && !previous->status.current);
    assert(previous->status.input_hash == baked.input_hash);

    Json multiple_volumes = fixture_scene(0.5f);
    multiple_volumes["entities"].push_back({
        {"id", 5}, {"transform", transform(8, 0)},
        {"lighting_bake_volume_2d", {{"size", {8.0f, 4.0f}}, {"texels_per_unit", 1.0f},
                                       {"affected_layers", 0xffffffffu}, {"enabled", true}}}
    });
    write_json(scene, multiple_volumes);
    assert(Engine::LightingBakeService::bake(root, scene, &baked, &bake_error));
    const auto multi_manifest = Engine::LightingBakeService::load_current(root, scene);
    assert(multi_manifest && multi_manifest->tiles.size() == 2);
    assert(multi_manifest->tiles[0].image_path != multi_manifest->tiles[1].image_path);

    Json no_receivers = fixture_scene(0.5f);
    no_receivers["entities"][1].erase("sprite_lighting_2d");
    write_json(scene, no_receivers);
    assert(Engine::LightingBakeService::bake(root, scene, &baked, &bake_error));
    const auto empty_manifest = Engine::LightingBakeService::load_current(root, scene);
    assert(empty_manifest && empty_manifest->tiles.empty());

    const std::string texture_handle = "0123456789abcdef";
    const auto alpha_texture = root / "Assets/Textures/alpha-caster.png";
    write_alpha_texture(alpha_texture, false);
    write_json(root / "ProjectSettings/AssetDatabase.json", {
        {"version", 1}, {"assets", Json::array({
            {{"handle", texture_handle}, {"path", "Assets/Textures/alpha-caster.png"},
             {"status", "ready"}, {"type", "texture"}}
        })}
    });
    Json alpha_scene = fixture_scene(0.5f);
    alpha_scene["entities"][3].erase("shadow_caster_2d");
    alpha_scene["entities"][3]["sprite"] = {
        {"texture_handle", texture_handle}, {"size", {8.0f, 8.0f}},
        {"src_rect", {0.0f, 0.0f, 8.0f, 8.0f}}, {"use_src_rect", true}
    };
    alpha_scene["entities"][3]["shadow_caster_2d"] = {
        {"source", "SpriteAlphaContour"}, {"mobility", "Baked"},
        {"affected_layers", 1u << 2}, {"height", 64.0f}, {"opacity", 1.0f},
        {"alpha_threshold", 0.5f}, {"contour_tolerance", 0.0f}, {"enabled", true}
    };
    write_json(scene, alpha_scene);
    assert(Engine::LightingBakeService::bake(root, scene, &baked, &bake_error));
    assert(baked.current && baked.baked_caster_count == 1);
    write_alpha_texture(alpha_texture, true);
    assert(!Engine::LightingBakeService::status(root, scene).current);

    std::filesystem::remove_all(root, error);
    std::cout << "Lighting bake tests passed\n";
    return 0;
}
