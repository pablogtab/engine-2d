#include "ShortcutManager.hpp"

#include "../../core/Log.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace Engine {

namespace {

constexpr int SHORTCUT_STATE_VERSION = 3;

struct KeyMapping {
    EditorKey editor;
    ImGuiKey imgui;
    const char* label;
};

constexpr std::array<KeyMapping, 34> KEY_MAPPINGS{{
    {EditorKey::A, ImGuiKey_A, "A"}, {EditorKey::B, ImGuiKey_B, "B"},
    {EditorKey::C, ImGuiKey_C, "C"}, {EditorKey::D, ImGuiKey_D, "D"},
    {EditorKey::E, ImGuiKey_E, "E"}, {EditorKey::F, ImGuiKey_F, "F"},
    {EditorKey::G, ImGuiKey_G, "G"}, {EditorKey::H, ImGuiKey_H, "H"},
    {EditorKey::I, ImGuiKey_I, "I"}, {EditorKey::J, ImGuiKey_J, "J"},
    {EditorKey::K, ImGuiKey_K, "K"}, {EditorKey::L, ImGuiKey_L, "L"},
    {EditorKey::M, ImGuiKey_M, "M"}, {EditorKey::N, ImGuiKey_N, "N"},
    {EditorKey::O, ImGuiKey_O, "O"}, {EditorKey::P, ImGuiKey_P, "P"},
    {EditorKey::Q, ImGuiKey_Q, "Q"}, {EditorKey::R, ImGuiKey_R, "R"},
    {EditorKey::S, ImGuiKey_S, "S"}, {EditorKey::T, ImGuiKey_T, "T"},
    {EditorKey::U, ImGuiKey_U, "U"}, {EditorKey::V, ImGuiKey_V, "V"},
    {EditorKey::W, ImGuiKey_W, "W"}, {EditorKey::X, ImGuiKey_X, "X"},
    {EditorKey::Y, ImGuiKey_Y, "Y"}, {EditorKey::Z, ImGuiKey_Z, "Z"},
    {EditorKey::Backspace, ImGuiKey_Backspace, "Backspace"},
    {EditorKey::Delete, ImGuiKey_Delete, "Delete"},
    {EditorKey::Escape, ImGuiKey_Escape, "Escape"},
    {EditorKey::Home, ImGuiKey_Home, "Home"},
    {EditorKey::Up, ImGuiKey_UpArrow, "Up"},
    {EditorKey::Down, ImGuiKey_DownArrow, "Down"},
    {EditorKey::F1, ImGuiKey_F1, "F1"},
    {EditorKey::F2, ImGuiKey_F2, "F2"}
}};

const KeyMapping* mapping(EditorKey key) {
    const auto found = std::find_if(KEY_MAPPINGS.begin(), KEY_MAPPINGS.end(),
        [key](const KeyMapping& candidate) { return candidate.editor == key; });
    return found == KEY_MAPPINGS.end() ? nullptr : &*found;
}

ImGuiKey imgui_key(EditorKey key) {
    const KeyMapping* found = mapping(key);
    return found ? found->imgui : ImGuiKey_None;
}

const char* key_label(EditorKey key) {
    const KeyMapping* found = mapping(key);
    return found ? found->label : "";
}

bool modifiers_match(const ShortcutBinding& binding, const ImGuiIO& io) {
#ifdef __APPLE__
    // ImGui swaps the logical Ctrl/Super states when macOS behaviors are
    // enabled. Translate them back to the physical modifiers exposed by the
    // editor contract.
    const bool command_down = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeySuper;
    const bool control_down = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
    if (binding.mod_cmd != command_down) return false;
    if (binding.mod_ctrl != control_down) return false;
#else
    if ((binding.mod_cmd || binding.mod_ctrl) != io.KeyCtrl) return false;
#endif
    if (binding.mod_shift != io.KeyShift) return false;
    if (binding.mod_alt != io.KeyAlt) return false;
    return true;
}

bool chord_matches(
    const ShortcutBinding& binding,
    EditorKey key,
    bool command,
    bool control,
    bool shift,
    bool alt
) {
    return key != EditorKey::None &&
        binding.primary_key == key &&
        binding.mod_cmd == command &&
        binding.mod_ctrl == control &&
        binding.mod_shift == shift &&
        binding.mod_alt == alt;
}

void copy_chord(ShortcutBinding& destination, const ShortcutBinding& source) {
    destination.primary_key = source.primary_key;
    destination.mod_cmd = source.mod_cmd;
    destination.mod_ctrl = source.mod_ctrl;
    destination.mod_shift = source.mod_shift;
    destination.mod_alt = source.mod_alt;
}

} // namespace

void ShortcutManager::register_shortcut(const ShortcutBinding& binding) {
    m_shortcuts[binding.id] = binding;
    m_default_shortcuts[binding.id] = binding;
}

