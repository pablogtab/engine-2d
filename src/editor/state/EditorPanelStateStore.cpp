#include "EditorPanelStateStore.hpp"

#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>

namespace Engine {

namespace {
constexpr int PANEL_STATE_VERSION = 1;
}

bool EditorPanelStateStore::load(const std::string& filepath) {
    m_states.clear();
    m_revision = 0;

    std::error_code error;
    if (!std::filesystem::is_regular_file(filepath, error)) return true;
    const std::string text = FileSystem::read_text(filepath);
    if (text.empty()) return true;
    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object() ||
        root.value("version", 0) != PANEL_STATE_VERSION ||
        !root.contains("panels") || !root["panels"].is_object()) {
        ENGINE_LOG_WARN("[EDITOR] Ignoring invalid panel-state file '{}'", filepath);
        return false;
    }

    for (const auto& [name, value] : root["panels"].items()) {
        if (!name.empty() && value.is_boolean()) m_states.emplace(name, value.get<bool>());
    }
    return true;
}

bool EditorPanelStateStore::save(const std::string& filepath) const {
    nlohmann::json panels = nlohmann::json::object();
    for (const auto& [name, open] : m_states) panels[name] = open;
    const std::string text = nlohmann::json{
        {"version", PANEL_STATE_VERSION},
        {"panels", std::move(panels)}
    }.dump(2) + '\n';
    if (FileSystem::write_text_atomically(filepath, text)) return true;
    ENGINE_LOG_ERROR("[EDITOR] Failed saving panel states to '{}'", filepath);
    return false;
}

bool EditorPanelStateStore::is_open(const std::string& panel_name, bool default_value) const {
    const auto found = m_states.find(panel_name);
    return found == m_states.end() ? default_value : found->second;
}

void EditorPanelStateStore::set_open(const std::string& panel_name, bool value) {
    if (panel_name.empty()) return;
    const auto [found, inserted] = m_states.emplace(panel_name, value);
    if (inserted) {
        ++m_revision;
        return;
    }
    if (found->second == value) return;
    found->second = value;
    ++m_revision;
}

} // namespace Engine
