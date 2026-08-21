#include "core/AssetDatabase.hpp"
#include "core/AssetManager.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/UIComponents.hpp"
#include "renderer/Renderer2D.hpp"
#include "scene/Camera2D.hpp"
#include "ui/CanvasUISystem.hpp"
#include "ui/UIInputSystem.hpp"
#include "ui/UITextLayout.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

using namespace Engine;

EntityID add_fullscreen_button(
    EntityManager& entities,
    std::int16_t canvas_order,
    SDL_Color normal,
    SDL_Color hovered,
    const glm::vec2& reference_size = {64.0f, 64.0f}
) {
    const EntityID canvas_entity = entities.create_entity();
    CanvasComponent canvas;
    canvas.reference_size = reference_size;
    canvas.scaling_policy = PresentationMode::Stretch;
    canvas.sorting_order = canvas_order;
    canvas.use_safe_area = false;
    entities.add_component<CanvasComponent>(canvas_entity, canvas);
    entities.add_component<HierarchyComponent>(canvas_entity, HierarchyComponent{});

    const EntityID button_entity = entities.create_entity();
    UILayoutComponent layout;
    layout.anchor_min = {0.0f, 0.0f};
    layout.anchor_max = {1.0f, 1.0f};
    layout.size_delta = {0.0f, 0.0f};
    UIButtonComponent button;
    button.normal_color = normal;
    button.hovered_color = hovered;
    entities.add_component<HierarchyComponent>(button_entity, HierarchyComponent{canvas_entity});
    entities.add_component<UILayoutComponent>(button_entity, layout);
    entities.add_component<UIButtonComponent>(button_entity, button);
    entities.get_component<HierarchyComponent>(canvas_entity).children.push_back(button_entity);
    return button_entity;
}

void read_pixel(SDL_Surface* surface, int x, int y, SDL_Color& color) {
    assert(SDL_ReadSurfacePixel(surface, x, y, &color.r, &color.g, &color.b, &color.a));
}

void test_canvas_rendering_uses_sorting_and_runtime_button_state() {
    SDL_Surface* surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        namespace fs = std::filesystem;
        const fs::path asset_root = fs::temp_directory_path() / "engine2d_canvas_ui_render_tests";
        std::error_code error;
        fs::remove_all(asset_root, error);
        fs::create_directories(asset_root / "assets/fonts");
        fs::copy_file(
            fs::path(__FILE__).parent_path().parent_path() / "vendor/lucide/lucide.ttf",
            asset_root / "assets/fonts/Test.ttf",
            fs::copy_options::overwrite_existing,
            error
        );
        assert(!error);
        AssetDatabase database(asset_root);
        static_cast<void>(database.scan(asset_root / "assets"));
        const AssetRecord* font_record = database.find_by_path("assets/fonts/Test.ttf");
        assert(font_record && font_record->type == AssetType::Font);

        EntityManager entities;
        static_cast<void>(add_fullscreen_button(
            entities, 0, SDL_Color{255, 0, 0, 255}, SDL_Color{255, 0, 0, 255}
        ));
        const EntityID top = add_fullscreen_button(
            entities, 2, SDL_Color{0, 0, 255, 255}, SDL_Color{0, 255, 0, 255}
        );
        UITextComponent text;
        text.text = "A\nB";
        text.font_handle = font_record->handle;
        text.font_size = 12.0f;
        entities.add_component<UITextComponent>(top, text);

        UIInputSystem input;
        const auto input_result = input.update(entities, {64.0f, 64.0f}, {}, {{32.0f, 32.0f}});
        assert(input_result.hovered == top);

        Renderer2D renderer(sdl_renderer);
        Camera2D camera(64.0f, 64.0f);
        camera.set_reference_size(128.0f, 128.0f);
        CanvasUISystem ui;
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        renderer.begin_scene(camera);
        const auto stats = ui.render(
            entities, renderer, database, {64.0f, 64.0f}, {}, &input
        );
        renderer.end_scene();
        SDL_RenderPresent(sdl_renderer);
        assert(stats.canvases == 2);
        assert(stats.button_backgrounds == 2);
        assert(stats.text_lines == 2);
        assert(renderer.get_frame_metrics().stage(RenderStage::CanvasUI).sprites == 2);

        SDL_Color pixel{};
        read_pixel(surface, 60, 60, pixel);
        assert(pixel.r == 0 && pixel.g == 255 && pixel.b == 0 && pixel.a == 255);
        fs::remove_all(asset_root, error);
    }
    AssetManager::get().clear();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

