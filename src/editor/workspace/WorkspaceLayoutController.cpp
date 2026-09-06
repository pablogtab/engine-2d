#include "WorkspaceLayoutController.hpp"

#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace Engine {

void WorkspaceLayoutController::initialize(
    const std::filesystem::path& project_root,
    const WorkspaceManager& workspaces
) {
    const std::filesystem::path normalized_root =
        std::filesystem::absolute(project_root).lexically_normal();
    m_layout_directory = normalized_root / "Library/Editor/Layouts";
    m_current_layout_path = layout_path(workspaces.active(), workspaces);
    m_last_saved_layout.clear();
    m_pending.reset();
    m_dockspace_initialized = false;

    std::error_code error;
    std::filesystem::create_directories(m_layout_directory, error);
    if (error) {
        ENGINE_LOG_WARN(
            "[EDITOR UI] Could not create workspace layout directory '{}': {}",
            m_layout_directory.string(), error.message()
        );
        return;
    }

    if (std::filesystem::exists(m_current_layout_path)) return;

    // Preserve the active layout from builds that predate per-workspace files.
    std::filesystem::path legacy_layout = normalized_root / "Library/Editor/imgui.ini";
    if (!std::filesystem::exists(legacy_layout)) {
        legacy_layout = std::filesystem::current_path() / "imgui.ini";
    }
    if (!std::filesystem::exists(legacy_layout)) return;

    std::filesystem::copy_file(
        legacy_layout,
        m_current_layout_path,
        std::filesystem::copy_options::skip_existing,
        error
    );
    if (error) {
        ENGINE_LOG_WARN(
            "[EDITOR UI] Could not migrate workspace layout '{}': {}",
            legacy_layout.string(), error.message()
        );
    }
}

std::filesystem::path WorkspaceLayoutController::layout_path(
    EditorWorkspaceId id,
    const WorkspaceManager& workspaces
) const {
    const EditorWorkspaceDescriptor* workspace = descriptor(id, workspaces);
    const char* key = workspace ? workspace->key : "2d";
    return m_layout_directory / (std::string(key) + ".ini");
}

const EditorWorkspaceDescriptor* WorkspaceLayoutController::descriptor(
    EditorWorkspaceId id,
    const WorkspaceManager& workspaces
) const {
    const auto& descriptors = workspaces.descriptors();
    const auto found = std::find_if(
        descriptors.begin(), descriptors.end(),
        [id](const EditorWorkspaceDescriptor& candidate) { return candidate.id == id; }
    );
    return found == descriptors.end() ? nullptr : &*found;
}

bool WorkspaceLayoutController::load(
    EditorWorkspaceId id,
    const WorkspaceManager& workspaces
) {
    m_current_layout_path = layout_path(id, workspaces);
    m_last_saved_layout.clear();

    std::error_code error;
    if (!std::filesystem::is_regular_file(m_current_layout_path, error) || error) return false;
    const auto size = std::filesystem::file_size(m_current_layout_path, error);
    if (error || size == 0) return false;

    const std::string layout = FileSystem::read_text(m_current_layout_path.string());
    if (layout.empty() || ImGui::GetCurrentContext() == nullptr) return false;

    ImGui::LoadIniSettingsFromMemory(layout.data(), layout.size());
    m_last_saved_layout = layout;
    const EditorWorkspaceDescriptor* workspace = descriptor(id, workspaces);
    ENGINE_LOG_INFO(
        "[EDITOR UI] Loaded '{}' workspace layout from '{}'.",
        workspace ? workspace->label : "2D",
        m_current_layout_path.string()
    );
    return true;
}

