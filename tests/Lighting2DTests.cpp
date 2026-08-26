#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/PhysicsComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "editor/commands/EditorCommandHistory.hpp"
#include "editor/commands/EntityCommands.hpp"
#include "renderer/Lighting2DSystem.hpp"
#include "renderer/ShadowSilhouette.hpp"
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

void test_sprite_material_keeps_four_strongest_lights() {
    SpriteMaterialLighting lighting;
    for (const float intensity : {0.2f, 0.8f, 0.4f, 1.0f, 0.6f, 0.1f}) {
        SpriteMaterialLight light;
        light.intensity = intensity;
        lighting.add_light(light);
    }
    assert(lighting.light_count == SpriteMaterialLighting::MaxLights);
    assert(approximate(lighting.lights[0].intensity, 1.0f));
    assert(approximate(lighting.lights[1].intensity, 0.8f));
    assert(approximate(lighting.lights[2].intensity, 0.6f));
    assert(approximate(lighting.lights[3].intensity, 0.4f));
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

void test_shadow_modes_have_distinct_sampling_and_penumbra() {
    Light2DComponent light;
    light.height = 64.0f;
    light.source_radius = 2.0f;
    light.shadow_softness = 0.5f;
    assert(shadow_sample_count(ShadowMode::Hard) == 1);
    assert(shadow_sample_count(ShadowMode::SoftFast) == 5);
    assert(shadow_sample_count(ShadowMode::ContactHardening) == 9);
    light.shadow_mode = ShadowMode::SoftFast;
    const float soft_near = shadow_penumbra_radius(light, 16.0f, 32.0f);
    const float soft_far = shadow_penumbra_radius(light, 128.0f, 32.0f);
    assert(approximate(soft_near, soft_far));
    light.shadow_mode = ShadowMode::ContactHardening;
    const float contact_near = shadow_penumbra_radius(light, 16.0f, 32.0f);
    const float contact_far = shadow_penumbra_radius(light, 128.0f, 32.0f);
    assert(contact_far > contact_near);
}

void test_alpha_silhouette_extracts_concavity_and_caches() {
    SDL_Surface* surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    std::vector<std::uint32_t> pixels(64, 0u);
    for (int y = 1; y < 7; ++y) {
        for (int x = 1; x < 3; ++x) pixels[y * 8 + x] = 0xffffffffu;
    }
    for (int y = 5; y < 7; ++y) {
        for (int x = 1; x < 7; ++x) pixels[y * 8 + x] = 0xffffffffu;
    }
    Texture texture(sdl_renderer, 8, 8, pixels.data());
    const SDL_FRect frame{0, 0, 8, 8};
    const auto detailed = extract_alpha_silhouette(texture, frame, 0.5f, 0.0f);
    const auto simplified = extract_alpha_silhouette(texture, frame, 0.5f, 1.0f);
    assert(detailed == extract_alpha_silhouette(texture, frame, 0.5f, 0.0f));
    assert(detailed.size() > 4);
    assert(simplified.size() >= 3 && simplified.size() < detailed.size());
    assert(std::any_of(detailed.begin(), detailed.end(), [](glm::vec2 point) {
        return point.x > 0.3f && point.x < 0.5f && point.y > 0.3f && point.y < 0.7f;
    }));
    ShadowSilhouetteCache cache;
    cache.begin_frame();
    const auto& first = cache.get(texture, frame, 0.5f, 1.0f);
    const auto& second = cache.get(texture, frame, 0.5f, 1.0f);
    assert(!first.empty() && &first == &second && cache.size() == 1);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
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

    const EntityID baked_light_entity = entities.create_entity();
    entities.add_component<TransformComponent>(baked_light_entity, TransformComponent{});
    Light2DComponent baked_light;
    baked_light.mobility = Light2DMobility::Baked;
    entities.add_component<Light2DComponent>(baked_light_entity, baked_light);

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
    authored.height = 12.0f;
    authored.opacity = 0.4f;
    entities.add_component<ShadowCaster2DComponent>(static_caster, authored);

    const EntityID baked_caster = entities.create_entity();
    entities.add_component<TransformComponent>(baked_caster, TransformComponent{});
    ShadowCaster2DComponent baked;
    baked.mobility = ShadowCasterMobility::Baked;
    entities.add_component<ShadowCaster2DComponent>(baked_caster, baked);

    Renderer2D renderer(nullptr);
    Lighting2DSystem system;
    system.configure(entities, renderer);
    assert(system.runtime_pass()->lights().size() == 1);
    assert(system.runtime_pass()->lights().front().world_position == glm::vec2(12.0f, 8.0f));
    assert(system.runtime_pass()->casters().size() == 2);
    assert(approximate(system.runtime_pass()->casters()[1].height, 12.0f));
    assert(approximate(system.runtime_pass()->casters()[1].opacity, 0.4f));
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
    light.range = 350.0f;
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

    entities.get_component<Light2DComponent>(target).range = 0.0f;
    entities.get_component<ShadowCaster2DComponent>(target).vertices.resize(2);
    const auto issues = registry.validate_entity(entities, target);
    assert(issues.size() == 2);

    EditorCommandHistory history;
    Light2DComponent changed = light;
    changed.intensity = 0.25f;
    changed.range = 512.0f;
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
        width = context.width;
        height = context.height;
        raster_scale = context.raster_scale;
        previous_layers_history.push_back(context.previously_composited_layers);
        return true;
    }

    RenderLayerMask layers{0};
    int calls{0};
    RenderLayer layer{0};
    const Camera2D* camera{nullptr};
    int width{0};
    int height{0};
    glm::vec2 raster_scale{1.0f};
    std::vector<RenderLayerMask> previous_layers_history;
};

