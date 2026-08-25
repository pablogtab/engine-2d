#include "EditorAiProjectKnowledge.hpp"

#include "../../core/AssetDatabase.hpp"
#include "../../core/Input.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../enginescript/module/GameModule.hpp"
#include "../../scene/ComponentRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace Engine {
namespace {

constexpr std::uintmax_t MAX_DOCUMENT_BYTES = 256 * 1024;
constexpr std::size_t MAX_OVERVIEW_DOCUMENTS = 6;
constexpr std::size_t MAX_OVERVIEW_DOCUMENT_PREVIEW_BYTES = 1000;

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

bool is_markdown(const std::filesystem::path &path) {
  return lowercase(path.extension().string()) == ".md";
}

nlohmann::json gameplay_tags_from_entity(const nlohmann::json &entity) {
  const nlohmann::json component =
      entity.value("component_data", nlohmann::json::object())
          .value("gameplay_tags", nlohmann::json::object());
  if (!component.is_object())
    return nlohmann::json::array();
  const nlohmann::json tags = component.value("tags", nlohmann::json::array());
  return tags.is_array() ? tags : nlohmann::json::array();
}

} // namespace

std::vector<EditorAiProjectKnowledge::Document>
EditorAiProjectKnowledge::project_documents() const {
  const std::filesystem::path root = AssetDatabase::get().project_root();
  std::vector<Document> documents;
  std::set<std::string> seen;
  const auto add = [&](const std::filesystem::path &path) {
    std::error_code error;
    if (std::filesystem::is_symlink(path, error) || error)
      return;
    if (!std::filesystem::is_regular_file(path, error) || error ||
        !is_markdown(path))
      return;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > MAX_DOCUMENT_BYTES)
      return;
    const std::filesystem::path relative = path.lexically_relative(root);
    if (relative.empty() || relative.native().starts_with(".."))
      return;
    const std::string id = relative.generic_string();
    if (seen.insert(id).second)
      documents.push_back({id, path, size});
  };

  add(root / "AGENTS.md");
  add(root / "README.md");
  for (const char *directory_name : {"Docs", "docs"}) {
    const std::filesystem::path directory = root / directory_name;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error)
      continue;
    for (std::filesystem::recursive_directory_iterator iterator(
             directory,
             std::filesystem::directory_options::skip_permission_denied, error),
         end;
         iterator != end && !error; iterator.increment(error)) {
      if (iterator.depth() > 4)
        iterator.disable_recursion_pending();
      if (iterator->is_symlink(error)) {
        iterator.disable_recursion_pending();
        continue;
      }
      add(iterator->path());
      if (documents.size() >= 100)
        break;
    }
  }
  std::sort(
      documents.begin(), documents.end(),
      [](const Document &lhs, const Document &rhs) { return lhs.id < rhs.id; });
  return documents;
}

std::string EditorAiProjectKnowledge::read_bounded(const Document &document) {
  if (document.size > MAX_DOCUMENT_BYTES)
    return {};
  std::ifstream input(document.absolute_path, std::ios::binary);
  if (!input)
    return {};
  std::ostringstream contents;
  contents << input.rdbuf();
  std::string result = contents.str();
  if (result.size() > MAX_DOCUMENT_BYTES ||
      result.find('\0') != std::string::npos)
    return {};
  return result;
}

EditorAiToolResult EditorAiProjectKnowledge::list_project_documents() const {
  nlohmann::json result = nlohmann::json::array();
  for (const Document &document : project_documents()) {
    result.push_back({{"document_id", document.id}, {"bytes", document.size}});
  }
  return {true, {{"documents", std::move(result)}}, {}};
}

