#include "ApplicationLifecycle.hpp"

#include <SDL3/SDL_events.h>

namespace Engine {

ApplicationLifecycleActions ApplicationLifecycle::handle(ApplicationLifecycleEvent event) {
    ApplicationLifecycleActions actions;
    if (event == ApplicationLifecycleEvent::LowMemory) {
        actions.trim_memory = true;
        return actions;
    }
    if (event == ApplicationLifecycleEvent::Terminating) {
        if (m_state == ApplicationLifecycleState::Terminating) return actions;
        actions.state_changed = true;
        actions.suspend_runtime = m_state == ApplicationLifecycleState::Foreground ||
            m_state == ApplicationLifecycleState::Resuming;
        actions.invalidate_render_surface = true;
        actions.request_termination = true;
        m_state = ApplicationLifecycleState::Terminating;
        return actions;
    }

    switch (event) {
        case ApplicationLifecycleEvent::WillEnterBackground:
            if (m_state == ApplicationLifecycleState::Foreground ||
                m_state == ApplicationLifecycleState::Resuming) {
                m_state = ApplicationLifecycleState::Suspending;
                actions.state_changed = true;
                actions.suspend_runtime = true;
                actions.invalidate_render_surface = true;
            }
            break;
        case ApplicationLifecycleEvent::DidEnterBackground:
            if (m_state != ApplicationLifecycleState::Suspended &&
                m_state != ApplicationLifecycleState::Terminating) {
                actions.suspend_runtime = m_state == ApplicationLifecycleState::Foreground ||
                    m_state == ApplicationLifecycleState::Resuming;
                actions.invalidate_render_surface = actions.suspend_runtime;
                actions.state_changed = true;
                m_state = ApplicationLifecycleState::Suspended;
            }
            break;
        case ApplicationLifecycleEvent::WillEnterForeground:
            if (m_state == ApplicationLifecycleState::Suspended ||
                m_state == ApplicationLifecycleState::Suspending) {
                m_state = ApplicationLifecycleState::Resuming;
                actions.state_changed = true;
                actions.restore_render_surface = true;
            }
            break;
        case ApplicationLifecycleEvent::DidEnterForeground:
            if (m_state != ApplicationLifecycleState::Foreground &&
                m_state != ApplicationLifecycleState::Terminating) {
                m_state = ApplicationLifecycleState::Foreground;
                actions.state_changed = true;
                actions.restore_render_surface = true;
                actions.resume_runtime = true;
                actions.refresh_display = true;
                actions.reset_frame_clock = true;
            }
            break;
        case ApplicationLifecycleEvent::LowMemory:
        case ApplicationLifecycleEvent::Terminating:
            break;
    }
    return actions;
}

std::optional<ApplicationLifecycleEvent> application_lifecycle_event_from_sdl(
    std::uint32_t event_type
) {
    switch (event_type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            return ApplicationLifecycleEvent::WillEnterBackground;
        case SDL_EVENT_DID_ENTER_BACKGROUND:
            return ApplicationLifecycleEvent::DidEnterBackground;
        case SDL_EVENT_WILL_ENTER_FOREGROUND:
            return ApplicationLifecycleEvent::WillEnterForeground;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            return ApplicationLifecycleEvent::DidEnterForeground;
        case SDL_EVENT_LOW_MEMORY:
            return ApplicationLifecycleEvent::LowMemory;
        case SDL_EVENT_TERMINATING:
            return ApplicationLifecycleEvent::Terminating;
        default:
            return std::nullopt;
    }
}

} // namespace Engine
