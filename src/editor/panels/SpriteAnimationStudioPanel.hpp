#pragma once

#include "../../ecs/components/AnimationComponents.hpp"

#include "../EditorPanel.hpp"
#include "../../scene/SceneManager.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EditorPropertyTransaction.hpp"
#include <imgui.h>
#include <memory>
#include <string>

namespace Engine {

class SpriteAnimationStudioPanel : public EditorPanel {
public:
    SpriteAnimationStudioPanel(
        SceneManager* scene_manager,
        std::shared_ptr<EditorSelection> selection
    );
    ~SpriteAnimationStudioPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void open_for_entity(EntityID entity) {
        if (m_target_entity != entity) m_preview_entity = 0;
        m_target_entity = entity;
        is_open = true;
    }
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) {
        m_command_history = std::move(command_history);
    }

private:
    void render_top_bar(EntityManager& em, EntityID entity);
    void render_preview_viewport(EntityManager& em, EntityID entity, float avail_width);
    void render_spritesheet_canvas(EntityManager& em, EntityID entity);
    void render_timeline(EntityManager& em, EntityID entity);
    void render_clip_tabs(EntityManager& em, EntityID entity);
    void reset_preview(EntityID entity, const AnimationComponent& animation, int clip_index = -1);
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
    EditorPropertyTransaction m_property_transactions;
    EntityID m_target_entity{0};

    float m_sheet_zoom{1.5f};
    float m_preview_zoom{2.0f};
    bool m_slicer_mode_freeform{false};

    // Freeform drag selection
    bool m_is_dragging_box{false};
    ImVec2 m_drag_start_uv{0, 0};
    ImVec2 m_drag_end_uv{0, 0};

    // Frame step timer for preview
    float m_preview_timer{0.0f};
    EntityID m_preview_entity{0};
    int m_preview_clip_index{0};
    int m_preview_frame{0};
    bool m_preview_playing{false};
    bool m_preview_finished{false};
    int m_timeline_selected_frame{-1};

    std::string m_name{"Animation Studio"};
};

} // namespace Engine
