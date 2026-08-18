#pragma once

#include "IEditorCommand.hpp"
#include "../../renderer/postprocess/PostProcessProfile.hpp"

namespace Engine {

class PostProcessPipeline;

class ModifyPostProcessProfileCommand final : public IEditorCommand {
public:
    ModifyPostProcessProfileCommand(
        PostProcessProfile* target,
        PostProcessPipeline* pipeline,
        PostProcessProfile before,
        PostProcessProfile after
    );

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string get_description() const override { return "Change Post-Processing Profile"; }

private:
    void apply(const PostProcessProfile& value);

    PostProcessProfile* m_target{nullptr};
    PostProcessPipeline* m_pipeline{nullptr};
    PostProcessProfile m_before;
    PostProcessProfile m_after;
};

} // namespace Engine
