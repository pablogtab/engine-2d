#pragma once

#include "DisplayBackend.hpp"
#include <functional>
#include <memory>
#include <thread>

namespace Engine {

class DisplayManager {
public:
    explicit DisplayManager(std::unique_ptr<IDisplayBackend> backend);

    [[nodiscard]] const DisplayState& current_state() const { return m_state; }
    [[nodiscard]] DisplayCapabilities capabilities() const;
    [[nodiscard]] std::vector<DisplayInfo> available_displays() const;
    [[nodiscard]] std::vector<DisplayMode> available_modes(DisplayId display_id) const;

    DisplayApplyResult apply(const DisplaySettings& settings);
    void refresh_from_system();
    void set_change_callback(std::function<void(const DisplayState&)> callback);

private:
    friend class Application;

    void set_apply_enabled(bool enabled) { m_apply_enabled = enabled; }
    [[nodiscard]] DisplayApplyResult failure(DisplayApplyError error, std::string message) const;
    [[nodiscard]] DisplayId resolve_display(DisplayId requested, const std::vector<DisplayInfo>& displays) const;
    [[nodiscard]] bool has_exclusive_mode(DisplayId display_id, const DisplaySettings& settings) const;

    std::unique_ptr<IDisplayBackend> m_backend;
    DisplayState m_state;
    std::thread::id m_main_thread;
    std::function<void(const DisplayState&)> m_change_callback;
    bool m_apply_enabled{true};
};

} // namespace Engine
