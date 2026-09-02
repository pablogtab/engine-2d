#pragma once

#include "WorkspaceManager.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace Engine {

enum class WorkspaceLayoutApplyResult {
    None,
    Loaded,
    Defaulted
};

class WorkspaceLayoutController {
public:
    void initialize(
        const std::filesystem::path& project_root,
        const WorkspaceManager& workspaces
    );

    [[nodiscard]] std::filesystem::path layout_path(
        EditorWorkspaceId id,
        const WorkspaceManager& workspaces
    ) const;
    [[nodiscard]] bool load(EditorWorkspaceId id, const WorkspaceManager& workspaces);
    void persist();

    void request(EditorWorkspaceId id, bool use_default = false);
    void request_default(EditorWorkspaceId id) { request(id, true); }
    void reset_all(const WorkspaceManager& workspaces);

    [[nodiscard]] WorkspaceLayoutApplyResult apply_pending(
        std::uint32_t dockspace_id,
        const WorkspaceManager& workspaces
    );
    [[nodiscard]] WorkspaceLayoutApplyResult ensure_initialized(
        std::uint32_t dockspace_id,
        const WorkspaceManager& workspaces
    );

    [[nodiscard]] bool has_pending_request() const { return m_pending.has_value(); }
    [[nodiscard]] bool pending_uses_default() const {
        return m_pending && m_pending->use_default;
    }

private:
    struct PendingLayout {
        EditorWorkspaceId id{EditorWorkspaceId::TwoD};
        bool use_default{false};
    };

    [[nodiscard]] const EditorWorkspaceDescriptor* descriptor(
        EditorWorkspaceId id,
        const WorkspaceManager& workspaces
    ) const;
    void build_default(
        EditorWorkspaceId id,
        std::uint32_t dockspace_id,
        const WorkspaceManager& workspaces
    );

    std::filesystem::path m_layout_directory{"Library/Editor/Layouts"};
    std::filesystem::path m_current_layout_path;
    std::string m_last_saved_layout;
    std::optional<PendingLayout> m_pending;
    bool m_dockspace_initialized{false};
};

} // namespace Engine
