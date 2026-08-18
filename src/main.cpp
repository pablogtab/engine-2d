#include "core/Application.hpp"
#include "editor/EditorLayer.hpp"
#include "scene/GenericScene.hpp"
#include "scene/SceneSerializer.hpp"
#include "sandbox/AnimatedSpriteDemoScene.hpp"
#include "sandbox/DepthDemoScene.hpp"
#include "sandbox/PlatformerDemoScene.hpp"
#include "sandbox/PostProcessValidationScene.hpp"
#include "sandbox/StressTestScene.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <algorithm>

namespace {

bool should_start_with_editor(int argc, char* argv[]) {
    bool standalone_or_demo = false;
    bool force_editor = false;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        std::transform(argument.begin(), argument.end(), argument.begin(), ::tolower);
        if (argument == "--editor" || argument == "-e" || argument == "--ide") {
            force_editor = true;
        } else if (argument == "--standalone" || argument == "-s" || argument == "--no-editor" || argument == "--game" ||
                   argument == "--demo" || argument == "-d" || argument.rfind("--demo=", 0) == 0 ||
                   (!argument.empty() && argument[0] != '-')) {
            standalone_or_demo = true;
        }
    }
    return force_editor || !standalone_or_demo;
}

} // namespace

class EngineTestbedApp : public Engine::Application {
public:
  EngineTestbedApp(int argc, char* argv[])
      : Engine::Application(
            "Cross-Platform 2D Engine - Testbed Framework",
            1280,
            720,
            should_start_with_editor(argc, argv)
        ),
        m_argc(argc), m_argv(argv) {
    parse_arguments();
  }

protected:
  void on_init() override {
    SDL_Renderer *renderer = get_window().get_sdl_renderer();

    // 1. Primary Editor Level: Load PlatformerDemo.scene from disk
    auto platformer_level = std::make_shared<Engine::GenericScene>("PlatformerDemo");
    Engine::SceneSerializer serializer(platformer_level.get(), renderer);
    if (serializer.deserialize("assets/scenes/PlatformerDemo.scene")) {
        get_scene_manager().add_scene(platformer_level);
    }

    // 2. Register testbed scenes for engine validation and standalone F1 mode
    get_scene_manager().add_scene(
        std::make_shared<Sandbox::PlatformerDemoScene>(renderer));
    get_scene_manager().add_scene(
        std::make_shared<Sandbox::AnimatedSpriteDemoScene>(renderer));
    get_scene_manager().add_scene(
        std::make_shared<Sandbox::DepthDemoScene>(renderer));
    get_scene_manager().add_scene(
        std::make_shared<Sandbox::PostProcessValidationScene>(renderer));
    get_scene_manager().add_scene(std::make_shared<Sandbox::StressTestScene>(
        renderer, &get_thread_pool(), 3000));

    // Activate the requested scene
    if (m_requested_scene_index >= 0 && static_cast<size_t>(m_requested_scene_index) < get_scene_manager().get_scene_count()) {
        get_scene_manager().set_active_scene(static_cast<size_t>(m_requested_scene_index));
    } else if (!m_requested_scene_name.empty()) {
        get_scene_manager().set_active_scene(m_requested_scene_name);
    } else {
        get_scene_manager().set_active_scene("PlatformerDemo");
    }

    // If a demo was requested or standalone mode specified, start in clean scene-only mode
    if (m_standalone_mode || m_is_demo_requested) {
        if (!m_force_editor) {
            if (get_editor_layer()) {
                get_editor_layer()->set_visible(false);
            }
            get_scene_manager().start_play();
        }
    }

    print_welcome_banner();
  }

private:
  void parse_arguments() {
    for (int i = 1; i < m_argc; ++i) {
        std::string arg = m_argv[i];
        std::string lower_arg = arg;
        std::transform(lower_arg.begin(), lower_arg.end(), lower_arg.begin(), ::tolower);

        if (lower_arg == "--standalone" || lower_arg == "-s" || lower_arg == "--no-editor" || lower_arg == "--game") {
            m_standalone_mode = true;
        } else if (lower_arg == "--editor" || lower_arg == "-e" || lower_arg == "--ide") {
            m_force_editor = true;
        } else if (lower_arg == "--list-demos" || lower_arg == "-l") {
            print_demo_list();
            exit(0);
        } else if (lower_arg == "--help" || lower_arg == "-h") {
            print_help();
            exit(0);
        } else if ((lower_arg == "--demo" || lower_arg == "-d") && i + 1 < m_argc) {
            set_requested_demo(m_argv[++i]);
        } else if (arg.rfind("--demo=", 0) == 0) {
            set_requested_demo(arg.substr(7));
        } else if (!arg.empty() && arg[0] != '-') {
            // Positional demo argument
            set_requested_demo(arg);
        }
    }
  }

