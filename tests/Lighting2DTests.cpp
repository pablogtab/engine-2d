#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "renderer/Lighting2DSystem.hpp"
#include "renderer/Renderer2D.hpp"
#include "renderer/Texture.hpp"
#include "scene/Camera2D.hpp"
#include "scene/ComponentRegistry.hpp"
#include <SDL3/SDL.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

namespace {

using namespace Engine;

bool approximate(float lhs, float rhs, float epsilon = 0.001f) {
    return std::abs(lhs - rhs) <= epsilon;
}

void test_point_light_bounds_and_falloff() {
    const glm::vec4 bounds = point_light_bounds({10.0f, -4.0f}, 25.0f);
    assert(bounds == glm::vec4(-15.0f, -29.0f, 35.0f, 21.0f));
    assert(point_light_bounds({3.0f, 4.0f}, -1.0f) == glm::vec4(3.0f, 4.0f, 3.0f, 4.0f));
    assert(approximate(point_light_falloff(0.0f, 100.0f, 0.75f), 0.75f));
    assert(point_light_falloff(100.0f, 100.0f, 1.0f) == 0.0f);
    assert(point_light_falloff(10.0f, 0.0f, 1.0f) == 0.0f);
    assert(point_light_falloff(25.0f, 100.0f, 1.0f) >
           point_light_falloff(75.0f, 100.0f, 1.0f));
    assert(point_light_falloff(50.0f, 100.0f, 1.0f, 0.5f) >
           point_light_falloff(50.0f, 100.0f, 1.0f, 2.0f));
}

void test_color_intensity_and_blend_modes() {
    const glm::vec4 base{0.25f, 0.5f, 0.75f, 0.8f};
    const glm::vec4 light{1.0f, 0.25f, 0.0f, 1.0f};
    const glm::vec4 additive = blend_light_sample(base, light, 0.5f, Light2DBlendMode::Additive);
    const glm::vec4 multiply = blend_light_sample(base, light, 0.5f, Light2DBlendMode::Multiply);
    const glm::vec4 screen = blend_light_sample(base, light, 0.5f, Light2DBlendMode::Screen);
    assert(approximate(additive.r, 0.75f));
    assert(approximate(additive.g, 0.625f));
    assert(approximate(multiply.r, 0.25f));
    assert(approximate(multiply.g, 0.3125f));
    assert(approximate(screen.r, 0.625f));
    assert(approximate(screen.g, 0.5625f));
    assert(additive.a == base.a && multiply.a == base.a && screen.a == base.a);
}

void test_shadow_geometry_extrudes_only_light_facing_edges() {
    const std::vector<glm::vec2> polygon{
        {-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}
    };
    const auto triangles = build_shadow_triangles({-5.0f, 0.0f}, polygon, 10.0f);
    assert(triangles.size() == 12);
    assert(triangles[0] == polygon[0]);
    bool has_extruded_vertex = false;
    for (const glm::vec2& point : triangles) {
        if (point.x > 1.0f) has_extruded_vertex = true;
    }
    assert(has_extruded_vertex);

    std::vector<glm::vec2> reversed(polygon.rbegin(), polygon.rend());
    const auto reversed_triangles = build_shadow_triangles({-5.0f, 0.0f}, reversed, 10.0f);
    assert(reversed_triangles.size() == triangles.size());
    assert(build_shadow_triangles(
        {0.0f, 0.0f}, std::vector<glm::vec2>{{0.0f, 0.0f}, {1.0f, 0.0f}}, 10.0f
    ).empty());
}

void test_system_extracts_lights_and_shadow_sources() {
    EntityManager entities;
    const EntityID light_entity = entities.create_entity();
    TransformComponent light_transform;
    light_transform.world_position = {12.0f, 8.0f, 0.0f};
    entities.add_component<TransformComponent>(light_entity, light_transform);
    Light2DComponent light;
    light.affected_layers = render_layer_mask(3);
    entities.add_component<Light2DComponent>(light_entity, light);

    const EntityID collider_caster = entities.create_entity();
    entities.add_component<TransformComponent>(collider_caster, TransformComponent{});
    entities.add_component<ColliderComponent>(collider_caster, ColliderComponent::Box({20.0f, 10.0f}));
    entities.add_component<ShadowCaster2DComponent>(collider_caster, ShadowCaster2DComponent{});

    const EntityID static_caster = entities.create_entity();
    entities.add_component<TransformComponent>(static_caster, TransformComponent{});
    ShadowCaster2DComponent authored;
    authored.source = ShadowGeometrySource::StaticPolygon;
    authored.vertices = {{0.0f, 0.0f}, {8.0f, 0.0f}, {0.0f, 8.0f}};
    authored.affected_layers = render_layer_mask(3);
    entities.add_component<ShadowCaster2DComponent>(static_caster, authored);

    Renderer2D renderer(nullptr);
    Lighting2DSystem system;
    system.configure(entities, renderer);
    assert(system.runtime_pass()->lights().size() == 1);
    assert(system.runtime_pass()->lights().front().world_position == glm::vec2(12.0f, 8.0f));
    assert(system.runtime_pass()->casters().size() == 2);
    assert(system.runtime_pass()->affected_layers() == render_layer_mask(3));
    assert(renderer.get_active_lighting_pass_count(3) == 1);
    assert(renderer.get_active_lighting_pass_count(2) == 0);
}

void test_components_serialize_validate_and_restore_commands() {
    EntityManager entities;
    const EntityID source = entities.create_entity();
    entities.add_component<TransformComponent>(source, TransformComponent{});

    Light2DComponent light;
    light.color = {20, 40, 80, 200};
    light.intensity = 0.7f;
    light.radius = 350.0f;
    light.falloff = 0.65f;
    light.blend_mode = Light2DBlendMode::Screen;
    light.affected_layers = render_layer_mask(1) | render_layer_mask(4);
    entities.add_component<Light2DComponent>(source, light);

    ShadowCaster2DComponent caster;
    caster.source = ShadowGeometrySource::StaticPolygon;
    caster.vertices = {{-4.0f, -2.0f}, {6.0f, -2.0f}, {0.0f, 7.0f}};
    caster.affected_layers = render_layer_mask(4);
    entities.add_component<ShadowCaster2DComponent>(source, caster);

    nlohmann::json serialized;
    std::string error;
    const ComponentRegistry& registry = default_component_registry();
    assert(registry.serialize_entity(entities, source, serialized, {}, error));
    assert(serialized.contains("light_2d"));
    assert(serialized.contains("shadow_caster_2d"));

    const EntityID target = entities.create_entity();
    assert(registry.deserialize_entity(entities, target, serialized, {}, error));
    assert(entities.get_component<Light2DComponent>(target) == light);
    assert(entities.get_component<ShadowCaster2DComponent>(target) == caster);

    entities.get_component<Light2DComponent>(target).radius = 0.0f;
    entities.get_component<ShadowCaster2DComponent>(target).vertices.resize(2);
    const auto issues = registry.validate_entity(entities, target);
    assert(issues.size() == 2);

    EditorCommandHistory history;
    Light2DComponent changed = light;
    changed.intensity = 0.25f;
    changed.radius = 512.0f;
    history.execute_command(std::make_unique<SetComponentCommand<Light2DComponent>>(
        &entities, source, light, changed, "Edit Light 2D"
    ));
    assert(entities.get_component<Light2DComponent>(source) == changed);
    history.undo();
    assert(entities.get_component<Light2DComponent>(source) == light);
    history.redo();
    assert(entities.get_component<Light2DComponent>(source) == changed);
}

class LayerRecordingPass final : public ILightingPass {
public:
    explicit LayerRecordingPass(RenderLayerMask layers = render_layer_mask(2)) : layers(layers) {}
    [[nodiscard]] const char* name() const override { return "Layer Recording"; }
    [[nodiscard]] bool is_enabled() const override { return true; }
    [[nodiscard]] RenderLayerMask affected_layers() const override { return layers; }
    bool apply(const LightingContext& context) override {
        ++calls;
        layer = context.render_layer;
        camera = context.camera;
        previous_layers_history.push_back(context.previously_composited_layers);
        return true;
    }

