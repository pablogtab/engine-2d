#include "core/Application.hpp"
#include "core/ProjectContext.hpp"
#include "editor/EditorLayer.hpp"
#include "editor/ProjectHub.hpp"
#include "editor/state/EditorEntityStateStore.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace {

struct LaunchOptions {
    std::filesystem::path project_path;
    bool editor{true};
    bool show_help{false};
    bool project_hub_smoke_test{false};
};

std::optional<LaunchOptions> parse_options(int argc, char* argv[], std::string& error) {
    LaunchOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--project") {
            if (++index >= argc) {
                error = "--project requires a directory";
                return std::nullopt;
            }
            options.project_path = argv[index];
        } else if (argument.rfind("--project=", 0) == 0) {
            options.project_path = argument.substr(10);
        } else if (argument == "--standalone" || argument == "--no-editor" || argument == "--game") {
            options.editor = false;
        } else if (argument == "--editor") {
            options.editor = true;
        } else if (argument == "--help" || argument == "-h") {
            options.show_help = true;
        } else if (argument == "--project-hub-smoke-test") {
            options.project_hub_smoke_test = true;
        } else {
            error = "Unknown argument: " + argument;
            return std::nullopt;
        }
    }
    if (options.project_path.empty()) {
        if (const char* environment_project = std::getenv("ENGINE2D_PROJECT")) {
            options.project_path = environment_project;
        }
    }
    return options;
}

void print_usage() {
    std::cout
        << "Usage: Engine2D --project <project-directory> [--editor|--standalone]\n"
        << "       Engine2D [--project-hub-smoke-test]\n"
        << "\n"
        << "A project directory must contain project.engine2d, Assets/, and ProjectSettings/.\n";
}

class EditorApplication final : public Engine::Application {
public:
    EditorApplication(Engine::ProjectContext project, bool editor)
        : Application(
            project,
            "Engine2D - " + project.descriptor().name,
            1280,
            720,
            editor
        ) {}

protected:
    void on_init() override {
        const auto scene_path = get_project().resolve(get_project().descriptor().startup_scene);
        auto scene = std::make_shared<Engine::Scene>(get_project().descriptor().name);
        Engine::SceneEditorMetadata metadata;
        Engine::SceneSerializer serializer(
            scene.get(), get_window().get_sdl_renderer(), nullptr, &metadata
        );
        if (!serializer.deserialize(scene_path.string())) {
            std::cerr << "Cannot load startup scene: " << scene_path << '\n';
            return;
        }

        get_scene_manager().add_scene(scene);
        get_scene_manager().set_active_scene(scene->get_name());
        if (Engine::EditorLayer* editor = get_editor_layer()) {
            auto& session = editor->get_or_create_session(scene.get(), scene->get_name());
            if (session.entity_state_store) {
                for (const auto& [entity, order] : metadata.component_orders) {
                    session.entity_state_store->set_component_order(entity, order);
                }
            }
            editor->bind_scene_path(scene.get(), scene_path.string());
        } else {
            get_scene_manager().start_play();
        }
    }
};

} // namespace

int main(int argc, char* argv[]) {
    std::string argument_error;
    const auto options = parse_options(argc, argv, argument_error);
    if (!options) {
        std::cerr << argument_error << "\n\n";
        print_usage();
        return 2;
    }
    if (options->show_help) {
        print_usage();
        return 0;
    }
    if (!options->editor && options->project_path.empty()) {
        std::cerr << "Standalone mode requires --project <project-directory>.\n\n";
        print_usage();
        return 2;
    }

    if (options->project_hub_smoke_test) {
        Engine::ProjectHub hub;
        static_cast<void>(hub.run(3));
        return 0;
    }

    std::optional<std::filesystem::path> selected_project;
    if (!options->project_path.empty()) selected_project = options->project_path;
    bool open_project_hub = !selected_project.has_value();
    do {
        if (open_project_hub) {
            Engine::ProjectHub hub;
            selected_project = hub.run();
            if (!selected_project) return 0;
        }

        std::string project_error;
        auto project = Engine::ProjectContext::open(*selected_project, &project_error);
        if (!project) {
            if (!options->editor) {
                std::cerr << project_error << '\n';
                return 2;
            }
            std::cerr << project_error << '\n';
            selected_project.reset();
            open_project_hub = true;
            continue;
        }

        EditorApplication application(std::move(*project), options->editor);
        Engine::ProjectHub::record_recent_project(
            application.get_project().root(), application.get_project().descriptor().name
        );
        application.run();
        open_project_hub = options->editor && application.should_open_project_hub_after_exit();
        if (open_project_hub) selected_project.reset();
    } while (open_project_hub);
    return 0;
}
