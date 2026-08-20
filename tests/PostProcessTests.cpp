#include "renderer/postprocess/IPostProcessPass.hpp"
#include "renderer/postprocess/PostProcessPipeline.hpp"
#include "renderer/postprocess/PostProcessPasses.hpp"
#include "renderer/postprocess/PostProcessProfile.hpp"
#include "renderer/SpriteSortKey.hpp"
#include "renderer/Renderer2D.hpp"
#include "renderer/material/FragmentShader.hpp"
#include "renderer/material/IMaterialBackend.hpp"
#include "renderer/material/Material.hpp"
#include "renderer/postprocess/MaterialPostProcessPass.hpp"
#include "renderer/Texture.hpp"
#include "scene/Camera2D.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/PostProcessProfileCommands.hpp"
#include "core/AssetDatabase.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

class RecordingMaterialBackend final : public Engine::IMaterialBackend {
public:
    bool is_available() const override { return true; }
    const char* backend_name() const override { return "recording"; }

    bool bind(const Engine::Material& material, const Engine::MaterialRenderContext&) override {
        ++bind_count;
        last_material = &material;
        return true;
    }

    void unbind() override { ++unbind_count; }

    bool draw_fullscreen(
        const Engine::Material& material,
        SDL_Texture*,
        const Engine::MaterialRenderContext&
    ) override {
        ++fullscreen_count;
        last_material = &material;
        return true;
    }

    int bind_count{0};
    int unbind_count{0};
    int fullscreen_count{0};
    const Engine::Material* last_material{nullptr};
};

void test_night_vision_shader_assets_match_runtime_contract() {
    const std::filesystem::path shader_directory =
        std::filesystem::path(ENGINE_SOURCE_DIR) / "assets" / "shaders";
    const auto canonical_source = shader_directory / "night_vision.frag.hlsl";
    const auto metal_variant = shader_directory / "night_vision.frag.msl";

    assert(std::filesystem::is_regular_file(canonical_source));
    assert(std::filesystem::file_size(canonical_source) > 0);

    Engine::FragmentShader shader("NightVision");
    shader.set_sampler_count(1);
    shader.set_uniform_buffer_count(1);
    assert(shader.add_variant_from_file(
        Engine::ShaderBinaryFormat::Msl,
        metal_variant.string(),
        "main0"
    ));
    assert(shader.sampler_count() == 1);
    assert(shader.uniform_buffer_count() == 1);
    assert(shader.variants().size() == 1);
    assert(shader.variants().front().format == Engine::ShaderBinaryFormat::Msl);
    assert(shader.variants().front().entry_point == "main0");
    assert(!shader.variants().front().code.empty());
    assert(shader.variants().front().code.back() == 0);
}

void test_material_contract_is_shared_by_sprites_and_post_process() {
    auto shader = std::make_shared<Engine::FragmentShader>("TestGrade");
    shader->set_uniform_buffer_count(1);
    shader->add_variant({Engine::ShaderBinaryFormat::Msl, {1, 2, 3}, "main0"});

    auto material = std::make_shared<Engine::Material>("TestGradeInstance", shader);
    struct alignas(16) GradeUniforms {
        float exposure;
        float saturation;
        float padding[2];
    } uniforms{1.25f, 0.75f, {0.0f, 0.0f}};
    material->set_uniform(0, uniforms);
    assert(material->uniform_bytes(0).size() == sizeof(GradeUniforms));

    auto backend = std::make_shared<RecordingMaterialBackend>();
    Engine::MaterialPostProcessPass pass(material, backend);
    Engine::PostProcessContext context{1280, 720, 1280, 720};
    pass.apply(nullptr, reinterpret_cast<SDL_Texture*>(1), nullptr, context);
    assert(backend->fullscreen_count == 1);
    assert(backend->last_material == material.get());

    SDL_Surface* surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);

    Engine::Renderer2D renderer(sdl_renderer);
    renderer.set_material_backend(backend);
    Engine::Camera2D camera(64.0f, 64.0f);
    renderer.begin_scene(camera);
    renderer.submit_sprite(
        nullptr,
        {0.0f, 0.0f, 0.0f},
        {8.0f, 8.0f},
        0.0f,
        {255, 255, 255, 255},
        nullptr,
        {0.5f, 0.5f},
        false,
        false,
        false,
        0,
        0,
        material
    );
    renderer.end_scene();
    assert(backend->bind_count == 1);
    assert(backend->unbind_count == 1);
    assert(backend->last_material == material.get());

    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

} // namespace

