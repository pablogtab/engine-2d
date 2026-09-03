#pragma once

#include "../EditorPanel.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include <string>
#include <vector>

namespace Engine {

class ShortcutsPreferencesPanel : public EditorPanel {
public:
    ShortcutsPreferencesPanel();
    ~ShortcutsPreferencesPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

private:
    struct PendingBinding {
        std::string action_id;
        EditorKey key{EditorKey::None};
        bool command{false};
        bool control{false};
        bool shift{false};
        bool alt{false};
        std::vector<std::string> conflicts;
    };

    std::string m_name{"Shortcuts & Keybindings"};
    char m_search_filter[64]{""};
    std::string m_rebinding_action_id{""};
    PendingBinding m_pending;
};

} // namespace Engine
