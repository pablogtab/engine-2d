#pragma once

#include "../EditorSelection.hpp"
#include "../../ecs/EcsTypes.hpp"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct SDL_Renderer;

namespace Engine {

class Camera2D;
class EditorCommandHistory;
class EditorEntityStateStore;
class IEditorCommand;
class LevelComposerPanel;
class Scene;

class LevelComposerViewportTool {
public:
    using PrefabPreviewCallback = std::function<void(
        const std::string& prefab_path,
        const std::vector<glm::vec2>& world_positions,
        float rotation_degrees,
        float depth_z
    )>;

    // Draws the active stamp ghost and handles click/drag placement. A drag is
    // previewed without scene mutations, then committed as one history entry.
    void handle(
        Scene* scene,
        LevelComposerPanel& panel,
        const Camera2D& camera,
        const ImVec2& image_min,
        const ImVec2& image_size,
        bool viewport_hovered,
        SDL_Renderer* renderer,
        std::shared_ptr<EditorSelection> selection,
        EditorCommandHistory* command_history,
        const EditorEntityStateStore* state_store,
        PrefabPreviewCallback preview_prefab = {},
        EntityID required_root = 0
    );

    [[nodiscard]] bool is_stroking() const { return m_stroking; }
    void cancel();

private:
    [[nodiscard]] std::unique_ptr<IEditorCommand> make_place_command(
        Scene& scene,
        const class LevelPaletteEntry& entry,
        EntityID target_parent,
        const glm::vec2& world_position,
        SDL_Renderer* renderer,
        std::shared_ptr<EditorSelection> selection,
        const EditorEntityStateStore* state_store
    );
    void draw_sprite_ghost(
        const class LevelPaletteEntry& entry,
        class Texture& texture,
        const glm::vec2& world_position,
        const Camera2D& camera,
        const ImVec2& image_min,
        const ImVec2& image_size
    ) const;
    bool m_stroking{false};
    Scene* m_stroke_scene{nullptr};
    EditorCommandHistory* m_stroke_history{nullptr};
    std::string m_stroke_entry_id;
    glm::vec2 m_stroke_start{0.0f, 0.0f};
    glm::vec2 m_stroke_end{0.0f, 0.0f};
};

} // namespace Engine
