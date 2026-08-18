#pragma once

#include <imgui.h>

namespace Engine {

// Reusable compact toolbar row embedded in an editor panel. Toolbars own
// interaction only inside their row and never overlap a panel's content.
class EditorToolbox {
public:
    bool begin(const char* id, float height = 22.0f);
    void end();
    [[nodiscard]] bool is_hovered() const { return m_hovered; }

private:
    bool m_hovered{false};
};

} // namespace Engine