  void set_requested_demo(const std::string& input) {
    m_is_demo_requested = true;
    std::string lower = input;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "1" || lower == "platformer" || lower == "level" || lower == "authoring" || lower == "editor") {
        m_requested_scene_name = "PlatformerDemo";
    } else if (lower == "2" || lower == "testbed" || lower == "physics" || lower == "platformer_c++" || lower == "box2d") {
        m_requested_scene_name = "2D Platformer & Collision Layer Matrix";
    } else if (lower == "3" || lower == "sprites" || lower == "anim" || lower == "animated" || lower == "topdown") {
        m_requested_scene_name = "2D Animated Sprites & Asset Loader";
    } else if (lower == "4" || lower == "depth" || lower == "parallax" || lower == "2.5d") {
        m_requested_scene_name = "2.5D Depth & Parallax Test";
    } else if (lower == "5" || lower == "postfx" || lower == "post-process" || lower == "postprocess") {
        m_requested_scene_name = "Post-Processing Validation";
    } else if (lower == "6" || lower == "stress" || lower == "benchmark" || lower == "gpu") {
        m_requested_scene_name = "GPU/CPU Stress Test";
    } else {
        m_requested_scene_name = input;
    }
  }

  void print_demo_list() {
    std::cout << "\n=======================================================" << std::endl;
    std::cout << " [AVAILABLE DEMO SCENES]" << std::endl;
    std::cout << " 1. platformer  - PlatformerDemo.scene (Editor Level Authoring)" << std::endl;
    std::cout << " 2. testbed     - 2D Platformer & Collision Layer Matrix (C++ Box2D)" << std::endl;
    std::cout << " 3. sprites     - Top-Down Physics & 2D Animated Sprites" << std::endl;
    std::cout << " 4. depth       - 2.5D Depth & Parallax Test" << std::endl;
    std::cout << " 5. postfx      - Isolated Post-Processing Validation" << std::endl;
    std::cout << " 6. stress      - GPU/CPU Stress Test (3,000 Sprites)" << std::endl;
    std::cout << "=======================================================\n" << std::endl;
  }

  void print_help() {
    std::cout << "Usage: Engine2D [options] [demo_name]\n\n"
              << "Options:\n"
              << "  -d, --demo <name|id>   Launch specific demo scene directly\n"
              << "  -s, --standalone       Launch directly in game mode (without Editor)\n"
              << "  -l, --list-demos       List all available demo scenes\n"
              << "  -h, --help             Show this help menu\n\n"
              << "Demo Names / Numbers:\n"
              << "  1, platformer, level   PlatformerDemo (Editor Level Authoring)\n"
              << "  2, testbed, physics    2D Platformer & Collision Layer Matrix (C++)\n"
              << "  3, sprites, anim       Top-Down Physics & Animated Sprites\n"
              << "  4, depth, parallax     2.5D Depth & Parallax Test\n"
              << "  5, postfx              Isolated Post-Processing Validation\n"
              << "  6, stress, benchmark   GPU/CPU Stress Test (3,000 Sprites)\n\n"
              << "In-Game Controls:\n"
              << "  TAB / Shift+TAB        Cycle between demo scenes at runtime\n"
              << "  F1 or ` (Tilde)        Toggle Editor UI Overlay\n"
              << std::endl;
  }

  void print_welcome_banner() {
    std::cout << "\n=======================================================" << std::endl;
    std::cout << " [TESTBED FRAMEWORK ACTIVE]" << std::endl;
    std::cout << " - Press TAB to switch between test scenes at runtime!" << std::endl;
    std::cout << " - Press F1 to toggle between Fullscreen Game Mode and Editor UI" << std::endl;
    std::cout << " - Scene 1: PlatformerDemo.scene (Editor Level Authoring)" << std::endl;
    std::cout << " - Scene 2: 2D Platformer & Collision Layer Matrix (C++ Testbed)" << std::endl;
    std::cout << " - Scene 3: Top-Down Physics & Animated Sprites" << std::endl;
    std::cout << " - Scene 4: 2.5D Depth & Parallax Test" << std::endl;
    std::cout << " - Scene 5: Isolated Post-Processing Validation" << std::endl;
    std::cout << " - Scene 6: GPU/CPU Stress Test (3,000 Sprites)" << std::endl;
    std::cout << "=======================================================\n" << std::endl;
  }

  int m_argc{0};
  char** m_argv{nullptr};
  int m_requested_scene_index{-1};
  std::string m_requested_scene_name{""};
  bool m_standalone_mode{false};
  bool m_is_demo_requested{false};
  bool m_force_editor{false};
};

