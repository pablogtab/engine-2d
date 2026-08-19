#include "SpriteInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "../commands/EntityCommands.hpp"
#include "../../core/AssetManager.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../renderer/Texture.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>

namespace Engine {

namespace {

bool sprite_states_equal(const SpriteComponent& lhs, const SpriteComponent& rhs) {
    return lhs.texture == rhs.texture &&
        lhs.color.r == rhs.color.r && lhs.color.g == rhs.color.g &&
        lhs.color.b == rhs.color.b && lhs.color.a == rhs.color.a &&
        lhs.src_rect.x == rhs.src_rect.x && lhs.src_rect.y == rhs.src_rect.y &&
        lhs.src_rect.w == rhs.src_rect.w && lhs.src_rect.h == rhs.src_rect.h &&
        lhs.use_src_rect == rhs.use_src_rect &&
        lhs.visible == rhs.visible &&
        lhs.size.x == rhs.size.x && lhs.size.y == rhs.size.y &&
        lhs.material == rhs.material &&
        lhs.has_outline == rhs.has_outline &&
        lhs.outline_color.r == rhs.outline_color.r &&
        lhs.outline_color.g == rhs.outline_color.g &&
        lhs.outline_color.b == rhs.outline_color.b &&
        lhs.outline_color.a == rhs.outline_color.a &&
        lhs.outline_thickness == rhs.outline_thickness &&
        lhs.outline_pulse == rhs.outline_pulse &&
        lhs.flip_x == rhs.flip_x && lhs.flip_y == rhs.flip_y &&
        lhs.sorting_layer == rhs.sorting_layer &&
        lhs.order_in_layer == rhs.order_in_layer;
}

} // namespace

void SpriteInspectorDrawer::track_edit(
    EntityManager& em,
    EntityID entity,
    const SpriteComponent& before,
    const char* property,
    const char* description,
    const Context& context,
    bool force_commit
) {
    if (!context.command_history || !context.transactions) return;

    const auto& interaction = InspectorWidgets::last_property_interaction();
    const auto key = EditorPropertyTransaction::make_key(entity, property);
    if (interaction.activated || (force_commit && !context.transactions->contains(key))) {
        context.transactions->begin<SpriteComponent>(
            key,
            before,
            [&em, entity]() -> std::optional<SpriteComponent> {
                if (!em.is_alive(entity) || !em.has_component<SpriteComponent>(entity)) return std::nullopt;
                return em.get_component<SpriteComponent>(entity);
            },
            [&em, entity](const SpriteComponent& value) {
                if (em.is_alive(entity) && em.has_component<SpriteComponent>(entity)) {
                    em.get_component<SpriteComponent>(entity) = value;
                }
            },
            description,
            sprite_states_equal
        );
    }

    if (interaction.cancelled) {
        context.transactions->cancel(key);
    } else if (force_commit || interaction.deactivated_after_edit) {
        context.transactions->commit(key, *context.command_history);
    }
}

void SpriteInspectorDrawer::draw(EntityManager& em, EntityID entity, const Context& context) {
    if (!em.has_component<SpriteComponent>(entity)) return;

    const auto header = InspectorWidgets::component_header(
        "Sprite Renderer", "SpriteRendererHeader", true, true, EditorIcon::Sprite
    );
    if (header.remove_requested && context.command_history) {
        context.command_history->execute_command(std::make_unique<SetComponentCommand<SpriteComponent>>(
            &em,
            entity,
            em.get_component<SpriteComponent>(entity),
            std::nullopt,
            "Remove Sprite Renderer"
        ));
        return;
    }
    if (!header.open) return;

    using namespace InspectorWidgets;
    auto& sprite = em.get_component<SpriteComponent>(entity);

    std::string texture_path = sprite.texture ? sprite.texture->get_filepath() : "";
    char texture_buffer[256];
    std::strncpy(texture_buffer, texture_path.c_str(), sizeof(texture_buffer) - 1);
    texture_buffer[sizeof(texture_buffer) - 1] = '\0';

    const SpriteComponent texture_before = sprite;
    begin_property_interaction("Sprite.Texture");
    const bool texture_submitted = ImGui::InputText(
        "Texture",
        texture_buffer,
        sizeof(texture_buffer),
        ImGuiInputTextFlags_EnterReturnsTrue
    );
    capture_property_interaction();
    if (texture_submitted && context.renderer && context.asset_manager) {
        if (std::strlen(texture_buffer) == 0) {
            sprite.texture = context.asset_manager->get_white_texture(context.renderer);
        } else if (auto texture = context.asset_manager->load_texture(context.renderer, texture_buffer)) {
            sprite.texture = texture.get();
        }
    }
    track_edit(
        em,
        entity,
        texture_before,
        "Sprite.Texture",
        "Change Sprite Texture",
        context,
        texture_submitted
    );

    Texture* preview_texture = sprite.texture;
    if (!preview_texture && context.renderer && context.asset_manager) {
        preview_texture = context.asset_manager->get_white_texture(context.renderer);
    }
    if (preview_texture && preview_texture->get_sdl_texture()) {
        constexpr float max_preview_size = 112.0f;
        const float texture_width = static_cast<float>(std::max(1, preview_texture->get_width()));
        const float texture_height = static_cast<float>(std::max(1, preview_texture->get_height()));
        const float scale = std::min(max_preview_size / texture_width, max_preview_size / texture_height);
        const ImVec2 preview_size(texture_width * scale, texture_height * scale);

        begin_property_row("Preview");
        ImGui::Image(preview_texture->get_sdl_texture(), preview_size);
        ImGui::SameLine();
        ImGui::TextDisabled("%d x %d", preview_texture->get_width(), preview_texture->get_height());
    }

    const SpriteComponent size_before = sprite;
    float size[2] = {sprite.size.x, sprite.size.y};
    if (property_vec2("Size", size, InspectorPropertyPolicies::Sprite::Size)) {
        sprite.size = glm::vec2(size[0], size[1]);
    }
    track_edit(em, entity, size_before, "Sprite.Size", "Edit Sprite Size", context);

    const SpriteComponent tint_before = sprite;
    float color[3] = {
        sprite.color.r / 255.0f,
        sprite.color.g / 255.0f,
        sprite.color.b / 255.0f
    };
    begin_property_interaction("Sprite.Tint");
    if (ImGui::ColorEdit3("Tint Color", color)) {
        sprite.color.r = static_cast<std::uint8_t>(color[0] * 255.0f);
        sprite.color.g = static_cast<std::uint8_t>(color[1] * 255.0f);
        sprite.color.b = static_cast<std::uint8_t>(color[2] * 255.0f);
    }
    capture_property_interaction();
    track_edit(em, entity, tint_before, "Sprite.Tint", "Edit Sprite Tint", context);

    const SpriteComponent opacity_before = sprite;
    float opacity = sprite.color.a / 255.0f * 100.0f;
    if (property_float("Opacity", &opacity, InspectorPropertyPolicies::Sprite::Opacity)) {
        sprite.color.a = static_cast<std::uint8_t>(opacity / 100.0f * 255.0f);
    }
    track_edit(em, entity, opacity_before, "Sprite.Opacity", "Edit Sprite Opacity", context);

    const SpriteComponent sorting_before = sprite;
    int sorting_layer = sprite.sorting_layer;
    if (property_int("Sorting Layer", &sorting_layer, InspectorPropertyPolicies::Sprite::SortingOrder)) {
        sprite.sorting_layer = static_cast<std::int16_t>(sorting_layer);
    }
    track_edit(em, entity, sorting_before, "Sprite.SortingLayer", "Edit Sprite Sorting Layer", context);

    const SpriteComponent order_before = sprite;
    int order_in_layer = sprite.order_in_layer;
    if (property_int("Order in Layer", &order_in_layer, InspectorPropertyPolicies::Sprite::SortingOrder)) {
        sprite.order_in_layer = static_cast<std::int16_t>(order_in_layer);
    }
    track_edit(em, entity, order_before, "Sprite.OrderInLayer", "Edit Sprite Order in Layer", context);

    begin_property_row("Flags");
    const SpriteComponent flip_x_before = sprite;
    begin_property_interaction("Sprite.FlipX");
    ImGui::Checkbox("Flip X", &sprite.flip_x);
    capture_property_interaction();
    track_edit(em, entity, flip_x_before, "Sprite.FlipX", "Toggle Sprite Flip X", context);
    ImGui::SameLine();

    const SpriteComponent flip_y_before = sprite;
    begin_property_interaction("Sprite.FlipY");
    ImGui::Checkbox("Flip Y", &sprite.flip_y);
    capture_property_interaction();
    track_edit(em, entity, flip_y_before, "Sprite.FlipY", "Toggle Sprite Flip Y", context);
    ImGui::SameLine();

    const SpriteComponent visible_before = sprite;
    begin_property_interaction("Sprite.Visible");
    ImGui::Checkbox("Visible##sprite", &sprite.visible);
    capture_property_interaction();
    track_edit(em, entity, visible_before, "Sprite.Visible", "Toggle Sprite Visibility", context);
}

} // namespace Engine
