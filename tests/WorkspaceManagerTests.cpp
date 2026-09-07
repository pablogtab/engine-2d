#include "editor/workspace/WorkspaceManager.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace Engine;

int main() {
    WorkspaceManager manager;
    assert(manager.active() == EditorWorkspaceId::TwoD);
    assert(manager.contains(EditorPanelId::Viewport));
    assert(manager.contains(EditorPanelId::Hierarchy));
    assert(manager.contains(EditorPanelId::Console));
    assert(manager.contains(EditorPanelId::TextureComposer));
    assert(!manager.contains(EditorPanelId::Assistant));
    assert(!manager.contains(EditorPanelId::Metrics));

    assert(manager.set_active(EditorWorkspaceId::Animation));
    assert(manager.contains(EditorPanelId::Assets));
    assert(manager.contains(EditorPanelId::Animation));
    assert(!manager.contains(EditorPanelId::Hierarchy));
    assert(!manager.contains(EditorPanelId::Viewport));
    assert(!manager.contains(EditorPanelId::Inspector));

    assert(manager.set_active(EditorWorkspaceId::Tilemap));
    assert(std::string(manager.active_descriptor().key) == "tilemap");
    assert(std::string(manager.active_descriptor().label) == "Level");
    assert(manager.contains(EditorPanelId::Tilemap));
    assert(manager.contains(EditorPanelId::LevelComposer));
    assert(manager.contains(EditorPanelId::SurfaceGenerator));
    assert(manager.contains(EditorPanelId::TextureComposer));
    assert(std::string(editor_panel_id_key(EditorPanelId::LevelComposer)) ==
        "level_composer");
    assert(std::string(editor_panel_id_key(EditorPanelId::SurfaceGenerator)) ==
        "surface_generator");
    assert(std::string(editor_panel_id_key(EditorPanelId::TextureComposer)) ==
        "texture_composer");

    assert(manager.set_active(EditorWorkspaceId::Vfx));
    assert(manager.contains(EditorPanelId::Particles));
    assert(manager.contains(EditorPanelId::TrailPreview));
    assert(manager.contains(EditorPanelId::VfxPreview));
    assert(manager.contains(EditorPanelId::PostProcess));
    assert(!manager.contains(EditorPanelId::Console));
    assert(std::string(editor_panel_id_key(EditorPanelId::TrailPreview)) ==
        "trail_preview");
    assert(std::string(editor_panel_id_key(EditorPanelId::VfxPreview)) ==
        "vfx_preview");

    assert(manager.set_active(EditorWorkspaceId::Debug));
    assert(!manager.set_active(EditorWorkspaceId::Debug));
    assert(manager.contains(EditorPanelId::Metrics));
    assert(manager.contains(EditorPanelId::Diagnostics));
    assert(!manager.contains(EditorPanelId::Hierarchy));

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "engine2d_workspace_manager_tests";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    assert(!error);
    const std::filesystem::path state_path = directory / "WorkspaceState.json";

    assert(manager.save(state_path.string()));
    WorkspaceManager restored;
    assert(restored.load(state_path.string()));
    assert(restored.active() == EditorWorkspaceId::Debug);

    {
        std::ofstream invalid(state_path);
        invalid << "{\"version\":99,\"active\":\"vfx\"}\n";
    }
    WorkspaceManager tolerant;
    assert(!tolerant.load(state_path.string()));
    assert(tolerant.active() == EditorWorkspaceId::TwoD);

    std::filesystem::remove_all(directory, error);
    std::cout << "WorkspaceManagerTests passed\n";
    return 0;
}
