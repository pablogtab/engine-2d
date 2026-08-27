#include "SceneRuntime.hpp"
#include "../core/Input.hpp"
#include "../core/Log.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../renderer/SpriteAnimationSystem.hpp"
#include "../debug/FrameProfiler.hpp"
#include <algorithm>

namespace Engine {

SceneRuntime::SceneRuntime() : m_game_camera(1920.0f, 1080.0f) {
    m_camera_controller.bind_camera(&m_game_camera);
    m_camera_controller.set_follow_speed(6.0f);
    m_camera_controller.enable_deadzone(false);
    m_camera_controller.enable_bounds(false);
}

void SceneRuntime::attach(Scene& scene, SDL_Renderer* renderer) {
    PhysicsSystem::clear();
    PhysicsSystem::set_gravity(scene.settings().gravity);
    TransformSystem::update(scene.get_entity_manager());
    m_ui_input.reset();
    m_particle_world.clear();
    m_world_texts.clear();
    m_particle_world.set_renderer(renderer);
    auto& em = scene.get_entity_manager();
    em.for_each<ParticleEmitter2DComponent>([&](EntityID entity, ParticleEmitter2DComponent& emitter) {
        if (emitter.play_on_awake) {
            static_cast<void>(m_particle_world.play_entity(entity, scene, AssetDatabase::get()));
        }
    });
    static_cast<void>(m_camera_controller.update_from_entities(
        scene.get_entity_manager(), 0.0f, true
    ));
    ENGINE_LOG_INFO("[SCENE RUNTIME] Attached scene '{}'", scene.get_name());
}

void SceneRuntime::detach() {
    PhysicsSystem::clear();
    m_ui_input.reset();
    m_particle_world.clear();
    m_world_texts.clear();
    m_camera_controller.reset_entity_tracking();
}

void SceneRuntime::configure_game_view(const RenderViewConfiguration& configuration) {
    m_ui_output_size = {
        static_cast<float>(configuration.output_width),
        static_cast<float>(configuration.output_height)
    };
    m_ui_safe_area = configuration.safe_area;
    m_game_camera.set_viewport_size(
        static_cast<float>(configuration.output_width),
        static_cast<float>(configuration.output_height)
    );
    m_game_camera.set_reference_size(
        static_cast<float>(configuration.reference_width),
        static_cast<float>(configuration.reference_height)
    );
}

void SceneRuntime::update(Scene& scene, float delta_time, FrameProfiler* profiler) {
    FrameProfiler::Scope runtime_scope(profiler, "runtime.systems");
    const bool ui_had_focus = m_ui_input.focused_entity() != 0;
    // Gameplay owns arrow keys until a UI control explicitly gains focus.
    const bool navigate_previous = ui_had_focus && Input::is_key_just_pressed(SDL_SCANCODE_UP);
    const bool navigate_next = ui_had_focus && Input::is_key_just_pressed(SDL_SCANCODE_DOWN);
    const bool activate_pressed = ui_had_focus &&
        (Input::is_key_just_pressed(SDL_SCANCODE_RETURN) || Input::is_key_just_pressed(SDL_SCANCODE_SPACE));
    const bool activate_released = ui_had_focus &&
        (Input::is_key_just_released(SDL_SCANCODE_RETURN) || Input::is_key_just_released(SDL_SCANCODE_SPACE));
    {
    FrameProfiler::Scope ui_scope(profiler, "runtime.ui");
    static_cast<void>(m_ui_input.update(
        scene.get_entity_manager(),
        m_ui_output_size,
        m_ui_safe_area,
        {
            .pointer_position = Input::get_mouse_position(),
            .pointer_down = Input::is_mouse_pressed(MouseButton::LEFT),
            .pointer_pressed = Input::is_mouse_just_pressed(MouseButton::LEFT),
            .pointer_released = Input::is_mouse_just_released(MouseButton::LEFT),
            .focus_next = navigate_next,
            .focus_previous = navigate_previous,
            .activate_down = ui_had_focus &&
                (Input::is_key_pressed(SDL_SCANCODE_RETURN) || Input::is_key_pressed(SDL_SCANCODE_SPACE)),
            .activate_pressed = activate_pressed,
            .activate_released = activate_released
        },
        &scene.get_event_dispatcher()
    ));
    }
    if (scene.settings().physics_enabled) {
        FrameProfiler::Scope physics_scope(profiler, "physics");
        PhysicsSystem::update(scene.get_entity_manager(), scene.get_event_dispatcher(), delta_time);
    }
    {
        FrameProfiler::Scope animation_scope(profiler, "animation");
        SpriteAnimationSystem::update(scene.get_entity_manager(), delta_time);
    }
    {
        FrameProfiler::Scope particles_scope(profiler, "particles");
        m_particle_world.update(scene, delta_time, AssetDatabase::get());
    }
    for (RuntimeWorldText& text : m_world_texts) {
        text.position += text.velocity * delta_time;
        text.remaining -= delta_time;
        const float fade_window = std::min(0.25f, text.duration);
        const float fade = fade_window > 0.0f
            ? std::clamp(text.remaining / fade_window, 0.0f, 1.0f)
            : 0.0f;
        text.color.a = static_cast<std::uint8_t>(static_cast<float>(text.base_alpha) * fade);
    }
    std::erase_if(m_world_texts, [](const RuntimeWorldText& text) {
        return text.remaining <= 0.0f;
    });
    {
        FrameProfiler::Scope transforms_scope(profiler, "transforms");
        TransformSystem::update(scene.get_entity_manager());
    }
    {
        FrameProfiler::Scope camera_scope(profiler, "camera");
        static_cast<void>(m_camera_controller.update_from_entities(
            scene.get_entity_manager(), delta_time
        ));
    }
}

bool SceneRuntime::show_world_text(RuntimeWorldText text) {
    if (text.text.empty() || text.duration <= 0.0f || text.scale <= 0.0f) return false;
    constexpr std::size_t max_world_texts = 256;
    if (m_world_texts.size() >= max_world_texts) m_world_texts.erase(m_world_texts.begin());
    text.remaining = text.duration;
    m_world_texts.push_back(std::move(text));
    return true;
}

} // namespace Engine
