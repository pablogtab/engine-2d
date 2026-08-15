#include "core/Application.hpp"
#include "scene/GenericScene.hpp"
#include "scene/SceneSerializer.hpp"
#include "sandbox/AnimatedSpriteDemoScene.hpp"
#include "sandbox/DepthDemoScene.hpp"
#include "sandbox/PlatformerDemoScene.hpp"
#include "sandbox/StressTestScene.hpp"
#include <iostream>
#include <memory>

class EngineTestbedApp : public Engine::Application {
public:
  EngineTestbedApp()
      : Engine::Application("Cross-Platform 2D Engine - Testbed Framework",
                            1280, 720) {}

protected:
  void on_init() override {
    SDL_Renderer *renderer = get_window().get_sdl_renderer();

    // 1. Primary Editor Level: Load PlatformerDemo.scene from disk
    auto platformer_level = std::make_shared<Engine::GenericScene>("PlatformerDemo");
    Engine::SceneSerializer serializer(platformer_level.get(), renderer);
    if (serializer.deserialize("assets/scenes/PlatformerDemo.scene")) {
        get_scene_manager().add_scene(platformer_level);
        get_scene_manager().set_active_scene("PlatformerDemo");
    }

    // 2. Register testbed scenes for engine validation and standalone F1 mode
    get_scene_manager().add_scene(
        std::make_shared<Sandbox::PlatformerDemoScene>(renderer));
    get_scene_manager().add_scene(
        std::make_shared<Sandbox::AnimatedSpriteDemoScene>(renderer));
    get_scene_manager().add_scene(
        std::make_shared<Sandbox::DepthDemoScene>(renderer));
    get_scene_manager().add_scene(std::make_shared<Sandbox::StressTestScene>(
        renderer, &get_thread_pool(), 3000));

    // Ensure PlatformerDemo is the default active scene on launch
    get_scene_manager().set_active_scene("PlatformerDemo");

    std::cout << "\n======================================================="
              << std::endl;
    std::cout << " [TESTBED FRAMEWORK ACTIVE]" << std::endl;
    std::cout << " - Press TAB to switch between test scenes at runtime!"
              << std::endl;
    std::cout << " - Scene 1: PlatformerDemo.scene (Editor Level Authoring)"
              << std::endl;
    std::cout << " - Scene 2: 2D Platformer & Collision Layer Matrix (C++ Testbed)"
              << std::endl;
    std::cout << " - Scene 3: Top-Down Physics & Animated Sprites" << std::endl;
    std::cout << " - Scene 4: 2.5D Depth & Parallax Test" << std::endl;
    std::cout << " - Scene 5: GPU/CPU Stress Test (3,000 Sprites)" << std::endl;
    std::cout << "=======================================================\n"
              << std::endl;
  }
};

int main(int argc, char *argv[]) {
  EngineTestbedApp app;
  app.run();
  return 0;
}
