#include "PostProcessProfileCommands.hpp"
#include "../../renderer/postprocess/PostProcessPipeline.hpp"
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

void ModifyPostProcessProfileCommand::apply(const PostProcessProfile& value) {
    if (!m_target || !m_pipeline) return;
    *m_target = value;
    m_target->apply_to_pipeline(*m_pipeline);
}

void ModifyPostProcessProfileCommand::execute() {
    apply(m_after);
}

void ModifyPostProcessProfileCommand::undo() {
    apply(m_before);
}

} // namespace Engine
