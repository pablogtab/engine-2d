#include "DisplayManager.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace Engine {

namespace {

constexpr float RefreshTolerance = 0.01f;

} // namespace

DisplayManager::DisplayManager(std::unique_ptr<IDisplayBackend> backend)
    : m_backend(std::move(backend)),
      m_main_thread(std::this_thread::get_id()) {
    if (m_backend) m_state = m_backend->query_state();
}

DisplayCapabilities DisplayManager::capabilities() const {
    return m_backend ? m_backend->capabilities() : DisplayCapabilities{};
}

std::vector<DisplayInfo> DisplayManager::available_displays() const {
    if (!m_backend) return {};
    std::vector<DisplayInfo> displays = m_backend->available_displays();
    displays.erase(std::remove_if(displays.begin(), displays.end(), [](const DisplayInfo& display) {
        return display.id == InvalidDisplayId || display.bounds.width <= 0 || display.bounds.height <= 0;
    }), displays.end());
    std::unordered_set<DisplayId> seen_ids;
    displays.erase(std::remove_if(displays.begin(), displays.end(), [&seen_ids](const DisplayInfo& display) {
        return !seen_ids.insert(display.id).second;
    }), displays.end());
    std::sort(displays.begin(), displays.end(), [](const DisplayInfo& lhs, const DisplayInfo& rhs) {
        if (lhs.is_primary != rhs.is_primary) return lhs.is_primary;
        if (lhs.bounds.y != rhs.bounds.y) return lhs.bounds.y < rhs.bounds.y;
        if (lhs.bounds.x != rhs.bounds.x) return lhs.bounds.x < rhs.bounds.x;
        if (lhs.name != rhs.name) return lhs.name < rhs.name;
        return lhs.id < rhs.id;
    });
    for (DisplayInfo& display : displays) {
        display.content_scale = std::max(display.content_scale, 0.1f);
        display.modes = available_modes(display.id);
    }
    return displays;
}

std::vector<DisplayMode> DisplayManager::available_modes(DisplayId display_id) const {
    if (!m_backend) return {};
    std::vector<DisplayMode> modes = m_backend->available_modes(display_id);
    modes.erase(std::remove_if(modes.begin(), modes.end(), [](const DisplayMode& mode) {
        return mode.width <= 0 || mode.height <= 0 || mode.pixel_density <= 0.0f;
    }), modes.end());
    std::sort(modes.begin(), modes.end(), [](const DisplayMode& lhs, const DisplayMode& rhs) {
        if (lhs.width != rhs.width) return lhs.width < rhs.width;
        if (lhs.height != rhs.height) return lhs.height < rhs.height;
        if (std::abs(lhs.refresh_rate - rhs.refresh_rate) > RefreshTolerance) {
            return lhs.refresh_rate > rhs.refresh_rate;
        }
        return lhs.pixel_density < rhs.pixel_density;
    });
    modes.erase(std::unique(modes.begin(), modes.end(), [](const DisplayMode& lhs, const DisplayMode& rhs) {
        return lhs.width == rhs.width && lhs.height == rhs.height &&
               std::abs(lhs.refresh_rate - rhs.refresh_rate) <= RefreshTolerance &&
               std::abs(lhs.pixel_density - rhs.pixel_density) <= 0.01f;
    }), modes.end());
    return modes;
}

DisplayApplyResult DisplayManager::failure(DisplayApplyError error, std::string message) const {
    return {false, error, std::move(message), m_state};
}

DisplayId DisplayManager::resolve_display(DisplayId requested, const std::vector<DisplayInfo>& displays) const {
    if (requested != InvalidDisplayId) {
        const auto found = std::find_if(displays.begin(), displays.end(), [requested](const DisplayInfo& display) {
            return display.id == requested;
        });
        if (found != displays.end()) return requested;
    }
    const auto primary = std::find_if(displays.begin(), displays.end(), [](const DisplayInfo& display) {
        return display.is_primary;
    });
    return primary != displays.end() ? primary->id : (displays.empty() ? InvalidDisplayId : displays.front().id);
}

bool DisplayManager::has_exclusive_mode(DisplayId display_id, const DisplaySettings& settings) const {
    const auto modes = available_modes(display_id);
    return std::any_of(modes.begin(), modes.end(), [&settings](const DisplayMode& mode) {
        return mode.width == settings.window_width &&
               mode.height == settings.window_height &&
               std::abs(mode.refresh_rate - settings.refresh_rate) <= RefreshTolerance;
    });
}

