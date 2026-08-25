#include "EditorAiAttachmentStore.hpp"

#include "../../core/AssetDatabase.hpp"
#include "../../core/BinarySerializer.hpp"
#include "../../core/FileSystem.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../renderer/Texture.hpp"
#include "../../scene/Scene.hpp"
#include "../EditorSelection.hpp"

#include <algorithm>
#include <system_error>

namespace Engine {

EditorAiAttachmentStore::EditorAiAttachmentStore(
    SDL_Renderer *renderer, std::filesystem::path project_root)
    : m_renderer(renderer), m_project_root(std::move(project_root)) {}

EditorAiAttachmentStore::~EditorAiAttachmentStore() { clear_all(); }

void EditorAiAttachmentStore::prune_entities(
    const std::shared_ptr<Scene> &active_scene) {
  const std::shared_ptr<Scene> attached_scene = m_entity_scene.lock();
  if (!active_scene ||
      (!m_entities.empty() &&
       (!attached_scene || attached_scene.get() != active_scene.get()))) {
    m_entities.clear();
    m_entity_scene.reset();
    return;
  }
  const EntityManager &entities = active_scene->get_entity_manager();
  std::erase_if(m_entities, [&](const EntityContext &context) {
    return !entities.is_alive(context.entity_id);
  });
  if (m_entities.empty())
    m_entity_scene.reset();
}

EditorAiAttachmentResult EditorAiAttachmentStore::attach_selection(
    const std::shared_ptr<Scene> &active_scene,
    const EditorSelection *selection) {
  prune_entities(active_scene);
  if (!active_scene || !selection || selection->empty()) {
    return {false, "Select one or more scene entities before pressing Cmd+L."};
  }

  EntityManager &entities = active_scene->get_entity_manager();
  std::vector<EntityID> selected(selection->get_selected().begin(),
                                 selection->get_selected().end());
  std::sort(selected.begin(), selected.end());
  std::size_t added = 0;
  for (const EntityID entity : selected) {
    if (m_entities.size() + m_assets.size() >= MAX_CONTEXTS ||
        !entities.is_alive(entity))
      break;
    if (std::any_of(m_entities.begin(), m_entities.end(),
                    [entity](const EntityContext &item) {
                      return item.entity_id == entity;
                    }))
      continue;
    std::string name = "Entity " + std::to_string(entity);
    if (entities.has_component<TagComponent>(entity)) {
      name = entities.get_component<TagComponent>(entity).tag;
    }
    m_entities.push_back({entity, std::move(name)});
    ++added;
  }
  if (!m_entities.empty())
    m_entity_scene = active_scene;
  return added > 0
             ? EditorAiAttachmentResult{true,
                                        std::to_string(added) +
                                            " selection context(s) attached."}
             : EditorAiAttachmentResult{
                   false, "The current selection is already attached."};
}

EditorAiAttachmentResult EditorAiAttachmentStore::attach_assets(
    const std::vector<std::filesystem::path> &paths, AssetDatabase *database) {
  if (!database || paths.empty()) {
    return {
        false,
        "Select one or more files in the Asset Browser before pressing Cmd+L."};
  }
  std::size_t added = 0;
  for (const std::filesystem::path &path : paths) {
    if (m_entities.size() + m_assets.size() >= MAX_CONTEXTS)
      break;
    const AssetRecord *record = database->find_by_path(path.string());
    if (!record)
      continue;
    const std::string handle = record->handle.to_string();
    if (std::any_of(m_assets.begin(), m_assets.end(),
                    [&](const AssetContext &item) {
                      return item.asset_handle == handle;
                    }))
      continue;
    m_assets.push_back(
        {handle, record->source_path,
         std::filesystem::path(record->source_path).filename().string()});
    ++added;
  }
  return added > 0
             ? EditorAiAttachmentResult{true, std::to_string(added) +
                                                  " asset context(s) attached."}
             : EditorAiAttachmentResult{
                   false,
                   "The selected assets are already attached or unavailable."};
}

EditorAiAttachmentResult EditorAiAttachmentStore::attach_screenshot_bytes(
    const std::uint8_t *bytes, std::size_t size, const std::string &mime_type,
    const std::string &origin) {
  constexpr std::uint8_t PNG_SIGNATURE[] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (!bytes || size == 0 || size > MAX_SCREENSHOT_BYTES) {
    return {false,
            "Screenshot rejected: use a PNG or JPEG no larger than 12 MiB."};
  }
  const bool png =
      mime_type == "image/png" && size >= std::size(PNG_SIGNATURE) &&
      std::equal(std::begin(PNG_SIGNATURE), std::end(PNG_SIGNATURE), bytes);
  const bool jpeg = mime_type == "image/jpeg" && size >= 3 &&
                    bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff;
  if (!png && !jpeg) {
    return {false,
            "Screenshot rejected: use a PNG or JPEG no larger than 12 MiB."};
  }
  if (m_screenshots.size() >= MAX_SCREENSHOTS) {
    return {false, "A message can contain at most four screenshots."};
  }

  const std::string extension = png ? ".png" : ".jpg";
  const std::string name =
      "screenshot_" + std::to_string(++m_screenshot_sequence) + extension;
  const std::filesystem::path directory =
      m_project_root / "Library" / "EditorAi" / "ScreenshotAttachments";
  const std::filesystem::path path = directory / name;
  static_cast<void>(FileSystem::create_directories(directory.string()));
  if (!FileSystem::write_bytes(path.string(), bytes, size)) {
    return {false, "Could not cache the screenshot attachment."};
  }

  std::unique_ptr<Texture> preview;
  if (m_renderer)
    preview.reset(Texture::load_from_file(m_renderer, path.string()));
  if (!preview || preview->get_width() <= 0 || preview->get_height() <= 0 ||
      preview->get_width() > 8192 || preview->get_height() > 8192) {
    static_cast<void>(FileSystem::remove(path.string()));
    return {false,
            "Screenshot rejected: invalid image or dimensions above 8192 px."};
  }
  m_screenshots.push_back({path, origin.empty() ? name : origin, mime_type,
                           size, std::move(preview)});
  return {true, "Screenshot attached. Add a message or send it directly."};
}

nlohmann::json
EditorAiAttachmentStore::build_context(const nlohmann::json &request_context,
                                       AssetDatabase *database) const {
  nlohmann::json attached = nlohmann::json::array();
  const nlohmann::json entity_index =
      request_context.value("entity_index", nlohmann::json::array());
  for (const EntityContext &context : m_entities) {
    const auto match = std::find_if(
        entity_index.begin(), entity_index.end(), [&](const auto &entity) {
          return entity.value("entity_id", EntityID{0}) == context.entity_id;
        });
    if (match != entity_index.end())
      attached.push_back({{"kind", "entity"}, {"data", *match}});
  }

  const nlohmann::json asset_index =
      request_context.value("asset_index", nlohmann::json::array());
  for (const AssetContext &context : m_assets) {
    const auto match = std::find_if(
        asset_index.begin(), asset_index.end(), [&](const auto &asset) {
          return asset.value("asset_handle", "") == context.asset_handle;
        });
    if (match == asset_index.end())
      continue;
    nlohmann::json item{{"kind", "asset"}, {"data", *match}};
    if (database) {
      const std::filesystem::path absolute =
          (database->project_root() / match->value("path", context.path))
              .lexically_normal();
      std::error_code error;
      constexpr std::uintmax_t MAX_TEXT_BYTES = 64 * 1024;
      const std::uintmax_t size = std::filesystem::file_size(absolute, error);
      if (!error && size <= MAX_TEXT_BYTES) {
        const std::string contents = FileSystem::read_text(absolute.string());
        if (contents.find('\0') == std::string::npos)
          item["content"] = contents;
        else
          item["content_omitted"] = "binary asset";
      } else if (!error) {
        item["content_omitted"] = "text asset exceeds 64 KiB";
        item["content_bytes"] = size;
      }
    }
    attached.push_back(std::move(item));
  }
  return attached;
}

std::vector<EditorAiImageAttachment> EditorAiAttachmentStore::build_images(
    bool local_images,
    std::vector<std::filesystem::path> &cleanup_paths) const {
  std::vector<EditorAiImageAttachment> result;
  for (const Screenshot &screenshot : m_screenshots) {
    const std::vector<std::uint8_t> bytes =
        FileSystem::read_bytes(screenshot.path.string());
    if (bytes.empty())
      continue;
    result.push_back({screenshot.path.string(),
                      local_images ? std::string{}
                                   : "data:" + screenshot.mime_type +
                                         ";base64," + Base64::encode(bytes),
                      "auto"});
    cleanup_paths.push_back(screenshot.path);
  }
  return result;
}

void EditorAiAttachmentStore::remove_entity(std::size_t index) {
  if (index >= m_entities.size())
    return;
  m_entities.erase(m_entities.begin() + static_cast<std::ptrdiff_t>(index));
  if (m_entities.empty())
    m_entity_scene.reset();
}

void EditorAiAttachmentStore::remove_asset(std::size_t index) {
  if (index < m_assets.size()) {
    m_assets.erase(m_assets.begin() + static_cast<std::ptrdiff_t>(index));
  }
}

void EditorAiAttachmentStore::remove_screenshot(std::size_t index) {
  if (index >= m_screenshots.size())
    return;
  static_cast<void>(FileSystem::remove(m_screenshots[index].path.string()));
  m_screenshots.erase(m_screenshots.begin() +
                      static_cast<std::ptrdiff_t>(index));
}

void EditorAiAttachmentStore::clear_contexts() {
  m_entities.clear();
  m_assets.clear();
  m_entity_scene.reset();
}

void EditorAiAttachmentStore::clear_screenshots(bool remove_files) {
  if (remove_files) {
    for (const Screenshot &screenshot : m_screenshots) {
      static_cast<void>(FileSystem::remove(screenshot.path.string()));
    }
  }
  m_screenshots.clear();
}

void EditorAiAttachmentStore::clear_all() {
  clear_contexts();
  clear_screenshots();
}

} // namespace Engine