namespace {

bool nearly_equal(float a, float b, float epsilon = 0.0001f) {
    return std::abs(a - b) <= epsilon;
}

void test_effect_scale_matches_authoring_resolution() {
    Engine::PostProcessContext native{1280, 720, 1280, 720};
    assert(nearly_equal(native.horizontal_scale(), 1.0f));
    assert(nearly_equal(native.vertical_scale(), 1.0f));
    assert(nearly_equal(native.pixel_scale(), 1.0f));

    Engine::PostProcessContext full_hd{1920, 1080, 1280, 720};
    assert(nearly_equal(full_hd.horizontal_scale(), 1.5f));
    assert(nearly_equal(full_hd.vertical_scale(), 1.5f));
    assert(nearly_equal(full_hd.scale_pixels(4.0f), 6.0f));

    Engine::PostProcessContext half{640, 360, 1280, 720};
    assert(nearly_equal(half.pixel_scale(), 0.5f));
    assert(nearly_equal(half.scale_pixels(4.0f), 2.0f));
}

void test_non_uniform_output_uses_area_preserving_pixel_scale() {
    Engine::PostProcessContext context{1920, 720, 1280, 720};
    assert(nearly_equal(context.horizontal_scale(), 1.5f));
    assert(nearly_equal(context.vertical_scale(), 1.0f));
    assert(nearly_equal(context.pixel_scale(), std::sqrt(1.5f)));
}

void test_world_sort_keys_never_overlap_the_screen_space_partition() {
    const uint64_t lowest_world = Engine::SpriteSortKey::world(-32768, -1000.0f, -32768.0f, -32768);
    const uint64_t highest_world = Engine::SpriteSortKey::world(32767, 10000.0f, 32767.0f, 32767);
    const uint64_t screen = Engine::SpriteSortKey::screen(0.0f, nullptr);

    assert(!Engine::SpriteSortKey::is_screen_space(lowest_world));
    assert(!Engine::SpriteSortKey::is_screen_space(highest_world));
    assert(Engine::SpriteSortKey::is_screen_space(screen));
    assert(lowest_world < highest_world);
    assert(highest_world < screen);
}

void test_profile_round_trip_preserves_effect_reference_and_parameters() {
    Engine::PostProcessProfile source = Engine::PostProcessProfile::PresetRetroCRT();
    source.effect_reference_width = 1600;
    source.effect_reference_height = 900;
    source.blur_enabled = true;
    source.blur_radius = 2.25f;
    source.shadow_tint = SDL_Color{12, 34, 56, 78};

    Engine::PostProcessProfile restored;
    assert(restored.from_json(source.to_json()));
    assert(restored.name == source.name);
    assert(restored.effect_reference_width == 1600);
    assert(restored.effect_reference_height == 900);
    assert(restored.blur_enabled);
    assert(nearly_equal(restored.blur_radius, 2.25f));
    assert(restored.shadow_tint.r == 12);
    assert(restored.shadow_tint.g == 34);
    assert(restored.shadow_tint.b == 56);
    assert(restored.shadow_tint.a == 78);
    assert(restored.crt_enabled == source.crt_enabled);
    assert(nearly_equal(restored.crt_scanline_spacing, source.crt_scanline_spacing));
}

void test_profile_updates_pipeline_reference_without_renderer() {
    Engine::PostProcessPipeline pipeline;
    pipeline.init(nullptr, 1920, 1080);

    Engine::PostProcessProfile profile;
    profile.effect_reference_width = 960;
    profile.effect_reference_height = 540;
    profile.apply_to_pipeline(pipeline);

    const auto& context = pipeline.get_context();
    assert(context.output_width == 1920);
    assert(context.output_height == 1080);
    assert(context.authoring_width == 960);
    assert(context.authoring_height == 540);
    assert(nearly_equal(context.pixel_scale(), 2.0f));
}

void test_profile_edit_is_undoable() {
    Engine::PostProcessPipeline pipeline;
    pipeline.init(nullptr, 1920, 1080);
    Engine::PostProcessProfile profile = Engine::PostProcessProfile::PresetDefault();
    Engine::PostProcessProfile changed = profile;
    changed.bloom_intensity = 1.25f;
    changed.effect_reference_width = 1920;
    changed.effect_reference_height = 1080;

    Engine::EditorCommandHistory history;
    history.execute_command(std::make_unique<Engine::ModifyPostProcessProfileCommand>(
        &profile, &pipeline, profile, changed
    ));
    assert(nearly_equal(profile.bloom_intensity, 1.25f));
    assert(pipeline.get_context().authoring_width == 1920);

    history.undo();
    assert(nearly_equal(profile.bloom_intensity, Engine::PostProcessProfile::PresetDefault().bloom_intensity));
    assert(pipeline.get_context().authoring_width == 1280);

    history.redo();
    assert(nearly_equal(profile.bloom_intensity, 1.25f));
    assert(pipeline.get_context().authoring_width == 1920);
}

void test_pipeline_processes_the_existing_camera_backdrop() {
    constexpr int width = 32;
    constexpr int height = 24;
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    assert(surface != nullptr);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer != nullptr);

