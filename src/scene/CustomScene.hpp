#pragma once

#include "Scene.hpp"
#include "../renderer/RenderView.hpp"
#include <SDL3/SDL.h>

namespace Engine {

class AssetDatabase;
class Camera2D;
class IEntityRenderFilter;
class PostProcessPipeline;
class PostProcessProfile;
class Renderer2D;

// Explicit escape hatch for isolated engine validation scenes. Project scenes
// use Scene plus the contexts composed by SceneManager.
class CustomScene : public Scene {
public:
    using Scene::Scene;

    virtual void on_attach() {}
    virtual void on_detach() {}
    virtual void on_update(float) {}
    virtual void on_render(Renderer2D&) {}
    virtual void on_viewport_resize(RenderViewKind, const RenderViewConfiguration&) {}
    virtual void on_render_resources_invalidated() {}
    [[nodiscard]] virtual Camera2D* get_render_camera(RenderViewKind) { return nullptr; }
    [[nodiscard]] virtual const Camera2D* get_render_camera(RenderViewKind) const { return nullptr; }
    virtual void set_render_filter(const IEntityRenderFilter*) {}
    [[nodiscard]] virtual SDL_Color get_clear_color() const { return {24, 28, 36, 255}; }
    [[nodiscard]] virtual PostProcessPipeline* get_post_process_pipeline() { return nullptr; }
    [[nodiscard]] virtual PostProcessProfile* get_editable_post_process_profile() { return nullptr; }
    virtual bool set_post_process_profile_handle(AssetHandle handle, const AssetDatabase&) {
        Scene::set_post_process_profile_handle(handle);
        return true;
    }
    virtual bool set_render_layer_effect_assignments(
        std::vector<RenderLayerEffectAssignment> assignments, const AssetDatabase&
    ) {
        Scene::set_render_layer_effect_assignments(std::move(assignments));
        return true;
    }
    [[nodiscard]] virtual const std::vector<RenderLayerEffectAssignment>& get_render_layer_effect_assignments() const {
        return Scene::get_render_layer_effect_assignments();
    }
    virtual bool refresh_render_layer_effects(const AssetDatabase&) { return false; }
    virtual void set_is_editing(bool editing) { m_is_editing = editing; }
    [[nodiscard]] virtual bool is_editing() const { return m_is_editing; }

private:
    bool m_is_editing{true};
};

} // namespace Engine
