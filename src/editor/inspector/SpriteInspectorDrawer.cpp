#include "SpriteInspectorDrawer.hpp"
#include "InspectorWidgets.hpp"
#include "InspectorPropertyPolicies.hpp"
#include "MultiComponentInspectorTransaction.hpp"
#include "../commands/EntityCommands.hpp"
#include "../../core/AssetManager.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../renderer/Texture.hpp"
#include "../../renderer/material/Material.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <imgui_internal.h>
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

void draw_material_value(const SelectedComponentStates<SpriteComponent>& states) {
    InspectorWidgets::begin_property_row("Material");
    const bool mixed = selected_values_mixed<SpriteComponent, std::shared_ptr<const Material>>(
        states, [](const SpriteComponent& sprite) { return sprite.material; }
    );
    if (mixed) {
        ImGui::TextDisabled("Multiple values");
    } else if (states.front().second.material) {
        ImGui::TextUnformatted(states.front().second.material->name().c_str());
    } else {
        ImGui::TextDisabled("None");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Read-only until materials have stable asset identities.");
    }
}

void draw_material_value(const SpriteComponent& sprite) {
    SelectedComponentStates<SpriteComponent> state{{0, sprite}};
    draw_material_value(state);
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

    draw_material_value(sprite);

    const SpriteComponent outline_enabled_before = sprite;
    property_bool("Outline", &sprite.has_outline);
    track_edit(em, entity, outline_enabled_before, "Sprite.Outline", "Toggle Sprite Outline", context);

    if (sprite.has_outline) {
        const SpriteComponent outline_color_before = sprite;
        float outline_color[4] = {
            sprite.outline_color.r / 255.0f,
            sprite.outline_color.g / 255.0f,
            sprite.outline_color.b / 255.0f,
            sprite.outline_color.a / 255.0f
        };
        begin_property_interaction("Sprite.OutlineColor");
        if (ImGui::ColorEdit4("Outline Color", outline_color)) {
            sprite.outline_color = {
                static_cast<std::uint8_t>(outline_color[0] * 255.0f),
                static_cast<std::uint8_t>(outline_color[1] * 255.0f),
                static_cast<std::uint8_t>(outline_color[2] * 255.0f),
                static_cast<std::uint8_t>(outline_color[3] * 255.0f)
            };
        }
        capture_property_interaction();
        track_edit(em, entity, outline_color_before, "Sprite.OutlineColor", "Edit Sprite Outline Color", context);

        const SpriteComponent outline_thickness_before = sprite;
        property_float("Thickness", &sprite.outline_thickness, InspectorPropertyPolicies::Sprite::OutlineThickness);
        track_edit(em, entity, outline_thickness_before, "Sprite.OutlineThickness", "Edit Sprite Outline Thickness", context);

        const SpriteComponent outline_pulse_before = sprite;
        property_bool("Pulse", &sprite.outline_pulse);
        track_edit(em, entity, outline_pulse_before, "Sprite.OutlinePulse", "Toggle Sprite Outline Pulse", context);
    }

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

void SpriteInspectorDrawer::draw_multiple(
    EntityManager& em,
    const std::vector<EntityID>& entities,
    const Context& context
) {
    auto states = capture_selected_components<SpriteComponent>(em, entities);
    if (states.size() != entities.size()) return;
    if (!InspectorWidgets::component_header(
            "Sprite Renderer", "MultiSpriteRendererHeader", false, true, EditorIcon::Sprite
        ).open) return;

    using namespace InspectorWidgets;
    const ComponentTransactionContext transaction_context{context.command_history, context.transactions};
    const auto apply_all = [&](auto setter) {
        for (const EntityID entity : entities) setter(em.get_component<SpriteComponent>(entity));
    };
    const auto track = [&](const auto& before, const char* property, const char* description, bool force_commit = false) {
        track_selected_component_edit<SpriteComponent>(
            em, entities, before, property, description, transaction_context,
            sprite_states_equal, {}, force_commit
        );
    };

    {
        const auto before = capture_selected_components<SpriteComponent>(em, entities);
        const bool mixed = selected_values_mixed<SpriteComponent, Texture*>(
            before, [](const SpriteComponent& sprite) { return sprite.texture; }
        );
        std::string texture_path = mixed || !before.front().second.texture
            ? ""
            : before.front().second.texture->get_filepath();
        char texture_buffer[256];
        std::strncpy(texture_buffer, texture_path.c_str(), sizeof(texture_buffer) - 1);
        texture_buffer[sizeof(texture_buffer) - 1] = '\0';
        begin_property_interaction("MultiSprite.Texture");
        const bool submitted = ImGui::InputTextWithHint(
            "Texture", mixed ? "Multiple values" : "Texture path",
            texture_buffer, sizeof(texture_buffer), ImGuiInputTextFlags_EnterReturnsTrue
        );
        capture_property_interaction();
        if (submitted && context.renderer && context.asset_manager) {
            Texture* texture = nullptr;
            bool valid = false;
            if (std::strlen(texture_buffer) == 0) {
                texture = context.asset_manager->get_white_texture(context.renderer);
                valid = true;
            } else if (auto loaded = context.asset_manager->load_texture(context.renderer, texture_buffer)) {
                texture = loaded.get();
                valid = true;
            }
            if (valid) apply_all([&](SpriteComponent& sprite) { sprite.texture = texture; });
        }
        track(before, "Sprite.Texture", "Change Selected Sprite Textures", submitted);

        const auto current = capture_selected_components<SpriteComponent>(em, entities);
        const bool current_mixed = selected_values_mixed<SpriteComponent, Texture*>(
            current, [](const SpriteComponent& sprite) { return sprite.texture; }
        );
        if (current_mixed) {
            begin_property_row("Preview");
            ImGui::TextDisabled("Multiple textures");
        } else {
            Texture* preview = current.front().second.texture;
            if (!preview && context.renderer && context.asset_manager) {
                preview = context.asset_manager->get_white_texture(context.renderer);
            }
            if (preview && preview->get_sdl_texture()) {
                constexpr float max_preview_size = 96.0f;
                const float width = static_cast<float>(std::max(1, preview->get_width()));
                const float height = static_cast<float>(std::max(1, preview->get_height()));
                const float scale = std::min(max_preview_size / width, max_preview_size / height);
                begin_property_row("Preview");
                ImGui::Image(preview->get_sdl_texture(), ImVec2(width * scale, height * scale));
                ImGui::SameLine();
                ImGui::TextDisabled("%d x %d", preview->get_width(), preview->get_height());
            }
        }
    }

    {
        const auto before = capture_selected_components<SpriteComponent>(em, entities);
        float size[2] = {before.front().second.size.x, before.front().second.size.y};
        const bool mixed_x = selected_values_mixed<SpriteComponent, float>(before, [](const auto& sprite) { return sprite.size.x; });
        const bool mixed_y = selected_values_mixed<SpriteComponent, float>(before, [](const auto& sprite) { return sprite.size.y; });
        bool changed_x = false;
        bool changed_y = false;
        if (property_vec2("Size", size, InspectorPropertyPolicies::Sprite::Size, false, mixed_x, mixed_y, &changed_x, &changed_y)) {
            apply_all([&](SpriteComponent& sprite) {
                if (changed_x) sprite.size.x = size[0];
                if (changed_y) sprite.size.y = size[1];
            });
        }
        track(before, "Sprite.Size", "Edit Selected Sprite Sizes");
    }

    {
        const auto before = capture_selected_components<SpriteComponent>(em, entities);
        const SDL_Color first = before.front().second.color;
        const bool mixed = selected_values_mixed<SpriteComponent, SDL_Color>(before,
            [](const SpriteComponent& sprite) { return sprite.color; },
            [](const SDL_Color& lhs, const SDL_Color& rhs) {
                return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
            }
        );
        float color[3] = {first.r / 255.0f, first.g / 255.0f, first.b / 255.0f};
        begin_property_interaction("MultiSprite.Tint");
        if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        const bool changed = ImGui::ColorEdit3("Tint Color", color);
        if (mixed) ImGui::PopItemFlag();
        capture_property_interaction();
        if (changed) apply_all([&](SpriteComponent& sprite) {
            sprite.color.r = static_cast<std::uint8_t>(color[0] * 255.0f);
            sprite.color.g = static_cast<std::uint8_t>(color[1] * 255.0f);
            sprite.color.b = static_cast<std::uint8_t>(color[2] * 255.0f);
        });
        track(before, "Sprite.Tint", "Edit Selected Sprite Tint");
    }

    {
        const auto before = capture_selected_components<SpriteComponent>(em, entities);
        float opacity = before.front().second.color.a / 255.0f * 100.0f;
        const bool mixed = selected_values_mixed<SpriteComponent, std::uint8_t>(before, [](const auto& sprite) { return sprite.color.a; });
        if (property_float("Opacity", &opacity, InspectorPropertyPolicies::Sprite::Opacity, mixed)) {
            const auto alpha = static_cast<std::uint8_t>(opacity / 100.0f * 255.0f);
            apply_all([&](SpriteComponent& sprite) { sprite.color.a = alpha; });
        }
        track(before, "Sprite.Opacity", "Edit Selected Sprite Opacity");
    }

    draw_material_value(capture_selected_components<SpriteComponent>(em, entities));

    {
        const auto before = capture_selected_components<SpriteComponent>(em, entities);
        bool enabled = before.front().second.has_outline;
        const bool mixed = selected_values_mixed<SpriteComponent, bool>(before, [](const auto& sprite) { return sprite.has_outline; });
        if (property_bool("Outline", &enabled, mixed)) {
            apply_all([&](SpriteComponent& sprite) { sprite.has_outline = enabled; });
        }
        track(before, "Sprite.Outline", "Toggle Selected Sprite Outlines");
    }

    const auto outline_states = capture_selected_components<SpriteComponent>(em, entities);
    const bool any_outline = std::any_of(outline_states.begin(), outline_states.end(), [](const auto& state) {
        return state.second.has_outline;
    });
    if (any_outline) {
        {
            const auto before = capture_selected_components<SpriteComponent>(em, entities);
            const SDL_Color first = before.front().second.outline_color;
            const bool mixed = selected_values_mixed<SpriteComponent, SDL_Color>(before,
                [](const SpriteComponent& sprite) { return sprite.outline_color; },
                [](const SDL_Color& lhs, const SDL_Color& rhs) {
                    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
                }
            );
            float color[4] = {first.r / 255.0f, first.g / 255.0f, first.b / 255.0f, first.a / 255.0f};
            begin_property_interaction("MultiSprite.OutlineColor");
            if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            const bool changed = ImGui::ColorEdit4("Outline Color", color);
            if (mixed) ImGui::PopItemFlag();
            capture_property_interaction();
            if (changed) apply_all([&](SpriteComponent& sprite) {
                sprite.outline_color = {
                    static_cast<std::uint8_t>(color[0] * 255.0f),
                    static_cast<std::uint8_t>(color[1] * 255.0f),
                    static_cast<std::uint8_t>(color[2] * 255.0f),
                    static_cast<std::uint8_t>(color[3] * 255.0f)
                };
            });
            track(before, "Sprite.OutlineColor", "Edit Selected Sprite Outline Color");
        }
        {
            const auto before = capture_selected_components<SpriteComponent>(em, entities);
            float thickness = before.front().second.outline_thickness;
            const bool mixed = selected_values_mixed<SpriteComponent, float>(before, [](const auto& sprite) { return sprite.outline_thickness; });
            if (property_float("Thickness", &thickness, InspectorPropertyPolicies::Sprite::OutlineThickness, mixed)) {
                apply_all([&](SpriteComponent& sprite) { sprite.outline_thickness = thickness; });
            }
            track(before, "Sprite.OutlineThickness", "Edit Selected Sprite Outline Thickness");
        }
        {
            const auto before = capture_selected_components<SpriteComponent>(em, entities);
            bool pulse = before.front().second.outline_pulse;
            const bool mixed = selected_values_mixed<SpriteComponent, bool>(before, [](const auto& sprite) { return sprite.outline_pulse; });
            if (property_bool("Pulse", &pulse, mixed)) {
                apply_all([&](SpriteComponent& sprite) { sprite.outline_pulse = pulse; });
            }
            track(before, "Sprite.OutlinePulse", "Toggle Selected Sprite Outline Pulse");
        }
    }

    auto draw_int = [&](const char* label, const char* property, const char* description, auto getter, auto setter) {
        const auto before = capture_selected_components<SpriteComponent>(em, entities);
        int value = getter(before.front().second);
        const bool mixed = selected_values_mixed<SpriteComponent, int>(before, [&](const auto& sprite) { return getter(sprite); });
        if (property_int(label, &value, InspectorPropertyPolicies::Sprite::SortingOrder, mixed)) {
            apply_all([&](SpriteComponent& sprite) { setter(sprite, value); });
        }
        track(before, property, description);
    };
    draw_int("Sorting Layer", "Sprite.SortingLayer", "Edit Selected Sprite Sorting Layer",
        [](const SpriteComponent& sprite) { return static_cast<int>(sprite.sorting_layer); },
        [](SpriteComponent& sprite, int value) { sprite.sorting_layer = static_cast<std::int16_t>(value); });
    draw_int("Order in Layer", "Sprite.OrderInLayer", "Edit Selected Sprite Order in Layer",
        [](const SpriteComponent& sprite) { return static_cast<int>(sprite.order_in_layer); },
        [](SpriteComponent& sprite, int value) { sprite.order_in_layer = static_cast<std::int16_t>(value); });

    auto draw_bool = [&](const char* label, const char* property, const char* description, auto getter, auto setter) {
        const auto before = capture_selected_components<SpriteComponent>(em, entities);
        bool value = getter(before.front().second);
        const bool mixed = selected_values_mixed<SpriteComponent, bool>(before, [&](const auto& sprite) { return getter(sprite); });
        if (property_bool(label, &value, mixed)) {
            apply_all([&](SpriteComponent& sprite) { setter(sprite, value); });
        }
        track(before, property, description);
    };
    draw_bool("Flip X", "Sprite.FlipX", "Toggle Selected Sprite Flip X",
        [](const SpriteComponent& sprite) { return sprite.flip_x; },
        [](SpriteComponent& sprite, bool value) { sprite.flip_x = value; });
    draw_bool("Flip Y", "Sprite.FlipY", "Toggle Selected Sprite Flip Y",
        [](const SpriteComponent& sprite) { return sprite.flip_y; },
        [](SpriteComponent& sprite, bool value) { sprite.flip_y = value; });
    draw_bool("Visible", "Sprite.Visible", "Toggle Selected Sprite Visibility",
        [](const SpriteComponent& sprite) { return sprite.visible; },
        [](SpriteComponent& sprite, bool value) { sprite.visible = value; });
}

} // namespace Engine
