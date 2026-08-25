#pragma once

#include "../../ecs/EntityManager.hpp"
#include "EditorAiProvider.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct SDL_Renderer;

namespace Engine {

class AssetDatabase;
class EditorSelection;
class Scene;
class Texture;

struct EditorAiAttachmentResult {
  bool success{false};
  std::string message;
};

class EditorAiAttachmentStore {
public:
  struct EntityContext {
    EntityID entity_id{0};
    std::string name;
  };

  struct AssetContext {
    std::string asset_handle;
    std::string path;
    std::string name;
  };

  struct Screenshot {
    std::filesystem::path path;
    std::string name;
    std::string mime_type;
    std::size_t byte_count{0};
    std::unique_ptr<Texture> preview;
  };

  EditorAiAttachmentStore(SDL_Renderer *renderer,
                          std::filesystem::path project_root);
  ~EditorAiAttachmentStore();

  EditorAiAttachmentStore(const EditorAiAttachmentStore &) = delete;
  EditorAiAttachmentStore &operator=(const EditorAiAttachmentStore &) = delete;

  [[nodiscard]] EditorAiAttachmentResult
  attach_selection(const std::shared_ptr<Scene> &active_scene,
                   const EditorSelection *selection);
  [[nodiscard]] EditorAiAttachmentResult
  attach_assets(const std::vector<std::filesystem::path> &paths,
                AssetDatabase *database);
  [[nodiscard]] EditorAiAttachmentResult
  attach_screenshot_bytes(const std::uint8_t *bytes, std::size_t size,
                          const std::string &mime_type,
                          const std::string &origin);

  void prune_entities(const std::shared_ptr<Scene> &active_scene);
  [[nodiscard]] nlohmann::json
  build_context(const nlohmann::json &request_context,
                AssetDatabase *database) const;
  [[nodiscard]] std::vector<EditorAiImageAttachment>
  build_images(bool local_images,
               std::vector<std::filesystem::path> &cleanup_paths) const;

  void remove_entity(std::size_t index);
  void remove_asset(std::size_t index);
  void remove_screenshot(std::size_t index);
  void clear_contexts();
  void clear_screenshots(bool remove_files = true);
  void clear_all();

  [[nodiscard]] const std::vector<EntityContext> &entities() const {
    return m_entities;
  }
  [[nodiscard]] const std::vector<AssetContext> &assets() const {
    return m_assets;
  }
  [[nodiscard]] std::vector<Screenshot> &screenshots() { return m_screenshots; }
  [[nodiscard]] const std::vector<Screenshot> &screenshots() const {
    return m_screenshots;
  }
  [[nodiscard]] bool has_contexts() const {
    return !m_entities.empty() || !m_assets.empty();
  }
  [[nodiscard]] bool has_screenshots() const { return !m_screenshots.empty(); }

private:
  static constexpr std::size_t MAX_CONTEXTS = 20;
  static constexpr std::size_t MAX_SCREENSHOTS = 4;
  static constexpr std::size_t MAX_SCREENSHOT_BYTES = 12 * 1024 * 1024;

  SDL_Renderer *m_renderer{nullptr};
  std::filesystem::path m_project_root;
  std::vector<EntityContext> m_entities;
  std::vector<AssetContext> m_assets;
  std::vector<Screenshot> m_screenshots;
  std::weak_ptr<Scene> m_entity_scene;
  std::uint64_t m_screenshot_sequence{0};
};

} // namespace Engine