bool ShortcutManager::is_triggered(const std::string& action_id) {
    const auto found = m_shortcuts.find(action_id);
    if (found == m_shortcuts.end() || found->second.primary_key == EditorKey::None) return false;

    const ShortcutBinding& binding = found->second;
    const ImGuiIO& io = ImGui::GetIO();
    if (!modifiers_match(binding, io)) return false;

    bool pressed = ImGui::IsKeyPressed(imgui_key(binding.primary_key), false);
    if (!pressed && action_id == "editor.delete") {
        if (binding.primary_key == EditorKey::Backspace) {
            pressed = ImGui::IsKeyPressed(ImGuiKey_Delete, false);
        } else if (binding.primary_key == EditorKey::Delete) {
            pressed = ImGui::IsKeyPressed(ImGuiKey_Backspace, false);
        }
    }
    return pressed;
}

std::string ShortcutManager::get_label(const std::string& action_id) const {
    const auto found = m_shortcuts.find(action_id);
    if (found == m_shortcuts.end() || found->second.primary_key == EditorKey::None) return {};

    const ShortcutBinding& binding = found->second;
    return format_label(
        binding.primary_key, binding.mod_cmd, binding.mod_ctrl,
        binding.mod_shift, binding.mod_alt
    );
}

std::string ShortcutManager::format_label(
    EditorKey key,
    bool command,
    bool control,
    bool shift,
    bool alt
) const {
    if (key == EditorKey::None) return {};
    std::string result;
#ifdef __APPLE__
    if (command) result += "Cmd+";
    if (control) result += "Ctrl+";
#else
    if (command || control) result += "Ctrl+";
#endif
    if (shift) result += "Shift+";
    if (alt) result += "Alt+";
    result += key_label(key);
    return result;
}

bool ShortcutManager::rebind(
    const std::string& action_id,
    EditorKey key,
    bool command,
    bool control,
    bool shift,
    bool alt,
    bool replace_conflicts
) {
    const auto found = m_shortcuts.find(action_id);
    if (found == m_shortcuts.end()) return false;

    if (replace_conflicts && key != EditorKey::None) {
        for (auto& [other_id, other] : m_shortcuts) {
            if (other_id == action_id ||
                !chord_matches(other, key, command, control, shift, alt)) {
                continue;
            }
            other.primary_key = EditorKey::None;
            other.mod_cmd = false;
            other.mod_ctrl = false;
            other.mod_shift = false;
            other.mod_alt = false;
        }
    }

    ShortcutBinding& binding = found->second;
    binding.primary_key = key;
    binding.mod_cmd = command;
    binding.mod_ctrl = control;
    binding.mod_shift = shift;
    binding.mod_alt = alt;
    ENGINE_LOG_INFO("[SHORTCUTS] Rebound action '{}' to '{}'.", action_id, get_label(action_id));
    save_to_file();
    return true;
}

std::vector<std::string> ShortcutManager::conflicts_for(
    const std::string& action_id,
    EditorKey key,
    bool command,
    bool control,
    bool shift,
    bool alt
) const {
    std::vector<std::string> conflicts;
    if (key == EditorKey::None) return conflicts;
    for (const auto& [other_id, binding] : m_shortcuts) {
        if (other_id != action_id &&
            chord_matches(binding, key, command, control, shift, alt)) {
            conflicts.push_back(other_id);
        }
    }
    std::sort(conflicts.begin(), conflicts.end());
    return conflicts;
}

bool ShortcutManager::is_default(const std::string& action_id) const {
    const auto current = m_shortcuts.find(action_id);
    const auto default_binding = m_default_shortcuts.find(action_id);
    if (current == m_shortcuts.end() || default_binding == m_default_shortcuts.end()) {
        return false;
    }
    const ShortcutBinding& left = current->second;
    const ShortcutBinding& right = default_binding->second;
    return left.primary_key == right.primary_key &&
        left.mod_cmd == right.mod_cmd &&
        left.mod_ctrl == right.mod_ctrl &&
        left.mod_shift == right.mod_shift &&
        left.mod_alt == right.mod_alt;
}

bool ShortcutManager::reset_to_default(const std::string& action_id) {
    const auto current = m_shortcuts.find(action_id);
    const auto default_binding = m_default_shortcuts.find(action_id);
    if (current == m_shortcuts.end() || default_binding == m_default_shortcuts.end()) {
        return false;
    }
    copy_chord(current->second, default_binding->second);
    save_to_file();
    return true;
}

EditorKey ShortcutManager::capture_pressed_key() const {
    for (const KeyMapping& candidate : KEY_MAPPINGS) {
        if (ImGui::IsKeyPressed(candidate.imgui, false)) return candidate.editor;
    }
    return EditorKey::None;
}

