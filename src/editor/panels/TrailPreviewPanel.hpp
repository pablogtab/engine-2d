#pragma once

#include "../EditorPanel.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../renderer/TrailSystem2D.hpp"
#include "../../scene/Camera2D.hpp"

#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

class IMaterialBackend;
class Renderer2D;

class TrailPreviewPanel final : public EditorPanel {
public:
    TrailPreviewPanel(
        SDL_Renderer* renderer,
        std::shared_ptr<IMaterialBackend> material_backend
    );
    ~TrailPreviewPanel() override;

    void on_gui() override;
    void on_update(float delta_time) override;
    void on_render_resources_invalidated() override;
    void on_context_changed(const EditorPanelContext&) override { clear_target(); }
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void open_for_entity(EntityManager* entities, EntityID entity);
    [[nodiscard]] bool targets(const EntityManager* entities, EntityID entity) const;

private:
    enum class MotionMode {
        Straight,
        FollowCursor,
        FigureEight
    };

    struct VisualClone {
        EntityID source{0};
        std::uint64_t source_generation{0};
        EntityID preview{0};
    };

    [[nodiscard]] bool target_is_valid() const;
    [[nodiscard]] std::vector<EntityID> collect_visual_sources() const;
    void rebuild_preview();
    void sync_preview();
    void clear_target();
    void restart_preview();
    void destroy_render_resources();
    void render_toolbar();
    void render_canvas();
    void render_to_target();

    std::string m_name{"Trail Preview"};
    SDL_Renderer* m_renderer{nullptr};
    std::shared_ptr<IMaterialBackend> m_material_backend;
    std::unique_ptr<Renderer2D> m_preview_renderer;
    SDL_Texture* m_preview_target{nullptr};
    Camera2D m_preview_camera{640.0f, 360.0f};

    EntityManager* m_source_entities{nullptr};
    EntityID m_source_root{0};
    std::uint64_t m_source_generation{0};
    EntityManager m_preview_entities;
    EntityID m_preview_root{0};
    std::vector<VisualClone> m_visual_clones;
    TrailSystem2D m_trail_system;

    MotionMode m_motion_mode{MotionMode::FigureEight};
    bool m_playing{true};
    bool m_show_sprite{true};
    bool m_show_trail{true};
    bool m_orient_to_path{true};
    bool m_focus_requested{false};
    bool m_cursor_hovered{false};
    float m_elapsed{0.0f};
    float m_motion_speed{1.0f};
    float m_zoom{1.0f};
    glm::vec2 m_camera_position{0.0f};
    glm::vec2 m_emitter_position{0.0f};
    glm::vec2 m_previous_emitter_position{0.0f};
    glm::vec2 m_cursor_position{0.0f};
    float m_asset_rotation{0.0f};
    SDL_Color m_background{17, 20, 27, 255};
};

} // namespace Engine
