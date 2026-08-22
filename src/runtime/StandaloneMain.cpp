#include "core/Application.hpp"
#include "core/AssetDatabase.hpp"
#include "core/FileSystem.hpp"
#include "core/ProjectContext.hpp"
#include "scene/GenericScene.hpp"
#include "scene/SceneSerializer.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::filesystem::path packaged_project_root(const char* executable_argument) {
    if (!executable_argument) return {};
    std::error_code error;
    const auto executable = std::filesystem::absolute(executable_argument, error);
    if (error) return {};
    const auto portable_root = executable.parent_path();
    if (std::filesystem::is_regular_file(portable_root / "project.engine2d", error)) {
        return portable_root;
    }
    error.clear();
    const auto resources = executable.parent_path().parent_path() / "Resources";
    return std::filesystem::is_regular_file(resources / "project.engine2d", error)
        ? resources : std::filesystem::path{};
}

class StandaloneApplication final : public Engine::Application {
public:
    explicit StandaloneApplication(Engine::ProjectContext project)
        : Application(
            project,
            project.descriptor().name,
            1280,
            720,
            false
        ) {}

protected:
    void on_init() override {
        const auto scene_path = get_project().resolve(get_project().descriptor().startup_scene);
        auto scene = std::make_shared<Engine::GenericScene>(get_project().descriptor().name);
        Engine::SceneSerializer serializer(scene.get(), get_window().get_sdl_renderer());
        if (!serializer.deserialize(scene_path.string())) {
            std::cerr << "Cannot load startup scene: " << scene_path << '\n';
            return;
        }
        get_scene_manager().add_scene(scene);
        get_scene_manager().set_active_scene(scene->get_name());
        get_scene_manager().start_play();
    }
};

int run_smoke_test(const Engine::ProjectContext& project) {
    Engine::FileSystem::init("Engine2D", project.descriptor().name, true, project.root());
    auto& database = Engine::AssetDatabase::get();
    database.set_project_root(project.root());
    const auto scan = database.scan("Assets");
    const auto* scene = database.find_by_path(project.descriptor().startup_scene);
    if (!scene || scene->status != Engine::AssetStatus::Ready) {
        std::cerr << "[RUNTIME SMOKE] Startup scene is unavailable\n";
        return 1;
    }
    Engine::GenericScene loaded_scene(project.descriptor().name);
    Engine::SceneSerializer serializer(&loaded_scene);
    if (!serializer.deserialize(project.resolve(project.descriptor().startup_scene).string())) {
        std::cerr << "[RUNTIME SMOKE] Startup scene could not be deserialized\n";
        return 1;
    }
    std::cout << "[RUNTIME SMOKE] Project=" << project.descriptor().name
              << " Assets=" << scan.ready
              << " Scene=" << project.descriptor().startup_scene.generic_string() << '\n';
    return 0;
}

void print_usage() {
    std::cout << "Usage: Engine2DStandalone --project <project-directory> [--smoke-test]\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path project_path = packaged_project_root(argc > 0 ? argv[0] : nullptr);
    bool smoke_test = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--project" && index + 1 < argc) {
            project_path = argv[++index];
        } else if (argument.rfind("--project=", 0) == 0) {
            project_path = argument.substr(10);
        } else if (argument == "--smoke-test") {
            smoke_test = true;
        } else if (argument == "--help" || argument == "-h") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            print_usage();
            return 2;
        }
    }
    if (project_path.empty()) {
        std::cerr << "No project selected.\n";
        print_usage();
        return 2;
    }

    std::string error;
    auto project = Engine::ProjectContext::open(project_path, &error);
    if (!project) {
        std::cerr << error << '\n';
        return 2;
    }
    if (smoke_test) return run_smoke_test(*project);

    StandaloneApplication application(std::move(*project));
    application.run();
    return 0;
}
