#pragma once

#include "IEditorCommand.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../ecs/Components.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../EditorSelection.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <functional>

namespace Engine {

// Generic reversible edit for inspector and future editor property controls.
// Operations resolve the component from the entity ID at execution time, so they
// remain valid if component storage moves in memory.
class ModifyEntityCommand : public IEditorCommand {
public:
    using Operation = std::function<void(EntityManager&, EntityID)>;

    ModifyEntityCommand(
        EntityManager* em,
        EntityID entity,
        Operation apply_before,
        Operation apply_after,
        std::string description
    );

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    EntityManager* m_em{nullptr};
    EntityID m_entity{0};
    Operation m_apply_before;
    Operation m_apply_after;
    std::string m_description;
};

// Complete transform snapshot for reversible viewport gestures.
class TransformEntitiesCommand : public IEditorCommand {
public:
    TransformEntitiesCommand(
        EntityManager* em,
        std::unordered_map<EntityID, TransformComponent> before,
        std::unordered_map<EntityID, TransformComponent> after,
        std::string description
    );

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    void apply(const std::unordered_map<EntityID, TransformComponent>& states);

    EntityManager* m_em{nullptr};
    std::unordered_map<EntityID, TransformComponent> m_before;
    std::unordered_map<EntityID, TransformComponent> m_after;
    std::string m_description;
};

// 2. Delete Entities Command (with full snapshot restore on undo)
struct EntitySnapshot {
    EntityID id{0};
    bool has_tag{false};
    TagComponent tag;
    bool has_transform{false};
    TransformComponent transform;
    bool has_hierarchy{false};
    HierarchyComponent hierarchy;
    bool has_sprite{false};
    SpriteComponent sprite;
    bool has_rigidbody{false};
    RigidBodyComponent rigidbody;
    bool has_collider{false};
    ColliderComponent collider;
    bool has_tilemap{false};
    TilemapComponent tilemap;
    bool has_camera{false};
    CameraComponent camera;
    bool has_animation{false};
    AnimationComponent animation;
};

[[nodiscard]] EntitySnapshot capture_entity_snapshot(EntityManager& em, EntityID entity);
bool restore_entity_snapshot(EntityManager& em, const EntitySnapshot& snapshot);

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
    [[nodiscard]] EntityID get_created_entity() const { return m_created_id; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    EntitySnapshot m_snapshot;
    EntityID m_created_id{0};
    std::string m_description{"Create Entity"};
};

// Reversible viewport duplication. Full snapshots keep the operation forward
// compatible when an entity gains additional serializable components.
class DuplicateEntitiesCommand : public IEditorCommand {
public:
    DuplicateEntitiesCommand(
        EntityManager* em,
        std::shared_ptr<EditorSelection> selection,
        std::vector<EntityID> source_entities
    );

    void execute() override;
    void undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }

private:
    EntityManager* m_em{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::vector<EntityID> m_source_entities;
    std::vector<std::unique_ptr<CreateEntityCommand>> m_create_commands;
    std::vector<EntityID> m_duplicate_entities;
    std::string m_description{"Duplicate Entities"};
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