void test_editor_preview_draws_a_camera_relative_canvas_frame() {
    SDL_Surface* surface = SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        const std::filesystem::path asset_root = std::filesystem::temp_directory_path() /
            "engine2d_canvas_ui_editor_render_tests";
        std::error_code error;
        std::filesystem::remove_all(asset_root, error);
        std::filesystem::create_directories(asset_root / "assets");
        AssetDatabase database(asset_root);

        EntityManager entities;
        static_cast<void>(add_fullscreen_button(
            entities,
            0,
            SDL_Color{255, 0, 0, 255},
            SDL_Color{255, 0, 0, 255},
            {40.0f, 20.0f}
        ));
        EntityID canvas_entity = 0;
        for (const EntityID entity : entities.get_living_entities()) {
            if (entities.has_component<CanvasComponent>(entity)) {
                canvas_entity = entity;
                break;
            }
        }
        assert(canvas_entity != 0);
        entities.add_component<TransformComponent>(canvas_entity, TransformComponent{});

        Renderer2D renderer(sdl_renderer);
        Camera2D camera(128.0f, 128.0f);
        camera.set_reference_size(256.0f, 256.0f);
        CanvasUISystem ui;
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        renderer.begin_scene(camera);
        const auto stats = ui.render_editor_preview(entities, renderer, database, camera);
        renderer.end_scene();
        SDL_RenderPresent(sdl_renderer);

        assert(stats.canvases == 1);
        assert(stats.button_backgrounds == 1);
        assert(stats.editor_frames == 1);
        assert(renderer.get_frame_metrics().stage(RenderStage::CanvasUI).sprites == 5);

        SDL_Color interior{};
        read_pixel(surface, 64, 64, interior);
        assert(interior.r == 255 && interior.g == 0 && interior.b == 0);

        SDL_Color border{};
        read_pixel(surface, 54, 59, border);
        assert(border.b > border.r && border.g > border.r);

        std::filesystem::remove_all(asset_root, error);
    }
    AssetManager::get().clear();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

