#pragma once

#include "../EditorPanel.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Engine {

class EditorPanelRegistry {
public:
    bool add(EditorPanelId id, std::shared_ptr<EditorPanel> panel,
             const EditorPanelContext& context = {});
    bool remove(const std::string& name);

    [[nodiscard]] std::shared_ptr<EditorPanel> find(const std::string& name) const;
    [[nodiscard]] std::shared_ptr<EditorPanel> find(EditorPanelId id) const;
    [[nodiscard]] const std::vector<std::shared_ptr<EditorPanel>>& panels() const {
        return m_panels;
    }

    void broadcast_context(const EditorPanelContext& context);
    void invalidate_render_resources();

private:
    std::vector<std::shared_ptr<EditorPanel>> m_panels;
};

} // namespace Engine
