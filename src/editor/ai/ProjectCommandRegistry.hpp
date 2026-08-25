#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Engine {

struct ProjectCommand {
    std::string name;
    std::string kind;
    std::string program;
    std::vector<std::string> args;
    std::string working_directory;
    int timeout_seconds{0};
    std::vector<std::string> artifact_patterns;
};

class ProjectCommandRegistry {
public:
    ProjectCommandRegistry(std::string project_root, std::string engine_root);
    [[nodiscard]] bool load(std::string& error);
    [[nodiscard]] const std::vector<ProjectCommand>& commands() const { return m_commands; }
    [[nodiscard]] nlohmann::json status() const;
    [[nodiscard]] bool run(const std::string& name, nlohmann::json& result, std::string& error) const;
    static bool cancel_active();

private:
    [[nodiscard]] bool expand_and_validate(const ProjectCommand& command, std::vector<std::string>& arguments,
                                           std::string& working_directory, std::string& error) const;
    std::string m_project_root;
    std::string m_engine_root;
    std::vector<ProjectCommand> m_commands;
};

} // namespace Engine
