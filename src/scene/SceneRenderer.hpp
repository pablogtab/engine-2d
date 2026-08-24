#pragma once

#include "EditorSceneView.hpp"
#include "SceneRuntime.hpp"
#include "../renderer/Lighting2DSystem.hpp"
#include "../renderer/RenderEffectSystem.hpp"
#include "../renderer/postprocess/PostProcessPipeline.hpp"
#include "../renderer/postprocess/PostProcessProfile.hpp"
#include "../ui/CanvasUISystem.hpp"
#include <optional>

namespace Engine {

class AssetDatabase;
class Renderer2D;

class SceneRenderer {
public:
    void render(
        Scene& scene,
        SceneRuntime& runtime,
        EditorSceneView& editor_view,
        Renderer2D& renderer,
        bool editing,
        const AssetDatabase& database
    );
    void configure_view(RenderViewKind view, const RenderViewConfiguration& configuration);
    void invalidate_resources();
    void detach();

    [[nodiscard]] PostProcessPipeline* post_process_pipeline(Scene& scene, const AssetDatabase& database);
    [[nodiscard]] PostProcessProfile* editable_post_process_profile(Scene& scene, const AssetDatabase& database);
    [[nodiscard]] bool set_post_process_profile(Scene& scene, AssetHandle handle, const AssetDatabase& database);
    [[nodiscard]] bool set_render_layer_effects(
        Scene& scene,
        std::vector<RenderLayerEffectAssignment> assignments,
        const AssetDatabase& database
    );
    [[nodiscard]] bool refresh_render_layer_effects(Scene& scene, const AssetDatabase& database);

private:
    bool synchronize_post_process(Scene& scene, const AssetDatabase& database);
    bool synchronize_render_layer_effects(Scene& scene, const AssetDatabase& database);
    void initialize_post_process(Renderer2D& renderer, const Camera2D& camera);

    PostProcessPipeline m_post_process_pipeline;
    std::optional<PostProcessProfile> m_post_process_profile;
    AssetHandle m_loaded_post_process_handle{};
    RenderEffectSystem m_render_effect_system;
    std::vector<RenderLayerEffectAssignment> m_loaded_layer_assignments;
    Lighting2DSystem m_lighting_system;
    CanvasUISystem m_canvas_ui_system;
    bool m_post_process_initialized{false};
};

} // namespace Engine
