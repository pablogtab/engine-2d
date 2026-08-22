#pragma once

#include <cstdint>
#include <optional>

namespace Engine {

enum class ApplicationLifecycleEvent {
    WillEnterBackground,
    DidEnterBackground,
    WillEnterForeground,
    DidEnterForeground,
    LowMemory,
    Terminating
};

enum class ApplicationLifecycleState {
    Foreground,
    Suspending,
    Suspended,
    Resuming,
    Terminating
};

struct ApplicationLifecycleActions {
    bool state_changed{false};
    bool suspend_runtime{false};
    bool resume_runtime{false};
    bool trim_memory{false};
    bool invalidate_render_surface{false};
    bool restore_render_surface{false};
    bool refresh_display{false};
    bool reset_frame_clock{false};
    bool request_termination{false};
};

class ApplicationLifecycle {
public:
    [[nodiscard]] ApplicationLifecycleActions handle(ApplicationLifecycleEvent event);
    [[nodiscard]] ApplicationLifecycleState state() const { return m_state; }
    [[nodiscard]] bool should_run_frames() const {
        return m_state == ApplicationLifecycleState::Foreground;
    }

private:
    ApplicationLifecycleState m_state{ApplicationLifecycleState::Foreground};
};

[[nodiscard]] std::optional<ApplicationLifecycleEvent> application_lifecycle_event_from_sdl(
    std::uint32_t event_type
);

} // namespace Engine
