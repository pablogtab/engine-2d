#pragma once

#include <string>
#include <memory>

namespace Engine {

class EditorCommandHistory;
class EditorEntityStateStore;
class EditorSelection;

struct EditorPanelContext {
    std::shared_ptr<EditorCommandHistory> command_history;
    std::shared_ptr<EditorEntityStateStore> entity_state_store;
    std::shared_ptr<EditorSelection> selection;
};

enum class EditorPanelId {
    Unknown,
    Viewport,
    Hierarchy,
    Inspector,
    Console,
    Assets,
    Metrics,
    Diagnostics,
    PostProcess,
    Particles,
    ProjectSettings,
    Shortcuts,
    Tilemap,
    Animation,
    Assistant,
    UiGallery
};

[[nodiscard]] constexpr const char* editor_panel_id_key(EditorPanelId id) {
    switch (id) {
        case EditorPanelId::Viewport: return "viewport";
        case EditorPanelId::Hierarchy: return "hierarchy";
        case EditorPanelId::Inspector: return "inspector";
        case EditorPanelId::Console: return "console";
        case EditorPanelId::Assets: return "assets";
        case EditorPanelId::Metrics: return "metrics";
        case EditorPanelId::Diagnostics: return "diagnostics";
        case EditorPanelId::PostProcess: return "post_process";
        case EditorPanelId::Particles: return "particles";
        case EditorPanelId::ProjectSettings: return "project_settings";
        case EditorPanelId::Shortcuts: return "shortcuts";
        case EditorPanelId::Tilemap: return "tilemap";
        case EditorPanelId::Animation: return "animation";
        case EditorPanelId::Assistant: return "assistant";
        case EditorPanelId::UiGallery: return "ui_gallery";
        case EditorPanelId::Unknown: break;
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// EditorPanel
// Base abstract class for all modular editor panels (Hierarchy, Inspector,
// Viewport, Console, Profiler, Exporter, etc.)
// ---------------------------------------------------------------------------
class EditorPanel {
public:
    virtual ~EditorPanel() = default;

    virtual void on_gui() = 0;
    virtual void on_update(float /*delta_time*/) {}
    virtual void on_render_resources_invalidated() {}
    virtual void on_context_changed(const EditorPanelContext& /*context*/) {}
    [[nodiscard]] virtual const std::string& get_name() const = 0;

    [[nodiscard]] EditorPanelId get_id() const { return m_id; }
    void set_id(EditorPanelId id) { m_id = id; }

    bool is_open{true};

private:
    EditorPanelId m_id{EditorPanelId::Unknown};
};

} // namespace Engine
