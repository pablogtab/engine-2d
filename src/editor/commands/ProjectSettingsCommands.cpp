#include "ProjectSettingsCommands.hpp"
#include <utility>

namespace Engine {

ModifyGameViewSettingsCommand::ModifyGameViewSettingsCommand(
    ProjectSettings* settings,
    GameViewSettings before,
    GameViewSettings after,
    std::function<void(const GameViewSettings&)> on_changed
)
    : m_settings(settings),
      m_before(before),
      m_after(after),
      m_on_changed(std::move(on_changed)) {}

void ModifyGameViewSettingsCommand::apply(const GameViewSettings& value) {
    if (!m_settings) return;
    m_settings->set_game_view(value);
    if (m_on_changed) m_on_changed(value);
}

void ModifyGameViewSettingsCommand::execute() {
    apply(m_after);
}

void ModifyGameViewSettingsCommand::undo() {
    apply(m_before);
}

} // namespace Engine