    SDL_Texture* backdrop = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        width,
        height
    );
    assert(backdrop != nullptr);
    SDL_SetRenderTarget(renderer, backdrop);
    SDL_SetRenderDrawColor(renderer, 100, 120, 140, 255);
    SDL_RenderClear(renderer);

    Engine::PostProcessPipeline pipeline;
    pipeline.init(renderer, width, height, width, height);
    auto grading = std::make_shared<Engine::ColorGradingPass>();
    grading->set_tint(SDL_Color{128, 255, 255, 255});
    pipeline.add_pass(grading);

    pipeline.begin_capture(backdrop);
    pipeline.end_capture_and_process(nullptr);
    SDL_RenderPresent(renderer);

    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    Uint8 alpha = 0;
    assert(SDL_ReadSurfacePixel(surface, width / 2, height / 2, &red, &green, &blue, &alpha));
    assert(red >= 48 && red <= 52);
    assert(green >= 118 && green <= 122);
    assert(blue >= 138 && blue <= 142);
    assert(alpha == 255);

    pipeline.destroy_targets();
    SDL_DestroyTexture(backdrop);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

void test_pipeline_processes_the_standalone_clear_color() {
    constexpr int width = 32;
    constexpr int height = 24;
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    assert(surface != nullptr);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer != nullptr);

    Engine::PostProcessPipeline pipeline;
    pipeline.init(renderer, width, height, width, height);
    auto grading = std::make_shared<Engine::ColorGradingPass>();
    grading->set_tint(SDL_Color{128, 255, 255, 255});
    pipeline.add_pass(grading);

    pipeline.begin_capture(nullptr, SDL_Color{100, 120, 140, 255});
    pipeline.end_capture_and_process(nullptr);
    SDL_RenderPresent(renderer);

    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    Uint8 alpha = 0;
    assert(SDL_ReadSurfacePixel(surface, width / 2, height / 2, &red, &green, &blue, &alpha));
    assert(red >= 48 && red <= 52);
    assert(green >= 118 && green <= 122);
    assert(blue >= 138 && blue <= 142);
    assert(alpha == 255);

    pipeline.destroy_targets();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