void WorkspaceLayoutController::persist() {
    if (m_pending || m_current_layout_path.empty() || ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    std::size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    if (!data || size == 0) return;
    if (m_last_saved_layout.size() == size &&
        std::equal(data, data + size, m_last_saved_layout.begin())) {
        return;
    }

    const std::string current(data, size);
    if (!FileSystem::write_text_atomically(m_current_layout_path.string(), current)) {
        ENGINE_LOG_ERROR(
            "[EDITOR UI] Failed persisting workspace layout to '{}'.",
            m_current_layout_path.string()
        );
        return;
    }
    m_last_saved_layout = current;
}

void WorkspaceLayoutController::request(EditorWorkspaceId id, bool use_default) {
    if (id == EditorWorkspaceId::Count) return;
    m_pending = PendingLayout{.id = id, .use_default = use_default};
}

void WorkspaceLayoutController::reset_all(const WorkspaceManager& workspaces) {
    persist();
    std::error_code error;
    for (const auto& workspace : workspaces.descriptors()) {
        const std::filesystem::path path = layout_path(workspace.id, workspaces);
        error.clear();
        static_cast<void>(std::filesystem::remove(path, error));
        if (error) {
            ENGINE_LOG_WARN(
                "[EDITOR UI] Could not remove '{}' workspace layout '{}': {}",
                workspace.label, path.string(), error.message()
            );
        }
    }
    m_last_saved_layout.clear();
    request_default(workspaces.active());
    ENGINE_LOG_INFO("[EDITOR UI] Restored all workspace layouts to defaults.");
}

WorkspaceLayoutApplyResult WorkspaceLayoutController::apply_pending(
    std::uint32_t dockspace_id,
    const WorkspaceManager& workspaces
) {
    if (!m_pending) return WorkspaceLayoutApplyResult::None;

    const PendingLayout pending = *m_pending;
    m_pending.reset();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    if (!pending.use_default && load(pending.id, workspaces) &&
        ImGui::DockBuilderGetNode(dockspace_id) != nullptr) {
        return WorkspaceLayoutApplyResult::Loaded;
    }

    build_default(pending.id, dockspace_id, workspaces);
    return WorkspaceLayoutApplyResult::Defaulted;
}

WorkspaceLayoutApplyResult WorkspaceLayoutController::ensure_initialized(
    std::uint32_t dockspace_id,
    const WorkspaceManager& workspaces
) {
    if (m_dockspace_initialized) return WorkspaceLayoutApplyResult::None;
    m_dockspace_initialized = true;
    if (ImGui::DockBuilderGetNode(dockspace_id) != nullptr) {
        return WorkspaceLayoutApplyResult::Loaded;
    }

    build_default(workspaces.active(), dockspace_id, workspaces);
    return WorkspaceLayoutApplyResult::Defaulted;
}

void WorkspaceLayoutController::build_default(
    EditorWorkspaceId id,
    std::uint32_t dockspace_id,
    const WorkspaceManager& workspaces
) {
    m_current_layout_path = layout_path(id, workspaces);
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID dock_main_id = dockspace_id;
    switch (id) {
        case EditorWorkspaceId::TwoD: {
            // Approved authoring layout: compact hierarchy, wide properties,
            // and a lower asset region that preserves viewport focus.
            const ImGuiID dock_left = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Left, 0.18f, nullptr, &dock_main_id
            );
            const ImGuiID dock_right = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Right, 0.39f, nullptr, &dock_main_id
            );
            const ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Down, 0.30f, nullptr, &dock_main_id
            );
            ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left);
            ImGui::DockBuilderDockWindow("Scene Viewport", dock_main_id);
            ImGui::DockBuilderDockWindow("Entity Inspector", dock_right);
            ImGui::DockBuilderDockWindow("AI Assistant", dock_right);
            ImGui::DockBuilderDockWindow("Console & Logs", dock_bottom);
            ImGui::DockBuilderDockWindow("Asset Browser", dock_bottom);
            break;
        }
        case EditorWorkspaceId::Animation: {
            // Animation stays focused on source assets and the studio canvas.
            const ImGuiID dock_left = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Left, 0.21f, nullptr, &dock_main_id
            );
            ImGui::DockBuilderDockWindow("Asset Browser", dock_left);
            ImGui::DockBuilderDockWindow("Animation Studio", dock_main_id);
            break;
        }
        case EditorWorkspaceId::Tilemap: {
            const ImGuiID dock_left = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Left, 0.18f, nullptr, &dock_main_id
            );
            const ImGuiID dock_right = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Right, 0.32f, nullptr, &dock_main_id
            );
            const ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Down, 0.28f, nullptr, &dock_main_id
            );
            ImGui::DockBuilderDockWindow("Scene Hierarchy", dock_left);
            ImGui::DockBuilderDockWindow("Scene Viewport", dock_main_id);
            ImGui::DockBuilderDockWindow("Surface Generator", dock_bottom);
            ImGui::DockBuilderDockWindow("Entity Inspector", dock_right);
            ImGui::DockBuilderDockWindow("Level Composer", dock_right);
            ImGui::DockBuilderDockWindow("Tilemap Palette", dock_bottom);
            ImGui::DockBuilderDockWindow("Asset Browser", dock_bottom);
            break;
        }
        case EditorWorkspaceId::Vfx: {
            const ImGuiID dock_right = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Right, 0.29f, nullptr, &dock_main_id
            );
            ImGuiID dock_right_top = dock_right;
            const ImGuiID dock_right_bottom = ImGui::DockBuilderSplitNode(
                dock_right_top, ImGuiDir_Down, 0.48f, nullptr, &dock_right_top
            );
            const ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Down, 0.28f, nullptr, &dock_main_id
            );
            ImGui::DockBuilderDockWindow("Scene Viewport", dock_main_id);
            ImGui::DockBuilderDockWindow("VFX Preview", dock_main_id);
            ImGui::DockBuilderDockWindow("Trail Preview", dock_main_id);
            ImGui::DockBuilderDockWindow("Entity Inspector", dock_right_top);
            ImGui::DockBuilderDockWindow("Post-Processing Tuner", dock_right_bottom);
            ImGui::DockBuilderDockWindow("Particle Effect Editor", dock_bottom);
            ImGui::DockBuilderDockWindow("Asset Browser", dock_bottom);
            break;
        }
        case EditorWorkspaceId::Debug: {
            const ImGuiID dock_right = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Right, 0.30f, nullptr, &dock_main_id
            );
            ImGuiID dock_right_top = dock_right;
            const ImGuiID dock_right_bottom = ImGui::DockBuilderSplitNode(
                dock_right_top, ImGuiDir_Down, 0.50f, nullptr, &dock_right_top
            );
            const ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Down, 0.34f, nullptr, &dock_main_id
            );
            ImGui::DockBuilderDockWindow("Scene Viewport", dock_main_id);
            ImGui::DockBuilderDockWindow("Console & Logs", dock_bottom);
            ImGui::DockBuilderDockWindow("Engine & Metrics", dock_right_top);
            ImGui::DockBuilderDockWindow("Scene Diagnostics", dock_right_bottom);
            break;
        }
        case EditorWorkspaceId::Count:
            break;
    }

    ImGui::DockBuilderFinish(dockspace_id);
    m_last_saved_layout.clear();
}

} // namespace Engine