DisplayApplyResult DisplayManager::apply(const DisplaySettings& requested) {
    if (std::this_thread::get_id() != m_main_thread) {
        return failure(DisplayApplyError::WrongThread, "Display settings must be applied on the main thread");
    }
    if (!m_apply_enabled) {
        return failure(DisplayApplyError::UnavailableInEditor, "Physical display changes are disabled while the editor is visible");
    }
    if (!m_backend) return failure(DisplayApplyError::PlatformFailure, "Display backend is not available");

    const auto displays = available_displays();
    DisplaySettings normalized = requested;
    normalized.display_id = resolve_display(requested.display_id, displays);
    if (normalized.display_id == InvalidDisplayId) {
        return failure(DisplayApplyError::InvalidDisplay, "No available display could satisfy the request");
    }
    if (requested.display_id != InvalidDisplayId && normalized.display_id != requested.display_id) {
        return failure(DisplayApplyError::InvalidDisplay, "Requested display is no longer available");
    }
    if (normalized == m_state.settings) {
        return {true, DisplayApplyError::None, {}, m_state};
    }

    const DisplayCapabilities caps = capabilities();
    if (normalized.window_mode != m_state.settings.window_mode && !caps.can_change_window_mode) {
        return failure(DisplayApplyError::Unsupported, "Changing window mode is not supported on this platform");
    }
    if (normalized.display_id != m_state.settings.display_id && !caps.can_change_display) {
        return failure(DisplayApplyError::Unsupported, "Changing display is not supported on this platform");
    }
    if (normalized.vsync != m_state.settings.vsync && !caps.can_change_vsync) {
        return failure(DisplayApplyError::Unsupported, "Changing VSync is not supported on this platform");
    }
    if (normalized.window_mode == WindowMode::Windowed) {
        if (normalized.window_width <= 0 || normalized.window_height <= 0) {
            return failure(DisplayApplyError::InvalidMode, "Window dimensions must be positive");
        }
        const bool size_changes = normalized.window_width != m_state.logical_width ||
                                  normalized.window_height != m_state.logical_height;
        if (size_changes && !caps.can_change_resolution) {
            return failure(DisplayApplyError::Unsupported, "Changing window size is not supported on this platform");
        }
        normalized.refresh_rate = 0.0f;
    } else if (normalized.window_mode == WindowMode::BorderlessFullscreen) {
        normalized.refresh_rate = 0.0f;
    } else {
        const bool resolution_changes = normalized.window_width != m_state.settings.window_width ||
                                        normalized.window_height != m_state.settings.window_height;
        if (resolution_changes && !caps.can_change_resolution) {
            return failure(DisplayApplyError::Unsupported, "Changing fullscreen resolution is not supported on this platform");
        }
        const bool refresh_changes = std::abs(normalized.refresh_rate - m_state.settings.refresh_rate) > RefreshTolerance;
        if (refresh_changes && !caps.can_change_refresh_rate) {
            return failure(DisplayApplyError::Unsupported, "Changing refresh rate is not supported on this platform");
        }
        if (normalized.refresh_rate <= 0.0f) {
            const auto modes = available_modes(normalized.display_id);
            const auto best = std::find_if(modes.begin(), modes.end(), [&normalized](const DisplayMode& mode) {
                return mode.width == normalized.window_width && mode.height == normalized.window_height;
            });
            if (best != modes.end()) normalized.refresh_rate = best->refresh_rate;
        }
        if (!has_exclusive_mode(normalized.display_id, normalized)) {
            return failure(DisplayApplyError::InvalidMode, "Exclusive fullscreen mode is not available on the requested display");
        }
    }

    const DisplayState previous = m_state;
    std::string platform_error;
    if (!m_backend->apply(normalized, platform_error)) {
        std::string rollback_error;
        if (!m_backend->apply(previous.settings, rollback_error)) {
            m_state = m_backend->query_state();
            return failure(
                DisplayApplyError::RollbackFailure,
                platform_error + "; rollback failed: " + rollback_error
            );
        }
        m_state = m_backend->query_state();
        return failure(DisplayApplyError::PlatformFailure, std::move(platform_error));
    }

    const DisplayState updated = m_backend->query_state();
    const bool changed = updated != m_state;
    m_state = updated;
    if (changed && m_change_callback) m_change_callback(m_state);
    return {true, DisplayApplyError::None, {}, m_state};
}

void DisplayManager::refresh_from_system() {
    if (!m_backend || std::this_thread::get_id() != m_main_thread) return;
    DisplayState updated = m_backend->query_state();
    const auto displays = m_backend->available_displays();
    const DisplayId resolved = resolve_display(updated.settings.display_id, displays);
    if (resolved != InvalidDisplayId) updated.settings.display_id = resolved;
    if (updated == m_state) return;
    m_state = updated;
    if (m_change_callback) m_change_callback(m_state);
}

void DisplayManager::set_change_callback(std::function<void(const DisplayState&)> callback) {
    m_change_callback = std::move(callback);
}

} // namespace Engine
