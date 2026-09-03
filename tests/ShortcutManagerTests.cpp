#include "editor/shortcuts/ShortcutManager.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace Engine;

int main() {
    ShortcutManager& manager = ShortcutManager::get();
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("engine2d-shortcut-tests-" + std::to_string(unique));
    const std::filesystem::path preferences = directory / "EditorShortcuts.json";
    manager.load_from_file(preferences.string());

    manager.register_shortcut({
        .id = "test.first",
        .display_name = "First Action",
        .category = "Test",
        .primary_key = EditorKey::A,
        .mod_cmd = true
    });
    manager.register_shortcut({
        .id = "test.second",
        .display_name = "Second Action",
        .category = "Test",
        .primary_key = EditorKey::B,
        .mod_cmd = true
    });

#ifdef __APPLE__
    std::filesystem::create_directories(directory);
    {
        std::ofstream legacy(preferences);
        legacy << R"({
            "version": 2,
            "shortcuts": {
                "test.first": {
                    "key": 1,
                    "cmd": false,
                    "ctrl": true,
                    "shift": false,
                    "alt": false
                }
            }
        })";
    }
    manager.load_from_file(preferences.string());
    const ShortcutBinding& migrated = manager.get_shortcuts().at("test.first");
    assert(migrated.mod_cmd);
    assert(!migrated.mod_ctrl);
    {
        std::ifstream migrated_file(preferences);
        const nlohmann::json migrated_json = nlohmann::json::parse(migrated_file);
        assert(migrated_json.at("version") == 3);
    }
    assert(manager.format_label(
        EditorKey::A, true, true, true, true
    ) == "Cmd+Ctrl+Shift+Alt+A");
#else
    assert(manager.format_label(
        EditorKey::A, true, true, true, true
    ) == "Ctrl+Shift+Alt+A");
#endif
    assert(manager.is_default("test.first"));
    assert(manager.conflicts_for(
        "test.second", EditorKey::A, true, false, false, false
    ) == std::vector<std::string>{"test.first"});

    assert(manager.rebind(
        "test.second", EditorKey::A, true, false, false, false, true
    ));
    assert(manager.get_label("test.first").empty());
    assert(!manager.is_default("test.first"));
    assert(!manager.is_default("test.second"));
    assert(manager.conflicts_for(
        "test.second", EditorKey::A, true, false, false, false
    ).empty());

    assert(manager.reset_to_default("test.first"));
    assert(manager.is_default("test.first"));
    manager.reset_to_defaults();
    assert(manager.is_default("test.second"));
    assert(std::filesystem::is_regular_file(preferences));
    assert(!manager.reset_to_default("test.missing"));

    std::filesystem::remove_all(directory);
    std::cout << "ShortcutManagerTests passed\n";
    return 0;
}