int main(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    std::string lower = arg;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "--list-demos" || lower == "-l" || lower == "list") {
      std::cout << "\n=======================================================" << std::endl;
      std::cout << " [AVAILABLE DEMO SCENES]" << std::endl;
      std::cout << " 1. platformer  - PlatformerDemo.scene (Editor Level Authoring)" << std::endl;
      std::cout << " 2. testbed     - 2D Platformer & Collision Layer Matrix (C++ Box2D)" << std::endl;
      std::cout << " 3. sprites     - Top-Down Physics & 2D Animated Sprites" << std::endl;
      std::cout << " 4. depth       - 2.5D Depth & Parallax Test" << std::endl;
      std::cout << " 5. postfx      - Isolated Post-Processing Validation" << std::endl;
      std::cout << " 6. stress      - GPU/CPU Stress Test (3,000 Sprites)" << std::endl;
      std::cout << "=======================================================\n" << std::endl;
      return 0;
    }
    if (lower == "--help" || lower == "-h" || lower == "help") {
      std::cout << "Usage: Engine2D [options] [demo_name]\n\n"
                << "Options:\n"
                << "  -d, --demo <name|id>   Launch specific demo scene directly\n"
                << "  -s, --standalone       Launch directly in game mode (without Editor)\n"
                << "  -l, --list-demos       List all available demo scenes\n"
                << "  -h, --help             Show this help menu\n\n"
                << "Demo Names / Numbers:\n"
                << "  1, platformer, level   PlatformerDemo (Editor Level Authoring)\n"
                << "  2, testbed, physics    2D Platformer & Collision Layer Matrix (C++)\n"
                << "  3, sprites, anim       Top-Down Physics & Animated Sprites\n"
                << "  4, depth, parallax     2.5D Depth & Parallax Test\n"
                << "  5, postfx              Isolated Post-Processing Validation\n"
                << "  6, stress, benchmark   GPU/CPU Stress Test (3,000 Sprites)\n\n"
                << "In-Game Controls:\n"
                << "  TAB / Shift+TAB        Cycle between demo scenes at runtime\n"
                << "  F1 or ` (Tilde)        Toggle Editor UI Overlay\n"
                << std::endl;
      return 0;
    }
  }

  EngineTestbedApp app(argc, argv);
  app.run();
  return 0;
}
