#include "SceneRenderer.hpp"
#include "../core/AssetDatabase.hpp"
#include "../ecs/RenderSystem.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../renderer/postprocess/PostProcessPasses.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include <algorithm>

namespace Engine {

namespace {

bool same_assignments(
    const std::vector<RenderLayerEffectAssignment>& lhs,
    const std::vector<RenderLayerEffectAssignment>& rhs
) {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].profile_handle != rhs[index].profile_handle ||
            lhs[index].render_layer != rhs[index].render_layer ||
            lhs[index].enabled != rhs[index].enabled) return false;
    }
    return true;
}

const CameraComponent* get_primary_camera(Scene& scene) {
    auto& em = scene.get_entity_manager();
    const CameraComponent* primary = nullptr;
    em.for_each<CameraComponent>([&](EntityID, CameraComponent& cam) {
        if (!primary || cam.is_primary) {
            primary = &cam;
        }
    });
    return primary;
}

} // namespace

bool SceneRenderer::synchronize_post_process(Scene& scene, const AssetDatabase& database) {
    const CameraComponent* camera = get_primary_camera(scene);
    const AssetHandle handle = (camera && camera->post_process.enabled) ? camera->post_process.global_profile_handle : AssetHandle{};
    if (handle == m_loaded_post_process_handle) return !handle.is_valid() || m_post_process_profile.has_value();
    if (!handle.is_valid()) {
        m_loaded_post_process_handle = {};
        m_post_process_profile.reset();
        m_post_process_pipeline.destroy_targets();
        m_post_process_initialized = false;
        return true;
    }
    const auto loaded = PostProcessProfile::load_asset(handle, database);
    if (!loaded) return false;
    m_loaded_post_process_handle = handle;
    m_post_process_profile = *loaded;
    if (m_post_process_initialized) m_post_process_profile->apply_to_pipeline(m_post_process_pipeline);
    return true;
}

bool SceneRenderer::synchronize_render_layer_effects(Scene& scene, const AssetDatabase& database) {
    const CameraComponent* camera = get_primary_camera(scene);
    std::vector<RenderLayerEffectAssignment> assignments;
    if (camera && camera->post_process.enabled) {
        for (const auto& effect : camera->post_process.layer_effects) {
            assignments.push_back({effect.effect_handle, effect.render_layer, effect.enabled});
        }
    }
    if (same_assignments(assignments, m_loaded_layer_assignments)) return true;
    if (!m_render_effect_system.set_layer_assignments(assignments, database)) return false;
    m_loaded_layer_assignments = assignments;
    return true;
}

void SceneRenderer::initialize_post_process(Renderer2D& renderer, const Camera2D& camera) {
    if (!m_post_process_profile || m_post_process_initialized) return;
    const glm::vec2 viewport = camera.get_viewport_size();
    m_post_process_pipeline.init(
        renderer.get_sdl_renderer(),
        std::max(1, static_cast<int>(viewport.x)),
        std::max(1, static_cast<int>(viewport.y))
    );
    m_post_process_pipeline.add_pass(std::make_shared<VignettePass>());
    m_post_process_pipeline.add_pass(std::make_shared<ChromaticAberrationPass>());
    m_post_process_pipeline.add_pass(std::make_shared<BloomPass>());
    m_post_process_pipeline.add_pass(std::make_shared<GaussianBlurPass>());
    m_post_process_pipeline.add_pass(std::make_shared<ColorGradingPass>());
    m_post_process_pipeline.add_pass(std::make_shared<FilmGrainPass>());
    m_post_process_pipeline.add_pass(std::make_shared<ScanlinesCRTPass>());
    m_post_process_profile->apply_to_pipeline(m_post_process_pipeline);
    m_post_process_initialized = true;
}

