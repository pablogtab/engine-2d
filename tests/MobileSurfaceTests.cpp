#include "core/ApplicationLifecycle.hpp"
#include "core/DisplayManager.hpp"
#include "core/Time.hpp"
#include "scene/SceneManager.hpp"

#include <SDL3/SDL_events.h>
#include <cassert>
#include <memory>

namespace {

class ResourceScene final : public Engine::Scene {
public:
    explicit ResourceScene(const char* name) : Scene(name) {}
    void on_render_resources_invalidated() override { ++invalidations; }
    int invalidations{0};
};

class MobileDisplayBackend final : public Engine::IDisplayBackend {
public:
    Engine::DisplayState state{
        {1, Engine::WindowMode::Windowed, 1280, 720, 0.0f, true},
        1280, 720, 1280, 720, 1.0f, Engine::DisplayOrientation::Landscape
    };

    Engine::DisplayCapabilities capabilities() const override { return {}; }
    std::vector<Engine::DisplayInfo> available_displays() const override {
        return {{
            1,
            "Mobile Display",
            {0, 0, state.pixel_width, state.pixel_height},
            {0, 0, state.pixel_width, state.pixel_height},
            state.dpi_scale,
            state.orientation,
            {},
            true
        }};
    }
    std::vector<Engine::DisplayMode> available_modes(Engine::DisplayId) const override {
        return {};
    }
    Engine::DisplayState query_state() const override { return state; }
    bool apply(const Engine::DisplaySettings&, std::string& error) override {
        error = "mobile display state is owned by the operating system";
        return false;
    }
};

void test_ordered_suspend_resume() {
    Engine::ApplicationLifecycle lifecycle;
    auto actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::WillEnterBackground);
    assert(actions.state_changed && actions.suspend_runtime && actions.invalidate_render_surface);
    assert(!lifecycle.should_run_frames());
    assert(lifecycle.state() == Engine::ApplicationLifecycleState::Suspending);

    actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::DidEnterBackground);
    assert(actions.state_changed && !actions.suspend_runtime);
    assert(lifecycle.state() == Engine::ApplicationLifecycleState::Suspended);

    actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::WillEnterForeground);
    assert(actions.state_changed && actions.restore_render_surface);
    assert(lifecycle.state() == Engine::ApplicationLifecycleState::Resuming);

    actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::DidEnterForeground);
    assert(actions.state_changed && actions.resume_runtime && actions.restore_render_surface);
    assert(actions.refresh_display && actions.reset_frame_clock);
    assert(lifecycle.should_run_frames());
}

void test_duplicate_and_out_of_order_events_are_safe() {
    Engine::ApplicationLifecycle lifecycle;
    auto actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::DidEnterBackground);
    assert(actions.state_changed && actions.suspend_runtime && actions.invalidate_render_surface);
    actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::DidEnterBackground);
    assert(!actions.state_changed && !actions.suspend_runtime);

    actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::DidEnterForeground);
    assert(actions.state_changed && actions.resume_runtime && actions.restore_render_surface);
    actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::DidEnterForeground);
    assert(!actions.state_changed && !actions.resume_runtime);
}

void test_low_memory_and_termination_actions() {
    Engine::ApplicationLifecycle lifecycle;
    auto actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::LowMemory);
    assert(actions.trim_memory && !actions.state_changed);
    actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::Terminating);
    assert(actions.state_changed && actions.suspend_runtime);
    assert(actions.invalidate_render_surface && actions.request_termination);
    assert(lifecycle.state() == Engine::ApplicationLifecycleState::Terminating);
    actions = lifecycle.handle(Engine::ApplicationLifecycleEvent::DidEnterForeground);
    assert(!actions.state_changed && !actions.resume_runtime);
}