void test_centered_text_uses_the_font_baseline_inside_its_rect() {
    SDL_Surface* surface = SDL_CreateSurface(96, 96, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        const std::filesystem::path asset_root = std::filesystem::temp_directory_path() /
            "engine2d_canvas_ui_centered_text_tests";
        std::error_code error;
        std::filesystem::remove_all(asset_root, error);
        std::filesystem::create_directories(asset_root / "assets");
        AssetDatabase database(asset_root);

        EntityManager entities;
        const EntityID canvas_entity = entities.create_entity();
        CanvasComponent canvas;
        canvas.reference_size = {96.0f, 96.0f};
        canvas.scaling_policy = PresentationMode::Stretch;
        canvas.use_safe_area = false;
        entities.add_component<CanvasComponent>(canvas_entity, canvas);
        entities.add_component<HierarchyComponent>(canvas_entity, HierarchyComponent{});

        const EntityID text_entity = entities.create_entity();
        UILayoutComponent layout;
        layout.size_delta = {64.0f, 40.0f};
        UITextComponent text;
        text.text = "Text";
        text.font_size = 24.0f;
        text.horizontal_alignment = UIHorizontalAlignment::Center;
        text.vertical_alignment = UIVerticalAlignment::Center;
        entities.add_component<HierarchyComponent>(
            text_entity, HierarchyComponent{canvas_entity}
        );
        entities.add_component<UILayoutComponent>(text_entity, layout);
        entities.add_component<UITextComponent>(text_entity, text);
        entities.get_component<HierarchyComponent>(canvas_entity).children.push_back(text_entity);

        Renderer2D renderer(sdl_renderer);
        Camera2D camera(96.0f, 96.0f);
        CanvasUISystem ui;
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        renderer.begin_scene(camera);
        const auto stats = ui.render(entities, renderer, database, {96.0f, 96.0f});
        renderer.end_scene();
        SDL_RenderPresent(sdl_renderer);
        assert(stats.text_lines == 1);

        int minimum_y = 96;
        int maximum_y = -1;
        for (int y = 0; y < 96; ++y) {
            for (int x = 0; x < 96; ++x) {
                SDL_Color pixel{};
                read_pixel(surface, x, y, pixel);
                if (pixel.r == 0 && pixel.g == 0 && pixel.b == 0) continue;
                minimum_y = std::min(minimum_y, y);
                maximum_y = std::max(maximum_y, y);
            }
        }
        assert(maximum_y >= minimum_y);
        assert(minimum_y >= 28);
        assert(maximum_y < 68);
        const float ink_center_y = static_cast<float>(minimum_y + maximum_y) * 0.5f;
        assert(std::abs(ink_center_y - 48.0f) <= 4.0f);

        std::filesystem::remove_all(asset_root, error);
    }
    AssetManager::get().clear();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

void test_text_margins_inset_the_content_rect_in_canvas_pixels() {
    UITextComponent text;
    text.margins.left = {4.0f, UILengthUnit::Pixels};
    text.margins.top = {6.0f, UILengthUnit::Pixels};
    text.margins.right = {10.0f, UILengthUnit::Percent};
    text.margins.bottom = {-50.0f, UILengthUnit::Percent};
    text.fit_padding = {1.0f, 2.0f, 3.0f, 4.0f};
    const UIRect content = calculate_ui_text_content_rect(
        {{10.0f, 20.0f}, {200.0f, 100.0f}},
        text,
        {2.0f, 0.5f}
    );
    assert(content.position == glm::vec2(20.0f, 24.0f));
    assert(content.size == glm::vec2(164.0f, 144.0f));

    text.margins.left = {20.0f, UILengthUnit::Pixels};
    text.margins.top = {20.0f, UILengthUnit::Pixels};
    text.margins.right = {20.0f, UILengthUnit::Pixels};
    text.margins.bottom = {20.0f, UILengthUnit::Pixels};
    text.fit_padding = glm::vec4(0.0f);
    const UIRect collapsed = calculate_ui_text_content_rect(
        {{0.0f, 0.0f}, {20.0f, 10.0f}},
        text,
        {1.0f, 1.0f}
    );
    assert(collapsed.position == glm::vec2(20.0f, 20.0f));
    assert(collapsed.size == glm::vec2(0.0f));
}

void test_text_fit_content_solves_percentage_insets() {
    UITextComponent text;
    text.text.clear();
    text.fit_content_width = true;
    text.fit_content_height = true;
    text.fit_padding = {10.0f, 5.0f, 10.0f, 5.0f};
    text.margins.left = {25.0f, UILengthUnit::Percent};
    text.margins.right = {25.0f, UILengthUnit::Percent};
    text.margins.top = {-50.0f, UILengthUnit::Percent};

    const glm::vec2 fitted = calculate_ui_text_fitted_size(text, {160.0f, 48.0f});
    assert(std::abs(fitted.x - 40.0f) <= 0.001f);
    assert(std::abs(fitted.y - (20.0f / 3.0f)) <= 0.001f);
    assert(resolve_ui_length({-50.0f, UILengthUnit::Percent}, 40.0f) == -20.0f);
    assert(resolve_ui_length({12.0f, UILengthUnit::Pixels}, 40.0f) == 12.0f);
}

} // namespace

int main() {
    test_canvas_rendering_uses_sorting_and_runtime_button_state();
    test_editor_preview_draws_a_camera_relative_canvas_frame();
    test_centered_text_uses_the_font_baseline_inside_its_rect();
    test_text_margins_inset_the_content_rect_in_canvas_pixels();
    test_text_fit_content_solves_percentage_insets();
    std::cout << "Canvas UI render tests passed\n";
    return 0;
}
