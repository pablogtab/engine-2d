#include "PlayModeCommands.hpp"

#include "../../scene/Scene.hpp"
#include "../../scene/SceneSerializer.hpp"

#include <utility>

namespace Engine {

ApplyPlayModeChangesCommand::ApplyPlayModeChangesCommand(
    Scene* scene,
    SDL_Renderer* renderer,
    PlayModeApplySnapshot snapshot
) : m_scene(scene),
    m_renderer(renderer),
    m_before_json(std::move(snapshot.before_json)),
    m_after_json(std::move(snapshot.after_json)),
    m_description(
        "Apply " + std::to_string(snapshot.property_count) + " Play Mode " +
        (snapshot.property_count == 1 ? "Change" : "Changes")
    ) {}

bool ApplyPlayModeChangesCommand::execute() {
    return apply(m_after_json);
}

bool ApplyPlayModeChangesCommand::undo() {
    return apply(m_before_json);
}

bool ApplyPlayModeChangesCommand::apply(const std::string& json) {
    if (!m_scene) return false;
    SceneSerializer serializer(m_scene, m_renderer);
    return serializer.deserialize_from_string(json);
}

} // namespace Engine