    RenderLayerMask layers{0};
    int calls{0};
    RenderLayer layer{0};
    const Camera2D* camera{nullptr};
    std::vector<RenderLayerMask> previous_layers_history;
};

void test_renderer_routes_lighting_only_to_affected_layers() {
    SDL_Surface* surface = SDL_CreateSurface(48, 48, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        Renderer2D renderer(sdl_renderer);
        Camera2D camera(48.0f, 48.0f);
        std::unique_ptr<Texture> white(Texture::create_solid(sdl_renderer, 4, 4, 255, 255, 255, 255));
        auto pass = std::make_shared<LayerRecordingPass>();
        renderer.add_lighting_pass(pass);

        renderer.begin_scene(camera);
        renderer.submit_sprite(white.get(), {-8.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
                               {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
                               false, false, 0, 0, nullptr, 1);
        renderer.submit_sprite(white.get(), {8.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
                               {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
                               false, false, 0, 0, nullptr, 2);
        renderer.end_scene();

        assert(pass->calls == 1);
        assert(pass->layer == 2);
        assert(pass->camera == &camera);
        assert(renderer.get_frame_metrics().stage(RenderStage::Lighting).draw_calls == 1);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);

        camera.set_render_layer_mask(render_layer_mask(1));
        renderer.begin_scene(camera);
        renderer.submit_sprite(white.get(), {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
                               {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
                               false, false, 0, 0, nullptr, 2);
        renderer.end_scene();
        assert(pass->calls == 1);
        assert(renderer.get_culled_sprite_count() == 1);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

void test_renderer_composites_uncovered_lighting_once_per_frame() {
    SDL_Surface* surface = SDL_CreateSurface(48, 48, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        Renderer2D renderer(sdl_renderer);
        Camera2D camera(48.0f, 48.0f);
        std::unique_ptr<Texture> white(Texture::create_solid(
            sdl_renderer, 4, 4, 255, 255, 255, 255
        ));
        auto pass = std::make_shared<LayerRecordingPass>(
            render_layer_mask(1) | render_layer_mask(2)
        );
        renderer.add_lighting_pass(pass);

        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {-8.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 1
        );
        renderer.submit_sprite(
            white.get(), {8.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 2
        );
        renderer.end_scene();
        assert((pass->previous_layers_history == std::vector<RenderLayerMask>{
            0, render_layer_mask(1)
        }));

        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {0.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 2
        );
        renderer.end_scene();
        assert(pass->previous_layers_history.size() == 3);
        assert(pass->previous_layers_history.back() == 0);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

} // namespace

int main() {
    test_point_light_bounds_and_falloff();
    test_color_intensity_and_blend_modes();
    test_shadow_geometry_extrudes_only_light_facing_edges();
    test_system_extracts_lights_and_shadow_sources();
    test_components_serialize_validate_and_restore_commands();
    test_renderer_routes_lighting_only_to_affected_layers();
    test_renderer_composites_uncovered_lighting_once_per_frame();
    std::cout << "Lighting2DTests passed\n";
    return 0;
}
