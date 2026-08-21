#pragma once

#include "../renderer/RenderView.hpp"
#include "../ecs/EntityManager.hpp"
#include "../events/EventDispatcher.hpp"
#include "../core/AssetHandle.hpp"
#include "../renderer/RenderLayerEffectProfile.hpp"
#include <SDL3/SDL.h>
#include <vector>

namespace Engine {

class Renderer2D;
class PostProcessPipeline;
struct PostProcessProfile;
class AssetDatabase;

class Scene {
public:
    virtual ~Scene() = default;

    virtual void on_attach() {}
    virtual void on_detach() {}
    virtual void on_update(float delta_time) {}
    virtual void on_render(Renderer2D& renderer) {}
    virtual void on_viewport_resize(RenderViewKind view, const RenderViewConfiguration& configuration) {}

    [[nodiscard]] virtual SDL_Color get_clear_color() const { return SDL_Color{24, 28, 36, 255}; }

    [[nodiscard]] virtual PostProcessPipeline* get_post_process_pipeline() { return nullptr; }
    [[nodiscard]] virtual PostProcessProfile* get_editable_post_process_profile() { return nullptr; }
    [[nodiscard]] virtual bool supports_post_process_profile_assets() const { return false; }
    [[nodiscard]] virtual AssetHandle get_post_process_profile_handle() const { return {}; }
    virtual bool set_post_process_profile_handle(AssetHandle, const AssetDatabase&) { return false; }
    [[nodiscard]] virtual bool supports_render_layer_effects() const { return false; }
    [[nodiscard]] virtual const std::vector<RenderLayerEffectAssignment>& get_render_layer_effect_assignments() const {
        static const std::vector<RenderLayerEffectAssignment> empty;
        return empty;
    }
    virtual bool set_render_layer_effect_assignments(std::vector<RenderLayerEffectAssignment>, const AssetDatabase&) { return false; }
    virtual bool refresh_render_layer_effects(const AssetDatabase&) { return false; }

    virtual void set_is_editing(bool is_editing) {}
    [[nodiscard]] virtual bool is_editing() const { return true; }

    const std::string& get_name() const { return m_name; }

    EntityManager& get_entity_manager() { return m_entity_manager; }
    const EntityManager& get_entity_manager() const { return m_entity_manager; }
    void swap_entity_manager(EntityManager& replacement) noexcept { m_entity_manager.swap(replacement); }
    EventDispatcher& get_event_dispatcher() { return m_event_dispatcher; }

protected:
    explicit Scene(std::string name) : m_name(std::move(name)) {}

    EntityManager m_entity_manager;
    EventDispatcher m_event_dispatcher;

private:
    std::string m_name;
};

} // namespace Engine