class ShadowReceiverRecordingPass final : public ILightingPass {
public:
    [[nodiscard]] const char* name() const override { return "Shadow Receiver Recording"; }
    [[nodiscard]] bool is_enabled() const override { return true; }
    [[nodiscard]] RenderLayerMask affected_layers() const override { return render_layer_mask(0); }
    [[nodiscard]] bool requires_shadow_receiver_mask() const override { return true; }
    bool apply(const LightingContext& context) override {
        assert(context.shadow_receiver_mask);
        SDL_Texture* previous = SDL_GetRenderTarget(context.renderer);
        assert(SDL_SetRenderTarget(context.renderer, context.shadow_receiver_mask));
        const glm::vec2 receiver_position = context.camera->world_to_screen({-8.0f, 0.0f, 0.0f});
        const glm::vec2 rejected_position = context.camera->world_to_screen({8.0f, 0.0f, 0.0f});
        const auto read_alpha = [&](const glm::vec2& position) {
            const SDL_Rect rect{static_cast<int>(position.x), static_cast<int>(position.y), 1, 1};
            SDL_Surface* sample = SDL_RenderReadPixels(context.renderer, &rect);
            assert(sample);
            std::uint8_t red = 0, green = 0, blue = 0, alpha = 0;
            assert(SDL_ReadSurfacePixel(sample, 0, 0, &red, &green, &blue, &alpha));
            SDL_DestroySurface(sample);
            return alpha;
        };
        receiver_alpha = read_alpha(receiver_position);
        rejected_alpha = read_alpha(rejected_position);
        SDL_SetRenderTarget(context.renderer, previous);
        return true;
    }

    std::uint8_t receiver_alpha{0};
    std::uint8_t rejected_alpha{255};
};

