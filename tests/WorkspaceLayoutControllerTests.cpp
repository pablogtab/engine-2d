#include "editor/workspace/WorkspaceLayoutController.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace Engine;

int main() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "engine2d_workspace_layout_controller_tests";
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory / "Library/Editor", error);
    assert(!error);

    const std::string legacy_layout = "[Window][Legacy]\nPos=0,0\n";
    {
        std::ofstream legacy(directory / "Library/Editor/imgui.ini");
        legacy << legacy_layout;
    }

    WorkspaceManager workspaces;
    WorkspaceLayoutController layouts;
    layouts.initialize(directory, workspaces);

    assert(layouts.layout_path(EditorWorkspaceId::TwoD, workspaces) ==
        directory / "Library/Editor/Layouts/2d.ini");
    assert(layouts.layout_path(EditorWorkspaceId::Animation, workspaces) ==
        directory / "Library/Editor/Layouts/animation.ini");
    assert(std::filesystem::is_regular_file(
        layouts.layout_path(EditorWorkspaceId::TwoD, workspaces)
    ));

    layouts.request(EditorWorkspaceId::Animation);
    assert(layouts.has_pending_request());
    assert(!layouts.pending_uses_default());
    layouts.request_default(EditorWorkspaceId::Animation);
    assert(layouts.pending_uses_default());

    for (const auto& workspace : workspaces.descriptors()) {
        std::ofstream layout(layouts.layout_path(workspace.id, workspaces));
        layout << "[Window][" << workspace.label << "]\n";
    }
    layouts.reset_all(workspaces);
    assert(layouts.has_pending_request());
    assert(layouts.pending_uses_default());
    for (const auto& workspace : workspaces.descriptors()) {
        assert(!std::filesystem::exists(layouts.layout_path(workspace.id, workspaces)));
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* font_pixels = nullptr;
    int font_width = 0;
    int font_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    assert(font_pixels && font_width > 0 && font_height > 0);
    ImGui::NewFrame();
    ImGui::Begin("WorkspaceLayoutControllerTestHost");
    const ImGuiID dockspace_id = ImGui::GetID("TestDockspace");
    layouts.request_default(EditorWorkspaceId::Animation);
    assert(layouts.apply_pending(dockspace_id, workspaces) ==
        WorkspaceLayoutApplyResult::Defaulted);
    assert(ImGui::DockBuilderGetNode(dockspace_id) != nullptr);
    assert(!layouts.has_pending_request());
    ImGui::End();
    ImGui::EndFrame();
    layouts.persist();
    assert(std::filesystem::is_regular_file(
        layouts.layout_path(EditorWorkspaceId::Animation, workspaces)
    ));
    ImGui::DestroyContext();

    std::filesystem::remove_all(directory, error);
    std::cout << "WorkspaceLayoutControllerTests passed\n";
    return 0;
}
