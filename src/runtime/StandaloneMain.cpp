#include "core/Application.hpp"
#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"
#include "core/Log.hpp"
#include "renderer/material/ShaderAsset.hpp"
#include "scene/GenericScene.hpp"
#include "scene/SceneSerializer.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

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

int run_runtime_smoke_test() {
    auto& database = Engine::AssetDatabase::get();
    const auto scan_result = database.scan("assets");
    const auto* scene = database.find_by_path("assets/scenes/PlatformerDemo.scene");
    const auto* shader = database.find_by_path("assets/shaders/NightVision.shader");
    if (!scene || scene->status != Engine::AssetStatus::Ready ||
        !shader || shader->status != Engine::AssetStatus::Ready) {
        std::cerr << "[RUNTIME SMOKE] Required scene or shader asset is unavailable\n";
        return 1;
    }

    std::string shader_error;
    if (!Engine::ShaderAsset::load(shader->handle, database, &shader_error)) {
        std::cerr << "[RUNTIME SMOKE] Failed to load packaged shader: " << shader_error << '\n';
        return 1;
    }

    std::cout << "[RUNTIME SMOKE] Assets=" << scan_result.ready
              << " Scene=PlatformerDemo Shader=NightVision\n";
    return 0;
}

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

    if (argc > 1 && std::string_view(argv[1]) == "--smoke-test") {
        return run_runtime_smoke_test();
    }

    StandaloneApp app;
    app.run();
    return 0;
}
