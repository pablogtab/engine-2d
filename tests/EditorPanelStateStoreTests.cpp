#include "editor/state/EditorPanelStateStore.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace Engine;

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "engine2d_editor_panel_state_tests";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    assert(!error);
    const std::filesystem::path state_path = directory / "PanelStates.json";

    EditorPanelStateStore defaults;
    assert(defaults.is_open("Scene Viewport", true));
    assert(!defaults.is_open("Engine & Metrics", false));
    const std::uint64_t initial_revision = defaults.revision();
    defaults.set_open("Engine & Metrics", true);
    assert(defaults.revision() == initial_revision + 1);
    defaults.set_open("Engine & Metrics", true);
    assert(defaults.revision() == initial_revision + 1);
    defaults.set_open("AI Assistant", false);
    assert(defaults.save(state_path.string()));

    EditorPanelStateStore restored;
    assert(restored.load(state_path.string()));
    assert(restored.is_open("Engine & Metrics", false));
    assert(!restored.is_open("AI Assistant", true));
    assert(restored.is_open("New Panel", true));

    {
        std::ofstream invalid(state_path);
        invalid << "{\"version\":1,\"panels\":{\"Broken\":12}}\n";
    }
    EditorPanelStateStore tolerant;
    assert(tolerant.load(state_path.string()));
    assert(tolerant.states().empty());

    std::filesystem::remove_all(directory, error);
    std::cout << "EditorPanelStateStoreTests passed\n";
    return 0;
}
