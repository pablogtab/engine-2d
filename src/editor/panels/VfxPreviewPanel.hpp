#pragma once

#include "../EditorPanel.hpp"
#include "../../core/AssetHandle.hpp"
#include "../../ecs/components/ScriptingComponents.hpp"
#include "../../particles/ParticleWorld.hpp"
#include "../../renderer/TrailSystem2D.hpp"
#include "../../renderer/material/MaterialAsset.hpp"
#include "../../renderer/material/ShaderAsset.hpp"
#include "../../scene/Camera2D.hpp"
#include "../../scripting/ScriptRuntime.hpp"

#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

class AssetDatabase;
class IMaterialBackend;
class Renderer2D;
class Scene;
class SceneManager;

class VfxPreviewPanel final : public EditorPanel {
public:
    VfxPreviewPanel(
        SDL_Renderer* renderer,
        AssetDatabase* asset_database,
        SceneManager* scene_manager,
        std::shared_ptr<IMaterialBackend> material_backend
    );
    ~VfxPreviewPanel() override;

    void on_gui() override;
    void on_update(float delta_time) override;
    void on_render_resources_invalidated() override;
    void on_visibility_changed(bool visible) override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void open_asset(AssetHandle handle);

private:
    struct PreviewMaterialDraft {
        MaterialAssetDescription material;
        ShaderAssetDescription shader;
    };

    static constexpr int PREVIEW_WIDTH = 800;
    static constexpr int PREVIEW_HEIGHT = 450;
    static constexpr float FIXED_STEP = 1.0f / 60.0f;

    void stop_preview();
    bool rebuild_preview(std::optional<float> seek_time = std::nullopt);
    void finish_startup(const ScriptRuntimeStartResult& result);
    void restart_preview();
    void seek_to(float target_time);
    void simulate(float delta_time);
    void simulate_step(float delta_time);
    void fit_preview();
    void render_toolbar();
    void render_timeline();
    void render_canvas();
    void render_parameters();
    void render_to_target();
    void destroy_render_resources();
    void reset_parameters();
    void save_parameters();
    void collect_material_parameters();
    void apply_material_parameters();
    void apply_script_parameters(
        const std::unordered_map<EntityID, EntityID>& local_to_runtime
    );
    [[nodiscard]] static std::string script_parameter_key(
        EntityID local_entity,
        ScriptSlotID slot_id,
        const std::string& field_name
    );
    [[nodiscard]] EntityID instantiate_prefab(
        AssetHandle handle,
        const glm::vec2& position
    );
    [[nodiscard]] bool preview_blocked_by_play() const;
    [[nodiscard]] bool has_live_content() const;

    std::string m_name{"VFX Preview"};
    SDL_Renderer* m_renderer{nullptr};
    AssetDatabase* m_asset_database{nullptr};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<IMaterialBackend> m_material_backend;
    std::unique_ptr<Renderer2D> m_preview_renderer;
    SDL_Texture* m_preview_target{nullptr};
    Camera2D m_preview_camera{PREVIEW_WIDTH, PREVIEW_HEIGHT};

    AssetHandle m_asset_handle{};
    std::string m_asset_name;
    std::unique_ptr<Scene> m_scene;
    EntityID m_root_entity{0};
    ScriptRuntime m_script_runtime;
    ParticleWorld m_particle_world;
    TrailSystem2D m_trail_system;

    bool m_playing{true};
    bool m_loop{true};
    bool m_starting{false};
    bool m_suspended_for_play{false};
    bool m_focus_requested{false};
    float m_elapsed{0.0f};
    float m_duration{2.0f};
    float m_speed{1.0f};
    float m_timeline_cursor{0.0f};
    float m_last_step_delta{0.0f};
    std::uint64_t m_frame_index{0};
    std::uint32_t m_particle_seed{1337};
    std::optional<float> m_pending_seek;
    std::size_t m_script_count{0};
    std::size_t m_script_failures{0};
    std::string m_error;

    std::unordered_map<EntityID, EntityID> m_local_to_runtime;
    std::unordered_map<std::string, ScriptFieldValue> m_script_parameter_overrides;
    std::unordered_map<AssetHandle, PreviewMaterialDraft> m_material_parameter_drafts;
    std::unordered_set<AssetHandle> m_modified_materials;
    std::string m_save_feedback;
    bool m_save_feedback_is_error{false};

    float m_zoom{1.0f};
    glm::vec2 m_camera_position{0.0f};
    SDL_Color m_background{17, 20, 27, 255};
};

} // namespace Engine
