#pragma once

#include "../tools/EditorIcons.hpp"
#include "../../scene/ComponentRegistry.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;

namespace Engine {

class AssetManager;
class EditorCommandHistory;
class EditorPropertyTransaction;
class SpriteAnimationStudioPanel;
struct AnimationPreviewState;

enum class EditorComponentCategory {
    Core,
    Rendering,
    Physics,
    Animation,
    World,
    UI
};

struct EditorInspectorContext {
    SDL_Renderer* renderer{nullptr};
    AssetManager* asset_manager{nullptr};
    EditorCommandHistory* command_history{nullptr};
    EditorPropertyTransaction* transactions{nullptr};
    AnimationPreviewState* animation_preview{nullptr};
    SpriteAnimationStudioPanel* animation_studio{nullptr};
    bool locked{false};
};

struct EditorComponentDescriptor {
    std::string component_key;
    std::string drawer_key;
    std::string display_name;
    EditorComponentCategory category{EditorComponentCategory::Core};
    EditorIcon icon{EditorIcon::Component};
    bool show_in_inspector{true};
    bool addable{true};
    std::vector<std::string> dependencies;
    std::vector<std::string> incompatible_components;
    std::function<void(EntityManager&, EntityID, const EditorInspectorContext&)> draw;
    std::function<void(
        EntityManager&,
        EntityID,
        const EditorInspectorContext&,
        ComponentSnapshotSet&
    )> create_default;
};

struct EditorComponentAvailability {
    bool available{false};
    std::string reason;
};

class EditorComponentRegistry {
public:
    bool register_component(EditorComponentDescriptor descriptor, std::string* error = nullptr);

    [[nodiscard]] const EditorComponentDescriptor* find(std::string_view component_key) const;
    [[nodiscard]] const std::vector<EditorComponentDescriptor>& descriptors() const { return m_descriptors; }
    [[nodiscard]] bool validate(const ComponentRegistry& runtime_registry, std::string& error) const;
    [[nodiscard]] EditorComponentAvailability availability(
        const EditorComponentDescriptor& descriptor,
        const ComponentRegistry& runtime_registry,
        const EntityManager& em,
        EntityID entity
    ) const;
    bool build_default_snapshot(
        std::string_view component_key,
        EntityManager& em,
        EntityID entity,
        const EditorInspectorContext& context,
        ComponentSnapshotSet& snapshot,
        std::string& error
    ) const;

private:
    std::vector<EditorComponentDescriptor> m_descriptors;
    std::unordered_map<std::string, std::size_t> m_by_component_key;
    std::unordered_map<std::string, std::size_t> m_by_drawer_key;
};

[[nodiscard]] const char* editor_component_category_label(EditorComponentCategory category);
EditorComponentRegistry& default_editor_component_registry();

} // namespace Engine
