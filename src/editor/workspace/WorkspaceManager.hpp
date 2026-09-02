#pragma once

#include "../EditorPanel.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Engine {

enum class EditorWorkspaceId {
    TwoD,
    Animation,
    Tilemap,
    Vfx,
    Debug,
    Count
};

struct EditorWorkspaceDescriptor {
    EditorWorkspaceId id{EditorWorkspaceId::TwoD};
    const char* key{"2d"};
    const char* label{"2D"};
    std::vector<EditorPanelId> panels;
};

class WorkspaceManager {
public:
    WorkspaceManager();

    [[nodiscard]] EditorWorkspaceId active() const { return m_active; }
    [[nodiscard]] const EditorWorkspaceDescriptor& active_descriptor() const;
    [[nodiscard]] const std::array<EditorWorkspaceDescriptor,
        static_cast<std::size_t>(EditorWorkspaceId::Count)>& descriptors() const {
        return m_descriptors;
    }

    [[nodiscard]] bool set_active(EditorWorkspaceId id);
    [[nodiscard]] bool contains(EditorPanelId panel) const;
    [[nodiscard]] bool load(const std::string& path);
    [[nodiscard]] bool save(const std::string& path) const;

private:
    std::array<EditorWorkspaceDescriptor,
        static_cast<std::size_t>(EditorWorkspaceId::Count)> m_descriptors;
    EditorWorkspaceId m_active{EditorWorkspaceId::TwoD};
};

} // namespace Engine
