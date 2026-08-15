#pragma once

#include <string>

namespace Engine {

// ---------------------------------------------------------------------------
// EditorPanel
// Base abstract class for all modular editor panels (Hierarchy, Inspector,
// Viewport, Console, Profiler, Exporter, etc.)
// ---------------------------------------------------------------------------
class EditorPanel {
public:
    virtual ~EditorPanel() = default;

    virtual void on_gui() = 0;
    virtual void on_update(float /*delta_time*/) {}
    [[nodiscard]] virtual const std::string& get_name() const = 0;

    bool is_open{true};
};

} // namespace Engine
