#pragma once

#include "../../ecs/components/AnimationComponents.hpp"

#include "../EditorPanel.hpp"
#include "../../scene/SceneManager.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include <imgui.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Engine {

class SpriteAnimationStudioPanel : public EditorPanel {
public:
    SpriteAnimationStudioPanel(
        SceneManager* scene_manager,
        std::shared_ptr<EditorSelection> selection
    );
    ~SpriteAnimationStudioPanel() override = default;

    void on_gui() override;
    void on_context_changed(const EditorPanelContext& context) override {
        set_command_history(context.command_history);
    }
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void open_for_entity(EntityID entity);
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) {
        m_command_history = std::move(command_history);
    }
    [[nodiscard]] bool owns_draft_history_focus() const;
    [[nodiscard]] bool can_undo_draft() const;
    [[nodiscard]] bool can_redo_draft() const;
    void undo_from_editor_action();
    void redo_from_editor_action();

private:
    void render_top_bar(EntityManager& em, EntityID entity);
    void render_preview_viewport(EntityManager& em, EntityID entity);
    void render_animation_settings(EntityManager& em, EntityID entity);
    void render_spritesheet_canvas(
        EntityManager& em,
        EntityID entity,
        float available_height
    );
    void render_timeline(EntityManager& em, EntityID entity);
    void render_clip_tabs(EntityManager& em, EntityID entity);
    void reset_preview(EntityID entity, const AnimationComponent& animation, int clip_index = -1);
    void ensure_draft(EntityManager& em, EntityID entity);
    void clear_draft();
    void save_draft(EntityManager& em, EntityID entity);
    void discard_draft();
    [[nodiscard]] AnimationComponent& draft_animation(EntityManager& em, EntityID entity);
    [[nodiscard]] std::optional<std::string> validate_draft(const class Texture* texture) const;
    void refresh_draft_dirty();
    void record_draft_edit(const AnimationComponent& before);
    void commit_pending_history();
    void commit_nudge_edit();
    void undo_draft_edit();
    void redo_draft_edit();
    void track_edit(
        EntityManager& em,
        EntityID entity,
        const AnimationComponent& before,
        const std::string& property,
        const char* description,
        bool force_commit = false
    );

    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    EntityID m_target_entity{0};
    EntityID m_pending_target_entity{0};

    EntityManager* m_draft_manager{nullptr};
    EntityID m_draft_entity{0};
    std::optional<AnimationComponent> m_saved_animation;
    AnimationComponent m_draft_animation;
    bool m_draft_dirty{false};
    std::vector<AnimationComponent> m_draft_undo;
    std::vector<AnimationComponent> m_draft_redo;
    std::optional<AnimationComponent> m_pending_history_snapshot;
    std::optional<AnimationComponent> m_nudge_history_snapshot;
    double m_nudge_last_input_time{0.0};

    float m_sheet_zoom{1.5f};
    bool m_sheet_zoom_fit_pending{true};
    float m_preview_zoom{1.0f};
    bool m_slicer_mode_freeform{false};
    std::optional<SDL_FRect> m_freeform_clipboard;
    bool m_fixed_draw_size{false};
    ImVec2 m_fixed_draw_dimensions{0.0f, 0.0f};
    bool m_show_center_guides{true};

    enum class FreeformTool { Select, Draw };
    enum class FreeformDrag { None, Create, Move, Resize };
    FreeformTool m_freeform_tool{FreeformTool::Select};
    FreeformDrag m_freeform_drag{FreeformDrag::None};
    int m_selected_custom_frame{-1};
    ImVec2 m_drag_start_uv{0, 0};
    ImVec2 m_drag_end_uv{0, 0};
    SDL_FRect m_drag_origin_rect{0, 0, 0, 0};
    std::optional<AnimationComponent> m_freeform_drag_before;
    ImVec2 m_preview_pan{0, 0};

    // Frame step timer for preview
    float m_preview_timer{0.0f};
    EntityID m_preview_entity{0};
    int m_preview_clip_index{0};
    int m_preview_frame{0};
    int m_preview_direction{1};
    bool m_preview_playing{false};
    bool m_preview_finished{false};
    bool m_has_keyboard_focus{false};
    std::string m_name{"Animation Studio"};
};

} // namespace Engine