void test_world_is_processed_before_screen_space_hud_is_composited() {
    constexpr int width = 64;
    constexpr int height = 32;
    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    assert(surface != nullptr);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer != nullptr);

    Engine::Renderer2D renderer(sdl_renderer);
    renderer.set_scene_clear_color(SDL_Color{0, 0, 0, 255});
    Engine::Camera2D camera(static_cast<float>(width), static_cast<float>(height));

    Engine::PostProcessPipeline pipeline;
    pipeline.init(sdl_renderer, width, height, width, height);
    auto grading = std::make_shared<Engine::ColorGradingPass>();
    grading->set_tint(SDL_Color{128, 255, 128, 255});
    pipeline.add_pass(grading);

    std::unique_ptr<Engine::Texture> white(
        Engine::Texture::create_solid(sdl_renderer, 4, 4, 255, 255, 255, 255)
    );
    assert(white != nullptr);

    renderer.set_post_process_pipeline(&pipeline);
    renderer.begin_scene(camera);
    renderer.submit_sprite(
        white.get(),
        glm::vec3(-16.0f, 0.0f, 0.0f),
        glm::vec2(16.0f, 16.0f),
        0.0f,
        SDL_Color{255, 255, 255, 255}
    );
    renderer.submit_screen_sprite(
        white.get(),
        glm::vec2(48.0f, 16.0f),
        glm::vec2(16.0f, 16.0f),
        0.0f,
        SDL_Color{255, 255, 255, 255},
        nullptr,
        glm::vec2(0.5f, 0.5f)
    );
    renderer.end_scene();
    SDL_RenderPresent(sdl_renderer);

    Uint8 world_r = 0;
    Uint8 world_g = 0;
    Uint8 world_b = 0;
    Uint8 world_a = 0;
    assert(SDL_ReadSurfacePixel(surface, 16, 16, &world_r, &world_g, &world_b, &world_a));
    assert(world_r >= 126 && world_r <= 130);
    assert(world_g == 255);
    assert(world_b >= 126 && world_b <= 130);
    assert(world_a == 255);

    Uint8 hud_r = 0;
    Uint8 hud_g = 0;
    Uint8 hud_b = 0;
    Uint8 hud_a = 0;
    assert(SDL_ReadSurfacePixel(surface, 48, 16, &hud_r, &hud_g, &hud_b, &hud_a));
    assert(hud_r == 255);
    assert(hud_g == 255);
    assert(hud_b == 255);
    assert(hud_a == 255);

    pipeline.destroy_targets();
    white.reset();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

void test_profile_assets_round_trip_by_handle() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_postprocess_asset_test";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root / "assets/postprocess");
    Engine::AssetDatabase database(root);
    Engine::PostProcessProfile source = Engine::PostProcessProfile::PresetRetroCRT();
    source.name = "AssetProfile";
    const fs::path path = root / "assets/postprocess/retro.postprocess";
    assert(source.save_asset(path.string(), database));
    const auto* record = database.find_by_path("assets/postprocess/retro.postprocess");
    assert(record && record->type == Engine::AssetType::PostProcessProfile);
    const auto loaded = Engine::PostProcessProfile::load_asset(record->handle, database);
    assert(loaded && loaded->name == "AssetProfile");
    fs::remove_all(root, ignored);
}

} // namespace

int main() {
    test_night_vision_shader_assets_match_runtime_contract();
    test_material_contract_is_shared_by_sprites_and_post_process();
    test_effect_scale_matches_authoring_resolution();
    test_non_uniform_output_uses_area_preserving_pixel_scale();
    test_world_sort_keys_never_overlap_the_screen_space_partition();
    test_profile_round_trip_preserves_effect_reference_and_parameters();
    test_profile_updates_pipeline_reference_without_renderer();
    test_profile_edit_is_undoable();
    test_pipeline_processes_the_existing_camera_backdrop();
    test_pipeline_processes_the_standalone_clear_color();
    test_world_is_processed_before_screen_space_hud_is_composited();
    test_profile_assets_round_trip_by_handle();
    std::cout << "PostProcessTests passed\n";
    return 0;
}