void ShortcutManager::update(float dt, bool want_text_input) {
    if (want_text_input) return;
    const ImGuiIO& io = ImGui::GetIO();

    for (auto& [id, binding] : m_shortcuts) {
        (void)id;
        if (binding.primary_key == EditorKey::None) continue;
        const ImGuiKey key = imgui_key(binding.primary_key);
        const bool modifiers = modifiers_match(binding, io);
        const bool down = ImGui::IsKeyDown(key) && modifiers;
        const bool pressed = ImGui::IsKeyPressed(key, false) && modifiers;

        if (binding.enable_repeat) {
            if (pressed) {
                if (binding.callback) binding.callback();
                binding.is_holding = true;
                binding.hold_timer = 0.0f;
                binding.current_interval = 0.40f;
            } else if (binding.is_holding && down) {
                binding.hold_timer += dt;
                if (binding.hold_timer >= binding.current_interval) {
                    if (binding.callback) binding.callback();
                    binding.hold_timer = 0.0f;
                    binding.current_interval = std::max(0.06f, binding.current_interval * 0.78f);
                }
            } else if (!down) {
                binding.is_holding = false;
                binding.hold_timer = 0.0f;
                binding.current_interval = 0.40f;
            }
        } else if (pressed && binding.callback) {
            binding.callback();
        }
    }
}

void ShortcutManager::save_to_file(const std::string& filepath) {
    const std::string destination = filepath.empty() ? m_filepath : filepath;
    try {
        std::filesystem::create_directories(std::filesystem::path(destination).parent_path());
        json shortcuts = json::object();
        for (const auto& [id, binding] : m_shortcuts) {
            shortcuts[id] = {
                {"key", static_cast<int>(binding.primary_key)},
                {"cmd", binding.mod_cmd},
                {"ctrl", binding.mod_ctrl},
                {"shift", binding.mod_shift},
                {"alt", binding.mod_alt}
            };
        }
        const json root{{"version", SHORTCUT_STATE_VERSION}, {"shortcuts", shortcuts}};
        std::ofstream file(destination);
        if (file.is_open()) {
            file << root.dump(4);
            ENGINE_LOG_INFO("[SHORTCUTS] Saved preferences to '{}'.", destination);
        }
    } catch (const std::exception& error) {
        ENGINE_LOG_ERROR("[SHORTCUTS] Failed to save shortcuts: {}", error.what());
    }
}

void ShortcutManager::load_from_file(const std::string& filepath) {
    m_filepath = filepath;
    if (!std::filesystem::exists(filepath)) return;

    try {
        std::ifstream file(filepath);
        if (!file.is_open()) return;
        const json root = json::parse(file, nullptr, false);
        if (root.is_discarded()) {
            ENGINE_LOG_WARN("[SHORTCUTS] Ignoring invalid shortcuts config '{}'.", filepath);
            return;
        }
        const int version = root.value("version", 0);
        if ((version != 2 && version != SHORTCUT_STATE_VERSION) ||
            !root.contains("shortcuts") || !root["shortcuts"].is_object()) {
            ENGINE_LOG_WARN("[SHORTCUTS] Ignoring incompatible shortcuts config '{}'.", filepath);
            return;
        }

        const json& shortcuts = root["shortcuts"];
        for (auto& [id, binding] : m_shortcuts) {
            if (!shortcuts.contains(id)) continue;
            const json& item = shortcuts[id];
            const int key = item.value("key", static_cast<int>(EditorKey::None));
            if (key >= static_cast<int>(EditorKey::None) && key <= static_cast<int>(EditorKey::F2)) {
                binding.primary_key = static_cast<EditorKey>(key);
            }
            binding.mod_cmd = item.value("cmd", binding.mod_cmd);
            binding.mod_ctrl = item.value("ctrl", binding.mod_ctrl);
            binding.mod_shift = item.value("shift", binding.mod_shift);
            binding.mod_alt = item.value("alt", binding.mod_alt);
#ifdef __APPLE__
            // Version 2 briefly captured physical Cmd as logical Ctrl because
            // ImGui's macOS modifier swap was not normalized by the panel.
            if (version == 2 && !binding.mod_cmd && binding.mod_ctrl) {
                binding.mod_cmd = true;
                binding.mod_ctrl = false;
            }
#endif
        }
        ENGINE_LOG_INFO("[SHORTCUTS] Loaded user keybindings from '{}'.", filepath);
        if (version == 2) {
            save_to_file();
            ENGINE_LOG_INFO("[SHORTCUTS] Migrated keybindings to version {}.", SHORTCUT_STATE_VERSION);
        }
    } catch (const std::exception& error) {
        ENGINE_LOG_WARN("[SHORTCUTS] Could not parse shortcuts config: {}", error.what());
    }
}

void ShortcutManager::reset_to_defaults() {
    for (const auto& [id, default_binding] : m_default_shortcuts) {
        const auto found = m_shortcuts.find(id);
        if (found == m_shortcuts.end()) continue;
        copy_chord(found->second, default_binding);
    }
    save_to_file();
}

} // namespace Engine
