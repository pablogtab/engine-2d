#include "TilemapInspectorDrawer.hpp"
#include "AssetDropTarget.hpp"
#include "InspectorWidgets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../commands/PrefabCommands.hpp"
#include "../../core/AssetManager.hpp"
#include "../../tilemap/TilemapColliderSystem.hpp"
#include "../../scene/PrefabOverrides.hpp"
#include <imgui.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

namespace Engine {

namespace {

bool authored_tilemap_equal(const TilemapComponent& lhs, const TilemapComponent& rhs) {
    return lhs.tile_size == rhs.tile_size &&
        lhs.tile_pixel_size == rhs.tile_pixel_size &&
        lhs.tileset_path == rhs.tileset_path &&
        lhs.tileset_cols == rhs.tileset_cols &&
        lhs.tileset_rows == rhs.tileset_rows &&
        lhs.tiles == rhs.tiles &&
        lhs.solid_tile_ids == rhs.solid_tile_ids &&
        lhs.generate_colliders == rhs.generate_colliders &&
        lhs.is_trigger == rhs.is_trigger &&
        lhs.is_one_way == rhs.is_one_way &&
        lhs.category_bits == rhs.category_bits &&
        lhs.mask_bits == rhs.mask_bits &&
        lhs.friction == rhs.friction &&
        lhs.restitution == rhs.restitution &&
        lhs.render_layer == rhs.render_layer;
}

void rebuild_colliders(EntityManager& em, EntityID entity) {
    if (em.has_component<TilemapComponent>(entity)) {
        TilemapColliderSystem::build_composite_colliders(em, entity);
    } else {
        TilemapColliderSystem::clear_composite_colliders(em, entity);
    }
}

void reload_tileset(
    EntityManager& em,
    EntityID entity,
    SDL_Renderer* renderer,
    AssetManager* asset_manager
) {
    if (!em.has_component<TilemapComponent>(entity)) return;
    auto& tilemap = em.get_component<TilemapComponent>(entity);
    tilemap.tileset_texture = nullptr;
    if (!renderer || !asset_manager || tilemap.tileset_path.empty()) return;
    if (auto texture = asset_manager->load_texture(renderer, tilemap.tileset_path)) {
        tilemap.tileset_texture = texture.get();
    }
}

void track(
    EntityManager& em,
    EntityID entity,
    const TilemapComponent& before,
    const char* property,
    const char* description,
    const TilemapInspectorDrawer::Context& context,
    std::function<void(EntityManager&, EntityID)> side_effect = {},
    bool force_commit = false
) {
    track_component_edit<TilemapComponent>(
        em,
        entity,
        before,
        property,
        description,
        {context.command_history, context.transactions},
        authored_tilemap_equal,
        std::move(side_effect),
        force_commit
    );
}

} // namespace

void TilemapInspectorDrawer::draw(EntityManager& em, EntityID entity, const Context& context) {
    if (!em.has_component<TilemapComponent>(entity)) return;

    const bool has_override = PrefabOverrides::is_component_overridden(em, entity, "tilemap");
    const auto header = InspectorWidgets::registered_component_header("tilemap", true, true, has_override);
    if (header.revert_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<RevertComponentPrefabCommand>(
            &em, entity, "tilemap", context.renderer
        ));
        return;
    }
    if (header.apply_override_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<ApplyComponentPrefabCommand>(
            &em, entity, "tilemap", context.renderer
        ));
        return;
    }
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<TilemapComponent>>(
            &em,
            entity,
            em.get_component<TilemapComponent>(entity),
            std::nullopt,
            "Remove Tilemap Grid",
            rebuild_colliders
        ));
        return;
    }
    if (!header.open) return;


    using namespace InspectorWidgets;
    auto& tilemap = em.get_component<TilemapComponent>(entity);

    ImGui::TextColored(
        ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
        "Tile Count: %zu painted tiles",
        tilemap.tile_count()
    );
    if (!tilemap.empty()) {
        int min_x = 0;
        int min_y = 0;
        int max_x = 0;
        int max_y = 0;
        tilemap.get_bounds(min_x, min_y, max_x, max_y);
        ImGui::TextDisabled(
            "Bounds: [%d, %d] to [%d, %d] (%dx%d)",
            min_x,
            min_y,
            max_x,
            max_y,
            max_x - min_x + 1,
            max_y - min_y + 1
        );
    } else {
        ImGui::TextDisabled("Map is empty. Paint tiles in the Viewport to expand it.");
    }

    const TilemapComponent tile_size_before = tilemap;
    property_float("Tile Size", &tilemap.tile_size, InspectorPropertyPolicies::Tilemap::TileSize);
    track(
        em,
        entity,
        tile_size_before,
        "Tilemap.TileSize",
        "Edit Tilemap Tile Size",
        context,
        rebuild_colliders
    );

    const TilemapComponent pixel_size_before = tilemap;
    property_int("Pixel Size", &tilemap.tile_pixel_size, InspectorPropertyPolicies::Tilemap::PixelSize);
    track(em, entity, pixel_size_before, "Tilemap.PixelSize", "Edit Tilemap Pixel Size", context);

    const TilemapComponent render_layer_before = tilemap;
    int render_layer = tilemap.render_layer;
    if (property_int("Render Layer", &render_layer, InspectorPropertyPolicies::Tilemap::RenderLayer)) {
        tilemap.render_layer = static_cast<RenderLayer>(render_layer);
    }
    track(em, entity, render_layer_before, "Tilemap.RenderLayer", "Edit Tilemap Render Layer", context);

    char path_buffer[512];
    std::strncpy(path_buffer, tilemap.tileset_path.c_str(), sizeof(path_buffer) - 1);
    path_buffer[sizeof(path_buffer) - 1] = '\0';
    const TilemapComponent path_before = tilemap;
    begin_property_row("Tileset");
    begin_property_interaction("Tilemap.Tileset");
    bool path_submitted = ImGui::InputText(
        "##tileset",
        path_buffer,
        sizeof(path_buffer),
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    if (const auto dropped = accept_asset_drop_target(
            AssetDatabase::get(), AssetType::Texture
        )) {
        if (const AssetRecord* record = AssetDatabase::get().find(*dropped)) {
            std::snprintf(path_buffer, sizeof(path_buffer), "%s", record->source_path.c_str());
            path_submitted = tilemap.tileset_path != path_buffer;
        }
    }
    capture_property_interaction();
    if (path_submitted) tilemap.tileset_path = path_buffer;
    track(
        em,
        entity,
        path_before,
        "Tilemap.Tileset",
        "Change Tilemap Tileset",
        context,
        [renderer = context.renderer, asset_manager = context.asset_manager](EntityManager& command_em, EntityID id) {
            reload_tileset(command_em, id, renderer, asset_manager);
        },
        path_submitted
    );

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("COLLIDER AND PHYSICS");

    const TilemapComponent generate_before = tilemap;
    property_bool("Generate Colliders", &tilemap.generate_colliders);
    track(
        em,
        entity,
        generate_before,
        "Tilemap.GenerateColliders",
        "Toggle Tilemap Colliders",
        context,
        rebuild_colliders
    );

    if (tilemap.generate_colliders) {
        const TilemapComponent trigger_before = tilemap;
        property_bool("Is Trigger", &tilemap.is_trigger);
        track(em, entity, trigger_before, "Tilemap.IsTrigger", "Toggle Tilemap Trigger", context, rebuild_colliders);

        const TilemapComponent one_way_before = tilemap;
        property_bool("Is One-Way", &tilemap.is_one_way);
        track(em, entity, one_way_before, "Tilemap.IsOneWay", "Toggle One-Way Tilemap", context, rebuild_colliders);

        const TilemapComponent friction_before = tilemap;
        property_float("Friction", &tilemap.friction, InspectorPropertyPolicies::Tilemap::Friction);
        track(em, entity, friction_before, "Tilemap.Friction", "Edit Tilemap Friction", context, rebuild_colliders);

        const TilemapComponent restitution_before = tilemap;
        property_float(
            "Restitution", &tilemap.restitution, InspectorPropertyPolicies::Tilemap::Restitution
        );
        track(
            em,
            entity,
            restitution_before,
            "Tilemap.Restitution",
            "Edit Tilemap Restitution",
            context,
            rebuild_colliders
        );

        int category_bits = tilemap.category_bits;
        const TilemapComponent category_before = tilemap;
        if (property_int(
                "Category Bits", &category_bits, InspectorPropertyPolicies::Tilemap::CollisionBits
            )) {
            tilemap.category_bits = static_cast<std::uint16_t>(category_bits);
        }
        track(
            em,
            entity,
            category_before,
            "Tilemap.CategoryBits",
            "Edit Tilemap Collision Category",
            context,
            rebuild_colliders
        );

        int mask_bits = tilemap.mask_bits;
        const TilemapComponent mask_before = tilemap;
        if (property_int("Mask Bits", &mask_bits, InspectorPropertyPolicies::Tilemap::CollisionBits)) {
            tilemap.mask_bits = static_cast<std::uint16_t>(mask_bits);
        }
        track(
            em,
            entity,
            mask_before,
            "Tilemap.MaskBits",
            "Edit Tilemap Collision Mask",
            context,
            rebuild_colliders
        );
    }

    ImGui::Spacing();
    const std::string rebuild_label = editor_icon_label(EditorIcon::Zap, "Rebuild Colliders");
    if (ImGui::Button(rebuild_label.c_str(), ImVec2(-1.0f, 0.0f))) {
        rebuild_colliders(em, entity);
    }

    const std::string clear_label = editor_icon_label(EditorIcon::Trash, "Clear Tilemap");
    if (ImGui::Button(clear_label.c_str(), ImVec2(-1.0f, 0.0f)) && !tilemap.empty()) {
        const TilemapComponent before = tilemap;
        tilemap.clear();
        track(
            em,
            entity,
            before,
            "Tilemap.Clear",
            "Clear Tilemap",
            context,
            rebuild_colliders,
            true
        );
    }
}

} // namespace Engine
