#include "EditorComponentRegistry.hpp"

#include "AnimationInspectorDrawer.hpp"
#include "CameraInspectorDrawer.hpp"
#include "ColliderInspectorDrawer.hpp"
#include "LightingInspectorDrawer.hpp"
#include "RigidBodyInspectorDrawer.hpp"
#include "RenderEffectInspectorDrawer.hpp"
#include "SpriteInspectorDrawer.hpp"
#include "TilemapInspectorDrawer.hpp"
#include "TransformInspectorDrawer.hpp"
#include "../../core/AssetManager.hpp"
#include "../../ecs/CameraSystem.hpp"
#include "../../ecs/components/AnimationComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../renderer/SpriteAnimationSystem.hpp"
#include "../../tilemap/Tilemap.hpp"
#include <stdexcept>
#include <utility>

namespace Engine {
namespace {

void register_or_throw(EditorComponentRegistry& registry, EditorComponentDescriptor descriptor) {
    std::string error;
    if (!registry.register_component(std::move(descriptor), &error)) {
        throw std::runtime_error(error);
    }
}

void register_builtin_editor_components(EditorComponentRegistry& registry) {
    register_or_throw(registry, {
        .component_key = "tag",
        .display_name = "Tag",
        .category = EditorComponentCategory::Core,
        .icon = EditorIcon::Component,
        .show_in_inspector = false,
        .addable = false
    });
    register_or_throw(registry, {
        .component_key = "group",
        .display_name = "Group",
        .category = EditorComponentCategory::Core,
        .icon = EditorIcon::Component,
        .show_in_inspector = false,
        .addable = false
    });
    register_or_throw(registry, {
        .component_key = "prefab_link",
        .display_name = "Prefab Link",
        .category = EditorComponentCategory::Core,
        .icon = EditorIcon::Component,
        .show_in_inspector = false,
        .addable = false
    });

    register_or_throw(registry, {
        .component_key = "transform",
        .drawer_key = "TransformHeader",
        .display_name = "Transform",
        .category = EditorComponentCategory::Core,
        .icon = EditorIcon::Move,
        .addable = false,
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            TransformInspectorDrawer::draw(em, entity, context.locked, {
                context.command_history, context.transactions
            });
        }
    });
    register_or_throw(registry, {
        .component_key = "sprite",
        .drawer_key = "SpriteRendererHeader",
        .display_name = "Sprite Renderer",
        .category = EditorComponentCategory::Rendering,
        .icon = EditorIcon::Sprite,
        .dependencies = {"transform"},
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            SpriteInspectorDrawer::draw(em, entity, {
                context.renderer,
                context.asset_manager,
                context.command_history,
                context.transactions
            });
        },
        .create_default = [](
            EntityManager&,
            EntityID,
            const EditorInspectorContext& context,
            ComponentSnapshotSet& snapshot
        ) {
            Texture* texture = context.renderer && context.asset_manager
                ? context.asset_manager->get_white_texture(context.renderer)
                : nullptr;
            snapshot.set(SpriteComponent{texture, glm::vec2(64.0f, 64.0f)});
        }
    });
    register_or_throw(registry, {
        .component_key = "render_effect",
        .drawer_key = "RenderEffectHeader",
        .display_name = "Render Effect",
        .category = EditorComponentCategory::Rendering,
        .icon = EditorIcon::Zap,
        .dependencies = {"transform"},
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            RenderEffectInspectorDrawer::draw(em, entity, context.command_history);
        },
        .create_default = [](
            EntityManager&,
            EntityID,
            const EditorInspectorContext&,
            ComponentSnapshotSet& snapshot
        ) { snapshot.set(RenderEffectComponent{}); }
    });
    register_or_throw(registry, {
        .component_key = "light_2d",
        .drawer_key = "Light2DHeader",
        .display_name = "Light 2D",
        .category = EditorComponentCategory::Rendering,
        .icon = EditorIcon::Zap,
        .dependencies = {"transform"},
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            LightingInspectorDrawer::draw_light(em, entity, {
                context.command_history, context.transactions
            });
        },
        .create_default = [](
            EntityManager&,
            EntityID,
            const EditorInspectorContext&,
            ComponentSnapshotSet& snapshot
        ) { snapshot.set(Light2DComponent{}); }
    });
    register_or_throw(registry, {
        .component_key = "shadow_caster_2d",
        .drawer_key = "ShadowCaster2DHeader",
        .display_name = "Shadow Caster 2D",
        .category = EditorComponentCategory::Rendering,
        .icon = EditorIcon::Component,
        .dependencies = {"transform"},
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            LightingInspectorDrawer::draw_shadow_caster(em, entity, {
                context.command_history, context.transactions
            });
        },
        .create_default = [](
            EntityManager& em,
            EntityID entity,
            const EditorInspectorContext&,
            ComponentSnapshotSet& snapshot
        ) {
            ShadowCaster2DComponent caster;
            caster.source = em.has_component<ColliderComponent>(entity)
                ? ShadowGeometrySource::Collider
                : ShadowGeometrySource::StaticPolygon;
            snapshot.set(std::move(caster));
        }
    });
    register_or_throw(registry, {
        .component_key = "rigidbody",
        .drawer_key = "RigidBodyHeader",
        .display_name = "RigidBody 2D",
        .category = EditorComponentCategory::Physics,
        .icon = EditorIcon::Physics,
        .dependencies = {"transform"},
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            RigidBodyInspectorDrawer::draw(em, entity, {
                context.command_history, context.transactions
            });
        },
        .create_default = [](
            EntityManager&,
            EntityID,
            const EditorInspectorContext&,
            ComponentSnapshotSet& snapshot
        ) { snapshot.set(RigidBodyComponent::Dynamic()); }
    });
    register_or_throw(registry, {
        .component_key = "collider",
        .drawer_key = "ColliderHeader",
        .display_name = "Collider 2D",
        .category = EditorComponentCategory::Physics,
        .icon = EditorIcon::Collider,
        .dependencies = {"transform"},
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            ColliderInspectorDrawer::draw(em, entity, {
                context.command_history, context.transactions
            });
        },
        .create_default = [](
            EntityManager&,
            EntityID,
            const EditorInspectorContext&,
            ComponentSnapshotSet& snapshot
        ) { snapshot.set(ColliderComponent::Box(glm::vec2(64.0f, 64.0f))); }
    });
    register_or_throw(registry, {
        .component_key = "tilemap",
        .drawer_key = "TilemapHeader",
        .display_name = "Tilemap Grid",
        .category = EditorComponentCategory::World,
        .icon = EditorIcon::Grid,
        .dependencies = {"transform"},
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            TilemapInspectorDrawer::draw(em, entity, {
                context.renderer,
                context.asset_manager,
                context.command_history,
                context.transactions
            });
        },
        .create_default = [](
            EntityManager&,
            EntityID,
            const EditorInspectorContext&,
            ComponentSnapshotSet& snapshot
        ) { snapshot.set(TilemapComponent{}); }
    });
    register_or_throw(registry, {
        .component_key = "camera",
        .drawer_key = "CameraHeader",
        .display_name = "Camera 2D",
        .category = EditorComponentCategory::Rendering,
        .icon = EditorIcon::Camera,
        .dependencies = {"transform"},
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            CameraInspectorDrawer::draw(em, entity, {
                context.command_history, context.transactions
            });
        },
        .create_default = [](
            EntityManager& em,
            EntityID,
            const EditorInspectorContext&,
            ComponentSnapshotSet& snapshot
        ) {
            CameraComponent camera;
            camera.is_primary = !CameraSystem::has_camera(em);
            snapshot.set(camera);
        }
    });
    register_or_throw(registry, {
        .component_key = "animation",
        .drawer_key = "AnimationHeader",
        .display_name = "Sprite Animator 2D",
        .category = EditorComponentCategory::Animation,
        .icon = EditorIcon::Animation,
        .dependencies = {"transform"},
        .draw = [](EntityManager& em, EntityID entity, const EditorInspectorContext& context) {
            AnimationInspectorDrawer::draw(em, entity, {
                context.command_history,
                context.transactions,
                context.animation_preview,
                context.animation_studio
            });
        },
        .create_default = [](
            EntityManager&,
            EntityID,
            const EditorInspectorContext&,
            ComponentSnapshotSet& snapshot
        ) {
            AnimationComponent animation;
            animation.columns = 8;
            animation.rows = 3;
            animation.frame_width = 96.0f;
            animation.frame_height = 96.0f;
            animation.add_clip("Idle", 0, 8, 8.0f, true);
            animation.add_clip("Run", 8, 6, 11.0f, true);
            animation.add_clip("Jump", 16, 4, 8.0f, false);
            animation.add_clip("Fall", 20, 4, 8.0f, true);
            snapshot.set(std::move(animation));
        }
    });
}

} // namespace

EditorComponentRegistry& default_editor_component_registry() {
    static EditorComponentRegistry registry = [] {
        EditorComponentRegistry value;
        register_builtin_editor_components(value);
        std::string error;
        if (!value.validate(default_component_registry(), error)) {
            throw std::runtime_error(error);
        }
        return value;
    }();
    return registry;
}

} // namespace Engine
