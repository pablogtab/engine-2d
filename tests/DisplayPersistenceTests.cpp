#include "core/DisplayManager.hpp"
#include "core/PlayerPrefs.hpp"
#include "core/ProjectSettings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "Display persistence test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::trunc);
    file << contents;
    require(file.good(), "temporary fixture can be written");
}

class FakeDisplayBackend final : public Engine::IDisplayBackend {
public:
    Engine::DisplayCapabilities capabilities() const override {
        return {true, true, true, true, true};
    }

    std::vector<Engine::DisplayInfo> available_displays() const override {
        return {{
            1,
            "Primary",
            {0, 0, 1920, 1080},
            {0, 0, 1920, 1040},
            1.0f,
            Engine::DisplayOrientation::Landscape,
            {{1280, 720, 60.0f, 1.0f}},
            true
        }};
    }

    std::vector<Engine::DisplayMode> available_modes(Engine::DisplayId) const override {
        return {{1280, 720, 60.0f, 1.0f}};
    }

    Engine::DisplayState query_state() const override { return state; }

    bool apply(const Engine::DisplaySettings& settings, std::string&) override {
        state.settings = settings;
        state.logical_width = settings.window_width;
        state.logical_height = settings.window_height;
        state.pixel_width = settings.window_width;
        state.pixel_height = settings.window_height;
        return true;
    }

private:
    Engine::DisplayState state{
        {1, Engine::WindowMode::Windowed, 1280, 720, 0.0f, true},
        1280,
        720,
        1280,
        720,
        1.0f,
        Engine::DisplayOrientation::Landscape
    };
};

} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() / "engine2d_display_persistence_test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto project_path = directory / "project.json";
    const auto prefs_path = directory / "preferences.json";

    write_file(project_path, R"({
  "display": {
    "reference_width": 1600,
    "reference_height": 900,
    "presentation": "fill",
    "default_window_mode": "borderless",
    "default_window_width": 1024,
    "default_window_height": 768,
    "default_refresh_rate": 75.0,
    "default_vsync": false,
    "orientation_policy": "portrait"
  }
})");
    write_file(prefs_path, R"({"display.window_mode":"windowed","display.width":900})");

    Engine::ProjectSettings project_settings;
    require(project_settings.load(project_path.string()), "project display settings load");
    require(project_settings.display().reference_width == 1600, "reference resolution is loaded");
    require(project_settings.display().presentation == Engine::PresentationMode::Fill, "presentation is loaded");
    require(project_settings.display().default_window_mode == Engine::WindowMode::BorderlessFullscreen, "desktop mode is loaded");
    require(!project_settings.display().default_vsync, "VSync default is loaded");
    require(
        project_settings.display().orientation_policy == Engine::OrientationPolicy::Portrait,
        "orientation policy is loaded"
    );
    require(
        std::string(Engine::orientation_policy_sdl_hint(
            project_settings.display().orientation_policy
        )) == "Portrait PortraitUpsideDown",
        "orientation policy maps to the SDL startup hint"
    );
    require(
        Engine::orientation_policy_from_string("invalid") == Engine::OrientationPolicy::Auto,
        "unknown orientation policies use the safe automatic default"
    );

    Engine::PlayerPrefs::init(prefs_path.string());
    const std::string project_before = read_file(project_path);
    const std::string prefs_before = read_file(prefs_path);

    Engine::DisplayManager display(std::make_unique<FakeDisplayBackend>());
    Engine::DisplaySettings candidate = display.current_state().settings;
    candidate.window_width = 900;
    candidate.window_height = 600;
    candidate.vsync = false;
    require(display.apply(candidate).success, "runtime display settings apply");

    require(read_file(project_path) == project_before, "runtime apply does not modify ProjectSettings");
    require(read_file(prefs_path) == prefs_before, "runtime apply does not modify PlayerPrefs");

    Engine::ProjectDisplaySettings changed = project_settings.display();
    changed.default_window_mode = Engine::WindowMode::ExclusiveFullscreen;
    changed.default_refresh_rate = 120.0f;
    changed.orientation_policy = Engine::OrientationPolicy::LandscapeRight;
    project_settings.set_display(changed);
    const std::string saved = read_file(project_path);
    require(saved.find("\"display\"") != std::string::npos, "project settings use the display section");
    require(saved.find("\"game_view\"") == std::string::npos, "obsolete game_view section is not serialized");
    require(saved.find("\"exclusive\"") != std::string::npos, "desktop defaults are serialized");
    require(
        saved.find("\"orientation_policy\": \"landscape_right\"") != std::string::npos,
        "orientation policy is serialized"
    );

    std::filesystem::remove_all(directory);
    return EXIT_SUCCESS;
}
