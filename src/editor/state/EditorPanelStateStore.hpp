#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Engine {

// Project-local visibility state for editor panels. Dear ImGui persists window
// geometry and docking, but intentionally does not persist an application's
// own open/closed boolean.
class EditorPanelStateStore {
public:
    [[nodiscard]] bool load(const std::string& filepath);
    [[nodiscard]] bool save(const std::string& filepath) const;

    [[nodiscard]] bool is_open(const std::string& panel_name, bool default_value) const;
    void set_open(const std::string& panel_name, bool value);

    [[nodiscard]] std::uint64_t revision() const { return m_revision; }
    [[nodiscard]] const std::unordered_map<std::string, bool>& states() const { return m_states; }

private:
    std::unordered_map<std::string, bool> m_states;
    std::uint64_t m_revision{0};
};

} // namespace Engine
