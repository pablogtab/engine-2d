#include "SceneSettingsCommands.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../physics/PhysicsSystem.hpp"
#include "../../core/AssetDatabase.hpp"
#include <utility>

namespace Engine {

ModifySceneSettingsCommand::ModifySceneSettingsCommand(
    Scene* scene,
    SceneManager* scene_manager,
    const AssetDatabase* database,
    SceneSettings before,
    SceneSettings after,
    std::string description
)
    : m_scene(scene),
      m_scene_manager(scene_manager),
      m_database(database),
      m_before(std::move(before)),
      m_after(std::move(after)),
      m_description(std::move(description)) {}

bool ModifySceneSettingsCommand::apply(const SceneSettings& settings) {
    if (!m_scene) return false;
    if (m_scene_manager) {
        const AssetDatabase& database = m_database ? *m_database : AssetDatabase::get();
        return m_scene_manager->apply_scene_settings(*m_scene, settings, database);
    }
    m_scene->settings() = settings;
    PhysicsSystem::set_gravity(settings.gravity);
    return true;
}

bool ModifySceneSettingsCommand::execute() {
    return apply(m_after);
}

bool ModifySceneSettingsCommand::undo() {
    return apply(m_before);
}

} // namespace Engine
