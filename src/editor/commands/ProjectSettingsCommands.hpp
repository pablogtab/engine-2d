#pragma once

#include "IEditorCommand.hpp"
#include "../../core/ProjectSettings.hpp"
#include <functional>

namespace Engine {

class ModifyProjectDisplaySettingsCommand final : public IEditorCommand {
public:
    ModifyProjectDisplaySettingsCommand(
        ProjectSettings* settings,
        ProjectDisplaySettings before,
        ProjectDisplaySettings after,
        std::function<void(const ProjectDisplaySettings&)> on_changed = {}
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return "Change Project Display Settings"; }

private:
    bool apply(const ProjectDisplaySettings& value);

    ProjectSettings* m_settings{nullptr};
    ProjectDisplaySettings m_before;
    ProjectDisplaySettings m_after;
    std::function<void(const ProjectDisplaySettings&)> m_on_changed;
};

} // namespace Engine
