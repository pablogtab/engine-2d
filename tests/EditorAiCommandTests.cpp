#include "editor/ai/ProjectCommandRegistry.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

using namespace Engine;

int main() {
    const auto root = std::filesystem::temp_directory_path() / "engine2d-command-tests";
    const auto project = root / "project";
    const auto engine = root / "engine";
    std::filesystem::create_directories(project / "ProjectSettings");
    std::filesystem::create_directories(engine / "build");
    const auto config = nlohmann::json{
        {"version", 1},
        {"commands", nlohmann::json::array({
            {{"name", "print"}, {"kind", "log"}, {"program", "/usr/bin/printf"},
             {"args", nlohmann::json::array({"hello"})}, {"working_directory", "${projectRoot}"},
             {"timeout_seconds", 5}, {"artifact_patterns", nlohmann::json::array()}},
            {{"name", "artifact"}, {"kind", "build"}, {"program", "/usr/bin/touch"},
             {"args", nlohmann::json::array({"${projectRoot}/artifact.bin"})}, {"working_directory", "${projectRoot}"},
             {"timeout_seconds", 5}, {"artifact_patterns", nlohmann::json::array({"${projectRoot}/artifact.bin"})}},
            {{"name", "sleep"}, {"kind", "test"}, {"program", "/bin/sleep"},
             {"args", nlohmann::json::array({"5"})}, {"working_directory", "${engineRoot}"},
             {"timeout_seconds", 10}, {"artifact_patterns", nlohmann::json::array()}},
            {{"name", "flood"}, {"kind", "log"}, {"program", "/usr/bin/yes"},
             {"args", nlohmann::json::array({"bounded"})}, {"working_directory", "${projectRoot}"},
             {"timeout_seconds", 1}, {"artifact_patterns", nlohmann::json::array()}}
        })}
    };
    { std::ofstream out(project / "ProjectSettings/Commands.json"); out << config.dump(2); }

    ProjectCommandRegistry registry(project.string(), engine.string());
    std::string error;
    assert(registry.load(error));
    assert(registry.commands().size() == 4);
    const auto initial_status = registry.status();
    assert(initial_status.contains("platform"));
    assert(initial_status.contains("executables"));
    assert(initial_status.at("running") == false);
    nlohmann::json result;
    assert(registry.run("print", result, error));
    assert(result.at("status") == "succeeded");
    assert(result.at("output") == "hello");
    assert(registry.run("artifact", result, error));
    assert(result.at("artifacts").size() == 1);

    nlohmann::json cancelled_result;
    std::string cancelled_error;
    std::thread sleeping([&] { assert(registry.run("sleep", cancelled_result, cancelled_error)); });
    for (int attempt = 0; attempt < 100 && !registry.status().at("running").get<bool>(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    nlohmann::json parallel_result;
    std::string parallel_error;
    assert(!registry.run("print", parallel_result, parallel_error));
    assert(ProjectCommandRegistry::cancel_active());
    sleeping.join();
    assert(cancelled_result.at("cancelled") == true);
    assert(cancelled_result.at("status") == "cancelled");

    assert(registry.run("flood", result, error));
    assert(result.at("timed_out") == true);
    assert(result.at("output_truncated") == true);
    assert(result.at("output").get<std::string>().size() == 64 * 1024);

    auto invalid = config;
    invalid["commands"][0]["working_directory"] = "${projectRoot}/../../outside";
    { std::ofstream out(project / "ProjectSettings/Commands.json"); out << invalid.dump(2); }
    assert(!registry.load(error));

    std::filesystem::remove_all(root);
}
