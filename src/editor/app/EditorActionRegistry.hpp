#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

struct EditorAction {
    std::string id;
    std::string label;
    std::string category;
    std::string shortcut_id;
    std::function<void()> execute;
    std::function<bool()> enabled;
    std::function<bool()> checked;
};

class EditorActionRegistry {
public:
    bool register_action(EditorAction action);
    [[nodiscard]] bool execute(std::string_view id) const;
    [[nodiscard]] bool is_enabled(std::string_view id) const;
    [[nodiscard]] bool is_checked(std::string_view id) const;
    [[nodiscard]] const EditorAction* find(std::string_view id) const;
    [[nodiscard]] std::vector<const EditorAction*> search(std::string_view query) const;
    [[nodiscard]] const std::vector<EditorAction>& actions() const { return m_actions; }

private:
    std::vector<EditorAction> m_actions;
    std::unordered_map<std::string, std::size_t> m_indices;
};

} // namespace Engine
