#pragma once

#include "EditorAiToolRegistry.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Engine {

class EditorAiProjectKnowledge {
public:
  [[nodiscard]] EditorAiToolResult
  get_project_overview(const nlohmann::json &snapshot) const;
  [[nodiscard]] EditorAiToolResult
  find_entities_by_gameplay_tag(const nlohmann::json &arguments,
                                const nlohmann::json &entity_index) const;
  [[nodiscard]] EditorAiToolResult list_project_documents() const;
  [[nodiscard]] EditorAiToolResult
  read_project_document(const nlohmann::json &arguments) const;
  [[nodiscard]] EditorAiToolResult
  search_project_documents(const nlohmann::json &arguments) const;
  [[nodiscard]] EditorAiToolResult
  get_engine_capabilities(const nlohmann::json &tool_schemas) const;

private:
  struct Document {
    std::string id;
    std::filesystem::path absolute_path;
    std::uintmax_t size{0};
  };

  [[nodiscard]] std::vector<Document> project_documents() const;
  [[nodiscard]] static std::string read_bounded(const Document &document);
};

} // namespace Engine
