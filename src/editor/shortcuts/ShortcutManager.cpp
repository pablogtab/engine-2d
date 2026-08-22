#include "ShortcutManager.hpp"
#include "../../core/Log.hpp"
#include "../../core/FileSystem.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>

using json = nlohmann::json;

namespace Engine {

void ShortcutManager::register_shortcut(const ShortcutBinding& binding) {
    m_shortcuts[binding.id] = binding;
    m_default_shortcuts[binding.id] = binding;
}

bool ShortcutManager::is_triggered(const std::string& action_id) {
    auto it = m_shortcuts.find(action_id);
    if (it == m_shortcuts.end()) return false;

    const auto& b = it->second;
    if (b.primary_key == ImGuiKey_None) return false;

    ImGuiIO& io = ImGui::GetIO();
    bool has_cmd = io.KeySuper || io.KeyCtrl; // Cmd on Mac (Super), Ctrl on Win
    bool has_ctrl = io.KeyCtrl;
    bool has_shift = io.KeyShift;
    bool has_alt = io.KeyAlt;

    bool mod_match = true;
    if (b.mod_cmd && !has_cmd) mod_match = false;
    if (b.mod_ctrl && !has_ctrl) mod_match = false;
    if (b.mod_shift != has_shift) mod_match = false;
    if (b.mod_alt != has_alt) mod_match = false;

    if (!mod_match) return false;

    bool key_pressed = ImGui::IsKeyPressed(b.primary_key, false);
    if (!key_pressed && action_id == "editor.delete") {
        if (b.primary_key == ImGuiKey_Backspace) {
            key_pressed = ImGui::IsKeyPressed(ImGuiKey_Delete, false);
        } else if (b.primary_key == ImGuiKey_Delete) {
            key_pressed = ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
        }
    }

    return key_pressed;
}

std::string ShortcutManager::get_label(const std::string& action_id) const {
    auto it = m_shortcuts.find(action_id);
    if (it == m_shortcuts.end()) return "";

    const auto& b = it->second;
    if (b.primary_key == ImGuiKey_None) return "";

    std::string result = "";
    if (b.mod_cmd) {
#ifdef __APPLE__
        result += "Cmd+";
#else
        result += "Ctrl+";
#endif
    }
    if (b.mod_ctrl && !b.mod_cmd) result += "Ctrl+";
    if (b.mod_shift) result += "Shift+";
    if (b.mod_alt) result += "Alt+";

    const char* key_name = ImGui::GetKeyName(b.primary_key);
    if (key_name) {
        std::string name_str = key_name;
        if (name_str.rfind("ImGuiKey_", 0) == 0) {
            name_str = name_str.substr(9);
        }
        result += name_str;
    }

    return result;
}

bool ShortcutManager::rebind(const std::string& action_id, ImGuiKey key, bool cmd, bool ctrl, bool shift, bool alt) {
    auto it = m_shortcuts.find(action_id);
    if (it == m_shortcuts.end()) return false;

    it->second.primary_key = key;
    it->second.mod_cmd = cmd;
    it->second.mod_ctrl = ctrl;
    it->second.mod_shift = shift;
    it->second.mod_alt = alt;

    ENGINE_LOG_INFO("[SHORTCUTS] Rebound action '{}' to '{}'", action_id, get_label(action_id));
    save_to_file();
    return true;
}

void ShortcutManager::update(float dt, bool want_text_input) {
    if (want_text_input) return;

    ImGuiIO& io = ImGui::GetIO();

    for (auto& [id, b] : m_shortcuts) {
        if (b.primary_key == ImGuiKey_None) continue;

        bool has_cmd = io.KeySuper || io.KeyCtrl; // Cmd on Mac (Super), Ctrl on Win
        bool has_ctrl = io.KeyCtrl;
        bool has_shift = io.KeyShift;
        bool has_alt = io.KeyAlt;

        bool mod_match = true;
        if (b.mod_cmd && !has_cmd) mod_match = false;
        if (b.mod_ctrl && !has_ctrl) mod_match = false;
        if (b.mod_shift != has_shift) mod_match = false;
        if (b.mod_alt != has_alt) mod_match = false;

        bool is_key_down = ImGui::IsKeyDown(b.primary_key) && mod_match;
        bool is_key_pressed = ImGui::IsKeyPressed(b.primary_key, false) && mod_match;

        if (b.enable_repeat) {
            // Smart acceleration curve logic
            if (is_key_pressed) {
                if (b.callback) b.callback();
                b.is_holding = true;
                b.hold_timer = 0.0f;
                b.current_interval = 0.40f;
            } else if (b.is_holding && is_key_down) {
                b.hold_timer += dt;
                if (b.hold_timer >= b.current_interval) {
                    if (b.callback) b.callback();
                    b.hold_timer = 0.0f;
                    b.current_interval = std::max(0.06f, b.current_interval * 0.78f);
                }
            } else if (!is_key_down) {
                b.is_holding = false;
                b.hold_timer = 0.0f;
                b.current_interval = 0.40f;
            }
        } else {
            // Single tap trigger
            if (is_key_pressed && b.callback) {
                b.callback();
            }
        }
    }
}

void ShortcutManager::save_to_file(const std::string& filepath) {
    const std::string destination = filepath.empty() ? m_filepath : filepath;
    try {
        std::filesystem::create_directories(std::filesystem::path(destination).parent_path());
        json root;

        for (const auto& [id, b] : m_shortcuts) {
            json item;
            item["key"] = static_cast<int>(b.primary_key);
            item["cmd"] = b.mod_cmd;
            item["ctrl"] = b.mod_ctrl;
            item["shift"] = b.mod_shift;
            item["alt"] = b.mod_alt;
            root[id] = item;
        }

        std::ofstream file(destination);
        if (file.is_open()) {
            file << root.dump(4);
            ENGINE_LOG_INFO("[SHORTCUTS] Saved preferences to '{}'", destination);
        }
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[SHORTCUTS] Failed to save shortcuts: {}", e.what());
    }
}

void ShortcutManager::load_from_file(const std::string& filepath) {
    m_filepath = filepath;
    if (!std::filesystem::exists(filepath)) return;

    try {
        std::ifstream file(filepath);
        if (!file.is_open()) return;

        json root;
        file >> root;

        for (auto& [id, b] : m_shortcuts) {
            if (root.contains(id)) {
                const auto& item = root[id];
                if (item.contains("key")) b.primary_key = static_cast<ImGuiKey>(item["key"].get<int>());
                if (item.contains("cmd")) b.mod_cmd = item["cmd"].get<bool>();
                if (item.contains("ctrl")) b.mod_ctrl = item["ctrl"].get<bool>();
                if (item.contains("shift")) b.mod_shift = item["shift"].get<bool>();
                if (item.contains("alt")) b.mod_alt = item["alt"].get<bool>();
            }
        }
        ENGINE_LOG_INFO("[SHORTCUTS] Loaded user keybindings from '{}'", filepath);
    } catch (const std::exception& e) {
        ENGINE_LOG_WARN("[SHORTCUTS] Could not parse shortcuts config: {}", e.what());
    }
}

void ShortcutManager::reset_to_defaults() {
    for (const auto& [id, def_b] : m_default_shortcuts) {
        auto it = m_shortcuts.find(id);
        if (it != m_shortcuts.end()) {
            it->second.primary_key = def_b.primary_key;
            it->second.mod_cmd = def_b.mod_cmd;
            it->second.mod_ctrl = def_b.mod_ctrl;
            it->second.mod_shift = def_b.mod_shift;
            it->second.mod_alt = def_b.mod_alt;
        }
    }
    save_to_file();
}

} // namespace Engine
