#include "PostProcessProfileCommands.hpp"
#include "../../renderer/postprocess/PostProcessPipeline.hpp"
#include "../../scene/Scene.hpp"
#include "../../core/AssetDatabase.hpp"
#include <utility>

namespace Engine {

ModifyPostProcessProfileCommand::ModifyPostProcessProfileCommand(
    PostProcessProfile* target,
    PostProcessPipeline* pipeline,
    PostProcessProfile before,
    PostProcessProfile after
)
    : m_target(target),
      m_pipeline(pipeline),
      m_before(std::move(before)),
      m_after(std::move(after)) {}

bool ModifyPostProcessProfileCommand::apply(const PostProcessProfile& value) {
    if (!m_target || !m_pipeline) return false;
    *m_target = value;
    m_target->apply_to_pipeline(*m_pipeline);
    return true;
}

bool ModifyPostProcessProfileCommand::execute() {
    return apply(m_after);
}

bool ModifyPostProcessProfileCommand::undo() {
    return apply(m_before);
}

SetSceneRenderLayerEffectsCommand::SetSceneRenderLayerEffectsCommand(
    Scene* scene, AssetDatabase* database,
    std::vector<RenderLayerEffectAssignment> before, std::vector<RenderLayerEffectAssignment> after
) : m_scene(scene), m_database(database), m_before(std::move(before)), m_after(std::move(after)) {}

bool SetSceneRenderLayerEffectsCommand::apply(const std::vector<RenderLayerEffectAssignment>& assignments) {
    return m_scene && m_database && m_scene->set_render_layer_effect_assignments(assignments, *m_database);
}
bool SetSceneRenderLayerEffectsCommand::execute() { return apply(m_after); }
bool SetSceneRenderLayerEffectsCommand::undo() { return apply(m_before); }

ModifyRenderLayerEffectProfileCommand::ModifyRenderLayerEffectProfileCommand(
    Scene* scene, AssetDatabase* database, AssetHandle handle,
    RenderLayerEffectProfile before, RenderLayerEffectProfile after
) : m_scene(scene), m_database(database), m_handle(handle), m_before(std::move(before)), m_after(std::move(after)) {}

bool ModifyRenderLayerEffectProfileCommand::apply(
    const RenderLayerEffectProfile& profile, const RenderLayerEffectProfile& rollback
) {
    if (!m_database || !m_handle.is_valid()) return false;
    const AssetRecord* record = m_database->find(m_handle);
    if (!record || record->type != AssetType::RenderLayerEffectProfile) return false;
    const std::string asset_path = (m_database->project_root() / record->source_path).string();
    std::string error;
    if (!profile.save_asset(asset_path, *m_database, &error)) return false;
    if (!m_scene || m_scene->refresh_render_layer_effects(*m_database)) return true;
    static_cast<void>(rollback.save_asset(asset_path, *m_database));
    static_cast<void>(m_scene->refresh_render_layer_effects(*m_database));
    return false;
}
bool ModifyRenderLayerEffectProfileCommand::execute() { return apply(m_after, m_before); }
bool ModifyRenderLayerEffectProfileCommand::undo() { return apply(m_before, m_after); }

} // namespace Engine