EditorAiToolResult EditorAiProjectKnowledge::read_project_document(
    const nlohmann::json &arguments) const {
  if (arguments.size() != 1 || !arguments.contains("document_id") ||
      !arguments.at("document_id").is_string()) {
    return {
        false, {}, "read_project_document requires only a string document_id"};
  }
  const std::string id = arguments.at("document_id").get<std::string>();
  const std::vector<Document> documents = project_documents();
  const auto found =
      std::find_if(documents.begin(), documents.end(),
                   [&](const Document &document) { return document.id == id; });
  if (found == documents.end())
    return {false, {}, "Unknown or disallowed project document"};
  std::string content = read_bounded(*found);
  if (content.empty() && found->size > 0)
    return {false, {}, "Project document is unavailable"};
  return {
      true, {{"document_id", found->id}, {"content", std::move(content)}}, {}};
}

EditorAiToolResult EditorAiProjectKnowledge::search_project_documents(
    const nlohmann::json &arguments) const {
  if (arguments.size() != 2 || !arguments.contains("query") ||
      !arguments.at("query").is_string() || !arguments.contains("limit") ||
      !arguments.at("limit").is_number_integer()) {
    return {false, {}, "search_project_documents requires query and limit"};
  }
  const std::string query = lowercase(arguments.at("query").get<std::string>());
  const std::int64_t limit = arguments.at("limit").get<std::int64_t>();
  if (query.size() < 2 || query.size() > 128 || limit < 1 || limit > 20) {
    return {false, {}, "Project document query or limit is out of range"};
  }

  nlohmann::json matches = nlohmann::json::array();
  for (const Document &document : project_documents()) {
    const std::string content = read_bounded(document);
    const std::string normalized = lowercase(content);
    std::size_t offset = normalized.find(query);
    if (offset == std::string::npos)
      continue;
    const std::size_t begin = offset > 160 ? offset - 160 : 0;
    const std::size_t length =
        std::min<std::size_t>(400, content.size() - begin);
    matches.push_back({{"document_id", document.id},
                       {"snippet", content.substr(begin, length)}});
    if (matches.size() >= static_cast<std::size_t>(limit))
      break;
  }
  return {true, {{"matches", std::move(matches)}}, {}};
}

EditorAiToolResult EditorAiProjectKnowledge::find_entities_by_gameplay_tag(
    const nlohmann::json &arguments, const nlohmann::json &entity_index) const {
  if (arguments.size() != 2 || !arguments.contains("tag") ||
      !arguments.at("tag").is_string() || !arguments.contains("limit") ||
      !arguments.at("limit").is_number_integer()) {
    return {false, {}, "find_entities_by_gameplay_tag requires tag and limit"};
  }
  const std::string tag = arguments.at("tag").get<std::string>();
  const std::int64_t limit = arguments.at("limit").get<std::int64_t>();
  if (!is_valid_gameplay_tag(tag) || limit < 1 || limit > 100) {
    return {false, {}, "Gameplay tag or limit is invalid"};
  }

  nlohmann::json matches = nlohmann::json::array();
  std::size_t total = 0;
  for (const nlohmann::json &entity : entity_index) {
    const nlohmann::json tags = gameplay_tags_from_entity(entity);
    if (!tags.is_array() ||
        std::find(tags.begin(), tags.end(), tag) == tags.end())
      continue;
    ++total;
    if (matches.size() < static_cast<std::size_t>(limit)) {
      nlohmann::json summary = entity;
      summary.erase("component_data");
      summary["gameplay_tags"] = tags;
      matches.push_back(std::move(summary));
    }
  }
  return {true,
          {{"tag", tag},
           {"matches", std::move(matches)},
           {"total_matches", total},
           {"truncated", total > static_cast<std::size_t>(limit)}},
          {}};
}

EditorAiToolResult EditorAiProjectKnowledge::get_engine_capabilities(
    const nlohmann::json &tool_schemas) const {
  nlohmann::json components = nlohmann::json::array();
  for (const ComponentDescriptor &descriptor :
       default_component_registry().descriptors()) {
    components.push_back({{"key", descriptor.serialized_key},
                          {"name", descriptor.display_name},
                          {"removable", descriptor.removable}});
  }
  nlohmann::json tools = nlohmann::json::array();
  for (const nlohmann::json &schema : tool_schemas)
    tools.push_back(schema.value("name", ""));
  return {
      true,
      {{"engine_script",
        {{"abi_version", EngineScript::ENGINE_SCRIPT_ABI_VERSION},
         {"sdk_hash", EngineScript::ENGINE_SCRIPT_SDK_HASH}}},
       {"registered_components", std::move(components)},
       {"editor_ai_tools", std::move(tools)},
       {"input",
        {{"actions", Input::action_names()}, {"axes", Input::axis_names()}}},
       {"runtime",
        {{"play_transport", true},
         {"runtime_inspection", true},
         {"runtime_editing_ephemeral", true},
         {"physics_debug_overlays", true},
         {"frame_profiler", true},
         {"stress_scenarios", true},
         {"input_injection", false},
         {"record_replay", false}}}},
      {}};
}

