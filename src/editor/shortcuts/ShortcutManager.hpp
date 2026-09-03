#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

namespace Engine {

enum class EditorKey {
    None,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Backspace,
    Delete,
    Escape,
    Home,
    Up,
    Down,
    F1,
    F2
};

struct ShortcutBinding {
    std::string id;            // e.g. "editor.undo"
    std::string display_name;  // e.g. "Undo Action"
    std::string category;      // e.g. "Editing", "Camera", "General"
    
    EditorKey primary_key{EditorKey::None};
    bool mod_cmd{false};       // Command key on Mac (Super) / Ctrl on Win/Linux
    bool mod_ctrl{false};      // Raw Ctrl key
    bool mod_shift{false};
    bool mod_alt{false};
    
    bool enable_repeat{false}; // Enable smart acceleration curve (for Undo/Redo)
    std::function<void()> callback{nullptr};

    // Internal runtime state for repeat acceleration
    float hold_timer{0.0f};
    float current_interval{0.40f};
    bool is_holding{false};
};

class ShortcutManager {
public:
    static ShortcutManager& get() {
        static ShortcutManager s_instance;
        return s_instance;
    }

    ShortcutManager() = default;
    ~ShortcutManager() = default;

    ShortcutManager(const ShortcutManager&) = delete;
    ShortcutManager& operator=(const ShortcutManager&) = delete;

    // Registers an action binding
    void register_shortcut(const ShortcutBinding& binding);

    // Checks if an action was triggered in the current frame
    [[nodiscard]] bool is_triggered(const std::string& action_id);

    // Formats shortcut for UI menus (e.g. "Cmd+Z" or "Ctrl+Z")
    [[nodiscard]] std::string get_label(const std::string& action_id) const;
    [[nodiscard]] std::string format_label(
        EditorKey key,
        bool cmd,
        bool ctrl,
        bool shift,
        bool alt
    ) const;

    // Rebinds an action to a new key combination
    bool rebind(
        const std::string& action_id,
        EditorKey key,
        bool cmd,
        bool ctrl,
        bool shift,
        bool alt,
        bool replace_conflicts = false
    );

    [[nodiscard]] std::vector<std::string> conflicts_for(
        const std::string& action_id,
        EditorKey key,
        bool cmd,
        bool ctrl,
        bool shift,
        bool alt
    ) const;
    [[nodiscard]] bool is_default(const std::string& action_id) const;
    bool reset_to_default(const std::string& action_id);

    [[nodiscard]] EditorKey capture_pressed_key() const;

    // Process shortcuts and invoke callbacks
    void update(float dt, bool want_text_input);

    // Load / Save user customization to JSON file
    void save_to_file(const std::string& filepath = "");
    void load_from_file(const std::string& filepath);
    void reset_to_defaults();

    [[nodiscard]] const std::unordered_map<std::string, ShortcutBinding>& get_shortcuts() const { return m_shortcuts; }

private:
    std::unordered_map<std::string, ShortcutBinding> m_shortcuts;
    std::unordered_map<std::string, ShortcutBinding> m_default_shortcuts;
    std::string m_filepath{"ProjectSettings/EditorShortcuts.json"};
};

} // namespace Engine
