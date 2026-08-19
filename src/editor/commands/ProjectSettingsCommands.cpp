#include "ProjectSettingsCommands.hpp"
#include <utility>

namespace Engine {

ModifyProjectDisplaySettingsCommand::ModifyProjectDisplaySettingsCommand(
    ProjectSettings* settings,
    ProjectDisplaySettings before,
    ProjectDisplaySettings after,
    std::function<void(const ProjectDisplaySettings&)> on_changed
)
    : m_settings(settings),
      m_before(before),
      m_after(after),
      m_on_changed(std::move(on_changed)) {}

bool ModifyProjectDisplaySettingsCommand::apply(const ProjectDisplaySettings& value) {
    if (!m_settings) return false;
    m_settings->set_display(value);
    if (m_on_changed) m_on_changed(value);
    return true;
}

bool ModifyProjectDisplaySettingsCommand::execute() {
    return apply(m_after);
}

bool ModifyProjectDisplaySettingsCommand::undo() {
    return apply(m_before);
}

} // namespace Engine