EditorAiToolResult EditorAiProjectKnowledge::get_project_overview(
    const nlohmann::json &snapshot) const {
  const std::filesystem::path root = AssetDatabase::get().project_root();
  nlohmann::json descriptor = nlohmann::json::object();
  const Document project_file{"project.engine2d", root / "project.engine2d", 0};
  std::error_code error;
  const std::uintmax_t project_size =
      std::filesystem::file_size(project_file.absolute_path, error);
  if (!error && project_size <= 64 * 1024) {
    std::ifstream input(project_file.absolute_path);
    try {
      input >> descriptor;
    } catch (const nlohmann::json::exception &) {
      descriptor = nlohmann::json::object();
    }
  }

  std::map<std::string, std::size_t> asset_counts;
  nlohmann::json scripts = nlohmann::json::array();
  for (const nlohmann::json &asset :
       snapshot.value("asset_index", nlohmann::json::array())) {
    const std::string type = asset.value("type", "unknown");
    ++asset_counts[type];
    if (type == "script")
      scripts.push_back(asset.value("path", ""));
  }
  nlohmann::json counts = nlohmann::json::object();
  for (const auto &[type, count] : asset_counts)
    counts[type] = count;

  std::map<std::string, nlohmann::json> roles;
  for (const nlohmann::json &entity :
       snapshot.value("entity_index", nlohmann::json::array())) {
    const nlohmann::json tags = gameplay_tags_from_entity(entity);
    if (!tags.is_array())
      continue;
    for (const nlohmann::json &tag : tags) {
      if (!tag.is_string())
        continue;
      roles[tag.get<std::string>()].push_back(
          {{"entity_id", entity.value("entity_id", EntityID{0})},
           {"name", entity.value("name", "")}});
    }
  }
  nlohmann::json gameplay_roles = nlohmann::json::object();
  for (auto &[tag, entities] : roles)
    gameplay_roles[tag] = std::move(entities);

  nlohmann::json document_previews = nlohmann::json::array();
  const std::vector<Document> documents = project_documents();
  nlohmann::json document_index = nlohmann::json::array();
  for (const Document &document : documents) {
    document_index.push_back(
        {{"document_id", document.id}, {"bytes", document.size}});
  }
  for (std::size_t index = 0;
       index < documents.size() && index < MAX_OVERVIEW_DOCUMENTS; ++index) {
    std::string content = read_bounded(documents[index]);
    const bool truncated = content.size() > MAX_OVERVIEW_DOCUMENT_PREVIEW_BYTES;
    if (truncated)
      content.resize(MAX_OVERVIEW_DOCUMENT_PREVIEW_BYTES);
    document_previews.push_back({{"document_id", documents[index].id},
                                 {"preview", std::move(content)},
                                 {"truncated", truncated}});
  }

  return {
      true,
      {{"project_root", root.string()},
       {"name", descriptor.value("name", root.filename().string())},
       {"startup_scene", descriptor.value("startup_scene", "")},
       {"active_scene", snapshot.value("scene", nlohmann::json::object())},
       {"gameplay_roles", std::move(gameplay_roles)},
       {"scripts", std::move(scripts)},
       {"asset_counts", std::move(counts)},
       {"input",
        {{"actions", Input::action_names()}, {"axes", Input::axis_names()}}},
       {"project_documents", std::move(document_index)},
       {"project_document_previews", std::move(document_previews)}},
      {}};
}

} // namespace Engine
