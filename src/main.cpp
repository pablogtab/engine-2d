#include "core/Application.hpp"
#include "sandbox/AnimatedSpriteDemoScene.hpp"
#include "sandbox/DepthDemoScene.hpp"
#include "sandbox/StressTestScene.hpp"
#include <iostream>
#include <memory>

class EngineTestbedApp : public Engine::Application {
public:
    EngineTestbedApp() 
        : Engine::Application("Cross-Platform 2D Engine - Testbed Framework", 1280, 720) {}

protected:
    void on_init() override {
        SDL_Renderer* renderer = get_window().get_sdl_renderer();

        // Register testbed scenes
        get_scene_manager().add_scene(std::make_shared<Sandbox::AnimatedSpriteDemoScene>(renderer));
        get_scene_manager().add_scene(std::make_shared<Sandbox::DepthDemoScene>(renderer));
        get_scene_manager().add_scene(std::make_shared<Sandbox::StressTestScene>(renderer, &get_thread_pool(), 3000));

        // Restore last active scene if available
        get_scene_manager().restore_state();

        std::cout << "\n=======================================================" << std::endl;
        std::cout << " [TESTBED FRAMEWORK ACTIVE]" << std::endl;
        std::cout << " - Press TAB to switch between test scenes at runtime!" << std::endl;
        std::cout << " - Scene 1: 2D Animated Sprites & Asset Loader" << std::endl;
        std::cout << " - Scene 2: 2.5D Depth & Parallax Test" << std::endl;
        std::cout << " - Scene 3: GPU/CPU Stress Test (3,000 Sprites)" << std::endl;
        std::cout << "=======================================================\n" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    EngineTestbedApp app;
    app.run();
    return 0;
}
