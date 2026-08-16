#pragma once

#include "IEditorCommand.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/Components.hpp"
#include "../EditorSelection.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace Engine {

// 1. Move Entities Command (Gizmo Translation)
class MoveEntitiesCommand : public IEditorCommand {
public:
    MoveEntitiesCommand(
        EntityManager* em,
        std::unordered_map<EntityID, glm::vec3> old_positions,
        std::unordered_map<EntityID, glm::vec3> new_positions,
        std::string description = "Move Entities"
    );

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    EntityManager* m_em{nullptr};
    std::unordered_map<EntityID, glm::vec3> m_old_positions;
    std::unordered_map<EntityID, glm::vec3> m_new_positions;
    std::string m_description{"Move Entities"};
};

// 2. Delete Entities Command (with full snapshot restore on undo)
struct EntitySnapshot {
    EntityID id{0};
    TagComponent tag;
    TransformComponent transform;
    bool has_sprite{false};
    SpriteComponent sprite;
    bool has_rigidbody{false};
    RigidBodyComponent rigidbody;
    bool has_collider{false};
    ColliderComponent collider;
};

class DeleteEntitiesCommand : public IEditorCommand {
public:
    DeleteEntitiesCommand(
        EntityManager* em,
        std::shared_ptr<EditorSelection> selection,
        std::vector<EntityID> entities_to_delete
    );

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::vector<EntitySnapshot> m_snapshots;
    std::string m_description{"Delete Entities"};
};

// 3. Create Entity Command
class CreateEntityCommand : public IEditorCommand {
public:
    CreateEntityCommand(
        EntityManager* em,
        std::shared_ptr<EditorSelection> selection,
        EntitySnapshot snapshot
    );

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    EntitySnapshot m_snapshot;
    EntityID m_created_id{0};
    std::string m_description{"Create Entity"};
};

// 4. Paint Tiles Command (Tilemap Brush, Eraser, and Box Fill with Undo/Redo)
class PaintTilesCommand : public IEditorCommand {
public:
    PaintTilesCommand(
        EntityManager* em,
        EntityID tilemap_entity,
        std::unordered_map<int64_t, uint16_t> old_tiles,
        std::unordered_map<int64_t, uint16_t> new_tiles,
        std::string description = "Paint Tiles"
    );

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    EntityManager* m_em{nullptr};
    EntityID m_tilemap_entity{0};
    std::unordered_map<int64_t, uint16_t> m_old_tiles;
    std::unordered_map<int64_t, uint16_t> m_new_tiles;
    std::string m_description{"Paint Tiles"};
};

} // namespace Engine
