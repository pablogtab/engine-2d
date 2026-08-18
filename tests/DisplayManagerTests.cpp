#include "core/DisplayManager.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "DisplayManager test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

class FakeDisplayBackend final : public Engine::IDisplayBackend {
public:
    Engine::DisplayCapabilities caps{true, true, true, true, true};
    std::vector<Engine::DisplayInfo> displays{
        {1, "Primary", {0, 0, 1920, 1080}, {0, 0, 1920, 1040}, 1.0f, Engine::DisplayOrientation::Landscape, {}, true},
        {2, "Secondary", {1920, 0, 2560, 1440}, {1920, 0, 2560, 1400}, 2.0f, Engine::DisplayOrientation::Landscape, {}, false}
    };
    std::vector<Engine::DisplayMode> modes{
        {1920, 1080, 60.0f, 1.0f},
        {1280, 720, 60.0f, 1.0f},
        {1920, 1080, 120.0f, 1.0f},
        {1920, 1080, 60.0f, 1.0f},
        {0, 0, 0.0f, 1.0f}
    };
    Engine::DisplayState state{
        {1, Engine::WindowMode::Windowed, 1280, 720, 0.0f, true},
        1280, 720, 1280, 720, 1.0f, Engine::DisplayOrientation::Landscape
    };
    int failures_remaining{0};
    int apply_count{0};

    Engine::DisplayCapabilities capabilities() const override { return caps; }
    std::vector<Engine::DisplayInfo> available_displays() const override { return displays; }
    std::vector<Engine::DisplayMode> available_modes(Engine::DisplayId) const override { return modes; }
    Engine::DisplayState query_state() const override { return state; }

    bool apply(const Engine::DisplaySettings& settings, std::string& error) override {
        ++apply_count;
        if (failures_remaining > 0) {
            --failures_remaining;
            error = "simulated platform failure";
            return false;
        }
        state.settings = settings;
        if (settings.window_mode == Engine::WindowMode::BorderlessFullscreen) {
            const auto display = std::find_if(displays.begin(), displays.end(), [&settings](const Engine::DisplayInfo& info) {
                return info.id == settings.display_id;
            });
            state.logical_width = display != displays.end() ? display->bounds.width : settings.window_width;
            state.logical_height = display != displays.end() ? display->bounds.height : settings.window_height;
            state.settings.window_width = state.logical_width;
            state.settings.window_height = state.logical_height;
            state.settings.refresh_rate = 0.0f;
        } else {
            state.logical_width = settings.window_width;
            state.logical_height = settings.window_height;
        }
        state.pixel_width = state.logical_width * (settings.display_id == 2 ? 2 : 1);
        state.pixel_height = state.logical_height * (settings.display_id == 2 ? 2 : 1);
        state.dpi_scale = settings.display_id == 2 ? 2.0f : 1.0f;
        return true;
    }
};

} // namespace

