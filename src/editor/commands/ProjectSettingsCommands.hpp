#pragma once

#include "IEditorCommand.hpp"
#include "../../core/ProjectSettings.hpp"
#include <functional>

namespace Engine {

class ModifyGameViewSettingsCommand final : public IEditorCommand {
public:
    ModifyGameViewSettingsCommand(
        ProjectSettings* settings,
        GameViewSettings before,
        GameViewSettings after,
        std::function<void(const GameViewSettings&)> on_changed = {}
    );

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string get_description() const override { return "Change Game Output"; }

private:
    void apply(const GameViewSettings& value);

    ProjectSettings* m_settings{nullptr};
    GameViewSettings m_before;
    GameViewSettings m_after;
    std::function<void(const GameViewSettings&)> m_on_changed;
};

} // namespace Engine