void test_sdl_event_mapping() {
    using Engine::ApplicationLifecycleEvent;
    assert(Engine::application_lifecycle_event_from_sdl(SDL_EVENT_WILL_ENTER_BACKGROUND) ==
        ApplicationLifecycleEvent::WillEnterBackground);
    assert(Engine::application_lifecycle_event_from_sdl(SDL_EVENT_DID_ENTER_BACKGROUND) ==
        ApplicationLifecycleEvent::DidEnterBackground);
    assert(Engine::application_lifecycle_event_from_sdl(SDL_EVENT_WILL_ENTER_FOREGROUND) ==
        ApplicationLifecycleEvent::WillEnterForeground);
    assert(Engine::application_lifecycle_event_from_sdl(SDL_EVENT_DID_ENTER_FOREGROUND) ==
        ApplicationLifecycleEvent::DidEnterForeground);
    assert(Engine::application_lifecycle_event_from_sdl(SDL_EVENT_LOW_MEMORY) ==
        ApplicationLifecycleEvent::LowMemory);
    assert(Engine::application_lifecycle_event_from_sdl(SDL_EVENT_TERMINATING) ==
        ApplicationLifecycleEvent::Terminating);
    assert(!Engine::application_lifecycle_event_from_sdl(SDL_EVENT_KEY_DOWN));
}

void test_time_does_not_accumulate_suspension_gap() {
    Engine::Time::reset();
    Engine::Time::set_time_scale(0.5f);
    Engine::Time::update(0.02f);
    const float game_time = Engine::Time::get_time();
    const float real_time = Engine::Time::get_real_time();
    const std::uint64_t frame_count = Engine::Time::get_frame_count();
    Engine::Time::suspend();
    Engine::Time::update(10.0f);
    assert(Engine::Time::get_delta_time() == 0.0f);
    assert(Engine::Time::get_real_delta_time() == 0.0f);
    assert(Engine::Time::get_time() == game_time);
    assert(Engine::Time::get_real_time() == real_time);
    assert(Engine::Time::get_frame_count() == frame_count);
    Engine::Time::resume();
    assert(Engine::Time::get_time_scale() == 0.5f);
    Engine::Time::update(0.02f);
    assert(Engine::Time::get_delta_time() == 0.01f);
    Engine::Time::reset();
}

void test_scene_manager_invalidates_registered_and_transient_scenes() {
    Engine::SceneManager scenes;
    auto first = std::make_shared<ResourceScene>("First");
    auto second = std::make_shared<ResourceScene>("Second");
    auto transient = std::make_shared<ResourceScene>("Transient");
    scenes.add_scene(first);
    scenes.add_scene(second);
    scenes.enter_transient_scene(transient);
    scenes.invalidate_render_resources();
    assert(first->invalidations == 1);
    assert(second->invalidations == 1);
    assert(transient->invalidations == 1);
    scenes.exit_transient_scene();
}

void test_mobile_surface_changes_propagate_from_fake_backend() {
    auto backend = std::make_unique<MobileDisplayBackend>();
    MobileDisplayBackend* fake = backend.get();
    Engine::DisplayManager display(std::move(backend));
    int notifications = 0;
    Engine::DisplayState notified_state;
    display.set_change_callback([&](const Engine::DisplayState& state) {
        ++notifications;
        notified_state = state;
    });

    fake->state.logical_width = 720;
    fake->state.logical_height = 1280;
    fake->state.pixel_width = 720;
    fake->state.pixel_height = 1280;
    fake->state.orientation = Engine::DisplayOrientation::Portrait;
    fake->state.safe_area = {0.0f, 44.0f, 0.0f, 34.0f};
    display.refresh_from_system();
    assert(notifications == 1);
    assert(notified_state.orientation == Engine::DisplayOrientation::Portrait);
    assert(notified_state.safe_area == fake->state.safe_area);

    fake->state.logical_width = 540;
    fake->state.pixel_width = 540;
    fake->state.safe_area = {0.0f, 24.0f, 0.0f, 20.0f};
    display.refresh_from_system();
    assert(notifications == 2);
    assert(notified_state.pixel_width == 540);
    assert(notified_state.pixel_height == 1280);
    assert(notified_state.safe_area == fake->state.safe_area);

    Engine::DisplaySettings requested = notified_state.settings;
    requested.window_width = 800;
    const Engine::DisplayApplyResult result = display.apply(requested);
    assert(!result.success);
    assert(result.error == Engine::DisplayApplyError::Unsupported);
}

} // namespace

int main() {
    test_ordered_suspend_resume();
    test_duplicate_and_out_of_order_events_are_safe();
    test_low_memory_and_termination_actions();
    test_sdl_event_mapping();
    test_time_does_not_accumulate_suspension_gap();
    test_scene_manager_invalidates_registered_and_transient_scenes();
    test_mobile_surface_changes_propagate_from_fake_backend();
    return 0;
}
