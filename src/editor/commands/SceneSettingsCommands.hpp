#pragma once

#include "IEditorCommand.hpp"
#include "../../scene/Scene.hpp"
#include <cstdint>
#include <string>

namespace Engine {

class SceneManager;
class AssetDatabase;

inline constexpr std::uint32_t SCENE_SETTINGS_OWNER_ID = 0xFFFFFFFE;

class ModifySceneSettingsCommand final : public IEditorCommand {
public:
    ModifySceneSettingsCommand(
        Scene* scene,
        SceneManager* scene_manager,
        const AssetDatabase* database,
        SceneSettings before,
        SceneSettings after,
        std::string description = "Modify Scene Settings"
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    bool apply(const SceneSettings& settings);

    Scene* m_scene{nullptr};
    SceneManager* m_scene_manager{nullptr};
    const AssetDatabase* m_database{nullptr};
    SceneSettings m_before;
    SceneSettings m_after;
    std::string m_description;
};

} // namespace Engine
