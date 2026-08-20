#include "core/Application.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include "scene/GenericScene.hpp"
#include "scene/SceneSerializer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>

namespace {

class StandaloneApp final : public Engine::Application {
public:
    StandaloneApp()
        : Application("Engine2D", 1280, 720, false) {
        Engine::FileSystem::init("Engine2D", "Engine2D");
    }

protected:
    void on_init() override {
        auto scene = std::make_shared<Engine::GenericScene>("PlatformerDemo");
        Engine::SceneSerializer serializer(scene.get(), get_window().get_sdl_renderer());
        if (serializer.deserialize("assets/scenes/PlatformerDemo.scene")) {
            get_scene_manager().add_scene(scene);
            get_scene_manager().set_active_scene("PlatformerDemo");
            get_scene_manager().start_play();
        }
    }
};

} // namespace

int main(int argc, char* argv[]) {
    // Development runs use the current project directory. An exported macOS
    // bundle keeps exactly the same project-relative layout in Resources.
    // Switch to it before Application loads settings or scans assets.
    if (argc > 0 && argv[0] != nullptr) {
        std::error_code error;
        const auto executable = std::filesystem::absolute(argv[0], error);
        const auto resources = executable.parent_path().parent_path() / "Resources";
        if (!error && std::filesystem::is_directory(resources / "assets", error)) {
            std::filesystem::current_path(resources, error);
        }
    }

    if (char* preference_path = SDL_GetPrefPath("Engine2D", "Engine2D")) {
        Engine::Log::init((std::filesystem::path(preference_path) / "engine.log").string());
        SDL_free(preference_path);
    }

    StandaloneApp app;
    app.run();
    return 0;
}
