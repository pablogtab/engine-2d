#pragma once

#include "../EditorPanel.hpp"
#include "../shortcuts/ShortcutManager.hpp"
#include <string>

namespace Engine {

class ShortcutsPreferencesPanel : public EditorPanel {
public:
    ShortcutsPreferencesPanel() = default;
    ~ShortcutsPreferencesPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

private:
    std::string m_name{"Shortcuts & Keybindings"};
    char m_search_filter[64]{""};
    std::string m_rebinding_action_id{""};
};

} // namespace Engine