void SceneRenderer::render(
    Scene& scene,
    SceneRuntime& runtime,
    EditorSceneView& editor_view,
    Renderer2D& renderer,
    bool editing,
    const AssetDatabase& database
) {
    static_cast<void>(synchronize_post_process(scene, database));
    static_cast<void>(synchronize_render_layer_effects(scene, database));
    Camera2D& camera = editing ? editor_view.camera() : runtime.game_camera();
    initialize_post_process(renderer, camera);
    renderer.set_post_process_pipeline(m_post_process_profile ? &m_post_process_pipeline : nullptr);
    m_render_effect_system.configure(scene.get_entity_manager(), renderer, database);
    m_lighting_system.configure(scene.get_entity_manager(), renderer);
    renderer.begin_scene(camera);
    RenderSystem::render(
        scene.get_entity_manager(),
        renderer,
        editing ? editor_view.render_filter() : nullptr
    );
    if (!editing) {
        runtime.particle_world().render(renderer, scene.get_entity_manager());
    }
    if (editing) {
        m_canvas_ui_system.render_editor_preview(
            scene.get_entity_manager(), renderer, database, camera,
            editor_view.safe_area(), editor_view.show_safe_area_overlay(), editor_view.render_filter()
        );
    } else {
        m_canvas_ui_system.render(
            scene.get_entity_manager(), renderer, database,
            runtime.ui_output_size(), runtime.ui_safe_area(), &runtime.ui_input(), nullptr
        );
    }
    renderer.end_scene();
    renderer.set_post_process_pipeline(nullptr);
}

void SceneRenderer::configure_view(RenderViewKind, const RenderViewConfiguration& configuration) {
    if (m_post_process_initialized) {
        m_post_process_pipeline.resize(configuration.output_width, configuration.output_height);
    }
}

void SceneRenderer::invalidate_resources() {
    m_post_process_pipeline.destroy_targets();
    m_canvas_ui_system.clear();
    m_post_process_initialized = false;
}

void SceneRenderer::detach() {
    invalidate_resources();
}

PostProcessPipeline* SceneRenderer::post_process_pipeline(Scene& scene, const AssetDatabase& database) {
    return synchronize_post_process(scene, database) && m_post_process_profile
        ? &m_post_process_pipeline : nullptr;
}

PostProcessProfile* SceneRenderer::editable_post_process_profile(Scene& scene, const AssetDatabase& database) {
    return synchronize_post_process(scene, database) && m_post_process_profile
        ? &*m_post_process_profile : nullptr;
}

bool SceneRenderer::set_post_process_profile(
    Scene& scene, AssetHandle handle, const AssetDatabase& database
) {
    if (handle.is_valid() && !PostProcessProfile::load_asset(handle, database)) return false;
    auto& em = scene.get_entity_manager();
    em.for_each<CameraComponent>([&](EntityID, CameraComponent& cam) {
        if (cam.is_primary) {
            cam.post_process.global_profile_handle = handle;
        }
    });
    m_loaded_post_process_handle = {};
    return synchronize_post_process(scene, database);
}

bool SceneRenderer::set_render_layer_effects(
    Scene& scene,
    std::vector<RenderLayerEffectAssignment> assignments,
    const AssetDatabase& database
) {
    if (!m_render_effect_system.set_layer_assignments(assignments, database)) return false;
    auto& em = scene.get_entity_manager();
    em.for_each<CameraComponent>([&](EntityID, CameraComponent& cam) {
        if (cam.is_primary) {
            cam.post_process.layer_effects.clear();
            for (const auto& item : assignments) {
                cam.post_process.layer_effects.push_back({item.profile_handle, item.render_layer, item.enabled});
            }
        }
    });
    m_loaded_layer_assignments = std::move(assignments);
    return true;
}

bool SceneRenderer::refresh_render_layer_effects(Scene& scene, const AssetDatabase& database) {
    const CameraComponent* camera = get_primary_camera(scene);
    std::vector<RenderLayerEffectAssignment> assignments;
    if (camera && camera->post_process.enabled) {
        for (const auto& effect : camera->post_process.layer_effects) {
            assignments.push_back({effect.effect_handle, effect.render_layer, effect.enabled});
        }
    }
    if (!m_render_effect_system.set_layer_assignments(assignments, database)) return false;
    m_loaded_layer_assignments = std::move(assignments);
    return m_render_effect_system.refresh(database);
}

} // namespace Engine
