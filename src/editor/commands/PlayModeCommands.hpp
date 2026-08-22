#pragma once

#include "IEditorCommand.hpp"
#include "../../scene/SceneManager.hpp"

#include <string>

struct SDL_Renderer;

namespace Engine {

class Scene;

class ApplyPlayModeChangesCommand final : public IEditorCommand {
public:
    ApplyPlayModeChangesCommand(
        Scene* scene,
        SDL_Renderer* renderer,
        PlayModeApplySnapshot snapshot
    );

    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    bool apply(const std::string& json);

    Scene* m_scene{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    std::string m_before_json;
    std::string m_after_json;
    std::string m_description;
};

} // namespace Engine