int main() {
    auto backend = std::make_unique<FakeDisplayBackend>();
    FakeDisplayBackend* fake = backend.get();
    Engine::DisplayManager manager(std::move(backend));

    const auto modes = manager.available_modes(1);
    require(modes.size() == 3, "modes are filtered and deduplicated");
    require(modes[0].width == 1280, "modes are sorted by width");
    require(modes[1].refresh_rate == 120.0f, "higher refresh rate is sorted first");

    fake->displays.push_back({0, "Invalid", {}, {}, 0.0f, Engine::DisplayOrientation::Unknown, {}, false});
    fake->displays.push_back(fake->displays.front());
    const auto displays = manager.available_displays();
    require(displays.size() == 2, "invalid and duplicate displays are filtered");
    require(displays.front().is_primary, "primary display is ordered first");
    require(displays.front().modes.size() == 3, "display information exposes normalized modes");
    fake->displays.resize(2);

    int change_count = 0;
    manager.set_change_callback([&change_count](const Engine::DisplayState&) { ++change_count; });
    Engine::DisplaySettings windowed{2, Engine::WindowMode::Windowed, 900, 600, 0.0f, false};
    auto result = manager.apply(windowed);
    require(result.success, "windowed settings apply");
    require(result.state.pixel_width == 1800 && result.state.dpi_scale == 2.0f, "logical and physical sizes remain distinct");
    require(change_count == 1, "successful state change is notified once");

    const int apply_count_before_noop = fake->apply_count;
    result = manager.apply(result.state.settings);
    require(result.success, "applying the effective state is a successful no-op");
    require(fake->apply_count == apply_count_before_noop, "no-op does not touch the platform backend");
    require(change_count == 1, "no-op does not notify listeners");

    Engine::DisplaySettings borderless{2, Engine::WindowMode::BorderlessFullscreen, 1, 1, 144.0f, true};
    result = manager.apply(borderless);
    require(result.success, "borderless settings apply");
    require(result.state.logical_width == 2560 && result.state.logical_height == 1440, "borderless uses the desktop dimensions");
    require(result.state.settings.refresh_rate == 0.0f, "borderless ignores the requested refresh rate");

    const int changes_before_refresh = change_count;
    manager.refresh_from_system();
    require(change_count == changes_before_refresh, "unchanged system refresh does not notify listeners");

    Engine::DisplaySettings exclusive{2, Engine::WindowMode::ExclusiveFullscreen, 1920, 1080, 120.0f, true};
    result = manager.apply(exclusive);
    require(result.success, "announced exclusive mode applies");
    require(change_count == 3, "exclusive change is notified");

    exclusive.refresh_rate = 0.0f;
    result = manager.apply(exclusive);
    require(result.success && result.state.settings.refresh_rate == 120.0f, "zero refresh rate resolves to the best matching mode");

    fake->caps.can_change_resolution = false;
    Engine::DisplaySettings unavailable_resolution{2, Engine::WindowMode::ExclusiveFullscreen, 1280, 720, 60.0f, true};
    result = manager.apply(unavailable_resolution);
    require(!result.success && result.error == Engine::DisplayApplyError::Unsupported, "unsupported fullscreen resolution changes are explicit");
    fake->caps.can_change_resolution = true;
    fake->caps.can_change_refresh_rate = false;
    Engine::DisplaySettings unavailable_refresh{2, Engine::WindowMode::ExclusiveFullscreen, 1920, 1080, 60.0f, true};
    result = manager.apply(unavailable_refresh);
    require(!result.success && result.error == Engine::DisplayApplyError::Unsupported, "unsupported refresh-rate changes are explicit");
    fake->caps.can_change_refresh_rate = true;

    exclusive.refresh_rate = 75.0f;
    result = manager.apply(exclusive);
    require(!result.success && result.error == Engine::DisplayApplyError::InvalidMode, "unknown exclusive mode is rejected");

    const Engine::DisplayState before_failure = manager.current_state();
    fake->failures_remaining = 1;
    result = manager.apply(windowed);
    require(!result.success && result.error == Engine::DisplayApplyError::PlatformFailure, "platform failure is reported");
    require(manager.current_state() == before_failure, "failed apply rolls back to previous state");

    fake->failures_remaining = 2;
    result = manager.apply(windowed);
    require(!result.success && result.error == Engine::DisplayApplyError::RollbackFailure, "rollback failure is explicit");

    fake->caps = {};
    fake->state = before_failure;
    manager.refresh_from_system();
    result = manager.apply(windowed);
    require(!result.success && result.error == Engine::DisplayApplyError::Unsupported, "mobile-like capabilities reject mutable settings");

    fake->caps = {true, true, true, true, true};
    fake->displays.erase(fake->displays.begin() + 1);
    fake->state.settings.display_id = 2;
    fake->state.orientation = Engine::DisplayOrientation::Portrait;
    manager.refresh_from_system();
    require(manager.current_state().settings.display_id == 1, "removed display falls back to primary");
    require(manager.current_state().orientation == Engine::DisplayOrientation::Portrait, "orientation refreshes from system");

    Engine::DisplayApplyError thread_error = Engine::DisplayApplyError::None;
    std::thread worker([&]() {
        thread_error = manager.apply(manager.current_state().settings).error;
    });
    worker.join();
    require(thread_error == Engine::DisplayApplyError::WrongThread, "apply is restricted to main thread");

    return EXIT_SUCCESS;
}