void test_renderer_routes_lighting_only_to_affected_layers() {
    SDL_Surface* surface = SDL_CreateSurface(48, 48, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    {
        Renderer2D renderer(sdl_renderer);
        renderer.set_render_resolution(0.5f);
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
        assert(pass->width == 24 && pass->height == 24);
        assert(pass->raster_scale == glm::vec2(0.5f));
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

void test_renderer_builds_explicit_shadow_receiver_mask() {
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
        auto pass = std::make_shared<ShadowReceiverRecordingPass>();
        renderer.add_lighting_pass(pass);

        renderer.begin_scene(camera);
        renderer.submit_sprite(
            white.get(), {-8.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 0, {}, 0, SDL_BLENDMODE_BLEND,
            false, nullptr, true
        );
        renderer.submit_sprite(
            white.get(), {8.0f, 0.0f, 0.0f}, {8.0f, 8.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 0, {}, 0, SDL_BLENDMODE_BLEND,
            false, nullptr, false
        );
        renderer.end_scene();
        assert(pass->receiver_alpha > 240);
        assert(pass->rejected_alpha < 5);
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
}

std::pair<std::uint8_t, std::uint8_t> render_realtime_shadow_probe(
    bool receives_shadows,
    ShadowMode mode,
    float caster_opacity = 1.0f,
    float caster_height = 32.0f
) {
    SDL_Surface* surface = SDL_CreateSurface(96, 64, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer* sdl_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(sdl_renderer);
    std::pair<std::uint8_t, std::uint8_t> result{};
    {
        Renderer2D renderer(sdl_renderer);
        Camera2D camera(96.0f, 64.0f);
        std::unique_ptr<Texture> gray(Texture::create_solid(
            sdl_renderer, 4, 4, 32, 32, 32, 255
        ));
        Light2DComponent light;
        light.type = Light2DType::Point;
        light.color = {255, 0, 0, 255};
        light.range = 80.0f;
        light.intensity = 1.0f;
        light.shadow_mode = mode;
        light.shadow_strength = 1.0f;
        light.shadow_max_distance = 64.0f;
        light.height = 64.0f;
        auto pass = std::make_shared<Lighting2DPass>();
        pass->update(
            {{{-20.0f, 0.0f}, light, 1, 0.0f}},
            {ShadowPolygon2D{
                {{-2.0f, -5.0f}, {2.0f, -5.0f}, {2.0f, 5.0f}, {-2.0f, 5.0f}},
                render_layer_mask(0), caster_height, caster_opacity, 2
            }}
        );
        renderer.add_lighting_pass(pass);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        renderer.begin_scene(camera);
        renderer.submit_sprite(
            gray.get(), {0.0f, 0.0f, 0.0f}, {88.0f, 48.0f}, 0.0f,
            {255, 255, 255, 255}, nullptr, {0.5f, 0.5f}, false,
            false, false, 0, 0, nullptr, 0, {}, 0, SDL_BLENDMODE_BLEND,
            false, nullptr, receives_shadows
        );
        renderer.end_scene();
        assert(renderer.present());
        std::uint8_t green = 0, blue = 0, alpha = 0;
        assert(SDL_ReadSurfacePixel(surface, 58, 32, &result.first, &green, &blue, &alpha));
        assert(SDL_ReadSurfacePixel(surface, 35, 20, &result.second, &green, &blue, &alpha));
        assert(renderer.get_render_target_pool_metrics().leased_targets == 0);
    }
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroySurface(surface);
    return result;
}

void test_realtime_shadow_composition_respects_receivers() {
    const auto hard_receiver = render_realtime_shadow_probe(true, ShadowMode::Hard);
    const auto hard_rejected = render_realtime_shadow_probe(false, ShadowMode::Hard);
    const auto soft_receiver = render_realtime_shadow_probe(true, ShadowMode::SoftFast);
    const auto contact_receiver = render_realtime_shadow_probe(true, ShadowMode::ContactHardening);
    const auto partial_receiver = render_realtime_shadow_probe(true, ShadowMode::Hard, 0.25f);
    const auto low_receiver = render_realtime_shadow_probe(true, ShadowMode::Hard, 1.0f, 4.0f);
    const auto unshadowed = render_realtime_shadow_probe(true, ShadowMode::None);
    assert(hard_receiver.first + 20 < unshadowed.first);
    assert(hard_rejected.first + 5 >= unshadowed.first);
    assert(soft_receiver.first < unshadowed.first);
    assert(contact_receiver.first < unshadowed.first);
    assert(partial_receiver.first > hard_receiver.first && partial_receiver.first < unshadowed.first);
    assert(low_receiver.first > hard_receiver.first);
}

} // namespace

int main() {
    test_point_light_bounds_and_falloff();
    test_sprite_material_keeps_four_strongest_lights();
    test_color_intensity_and_blend_modes();
    test_shadow_geometry_extrudes_only_light_facing_edges();
    test_shadow_modes_have_distinct_sampling_and_penumbra();
    test_alpha_silhouette_extracts_concavity_and_caches();
    test_system_extracts_lights_and_shadow_sources();
    test_components_serialize_validate_and_restore_commands();
    test_renderer_routes_lighting_only_to_affected_layers();
    test_renderer_composites_uncovered_lighting_once_per_frame();
    test_renderer_builds_explicit_shadow_receiver_mask();
    test_realtime_shadow_composition_respects_receivers();
    std::cout << "Lighting2DTests passed\n";
    return 0;
}
