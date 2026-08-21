#pragma once

#include "IEditorCommand.hpp"
#include "../../renderer/postprocess/PostProcessProfile.hpp"
#include "../../renderer/RenderLayerEffectProfile.hpp"
#include <vector>

namespace Engine {

class PostProcessPipeline;
class Scene;
class AssetDatabase;

class ModifyPostProcessProfileCommand final : public IEditorCommand {
public:
    ModifyPostProcessProfileCommand(
        PostProcessProfile* target,
        PostProcessPipeline* pipeline,
        PostProcessProfile before,
        PostProcessProfile after
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return "Change Post-Processing Profile"; }

private:
    bool apply(const PostProcessProfile& value);

    PostProcessProfile* m_target{nullptr};
    PostProcessPipeline* m_pipeline{nullptr};
    PostProcessProfile m_before;
    PostProcessProfile m_after;
};

class SetSceneRenderLayerEffectsCommand final : public IEditorCommand {
public:
    SetSceneRenderLayerEffectsCommand(Scene* scene, AssetDatabase* database,
        std::vector<RenderLayerEffectAssignment> before, std::vector<RenderLayerEffectAssignment> after);
    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return "Change Scene Layer Effects"; }
private:
    bool apply(const std::vector<RenderLayerEffectAssignment>& assignments);
    Scene* m_scene{nullptr};
    AssetDatabase* m_database{nullptr};
    std::vector<RenderLayerEffectAssignment> m_before;
    std::vector<RenderLayerEffectAssignment> m_after;
};

class ModifyRenderLayerEffectProfileCommand final : public IEditorCommand {
public:
    ModifyRenderLayerEffectProfileCommand(Scene* scene, AssetDatabase* database, AssetHandle handle,
        RenderLayerEffectProfile before, RenderLayerEffectProfile after);
    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return "Edit Render Layer Effect Profile"; }
    [[nodiscard]] EditorCommandScope get_scope() const override { return EditorCommandScope::EditorSession; }
private:
    bool apply(const RenderLayerEffectProfile& profile, const RenderLayerEffectProfile& rollback);
    Scene* m_scene{nullptr};
    AssetDatabase* m_database{nullptr};
    AssetHandle m_handle{};
    RenderLayerEffectProfile m_before;
    RenderLayerEffectProfile m_after;
};

} // namespace Engine
