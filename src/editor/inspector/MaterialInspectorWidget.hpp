#pragma once

#include "AssetDropTarget.hpp"
#include "InspectorWidgets.hpp"
#include "../tools/EditorIcons.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../renderer/material/Material.hpp"
#include "../../renderer/material/MaterialAsset.hpp"
#include "../../renderer/material/SDLGPUMaterialBackend.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

struct MaterialInspectorActions {
    std::function<AssetHandle()> create;
    std::function<AssetHandle(AssetHandle)> duplicate;
    std::function<void(AssetHandle)> open;
    std::function<SDL_Texture*(AssetHandle)> preview;
};

inline void draw_material_preview_image(SDL_Texture* texture, ImVec2 size) {
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    constexpr float tile = 6.0f;
    for (float y = 0.0f; y < size.y; y += tile) {
        for (float x = 0.0f; x < size.x; x += tile) {
            const bool alternate = (static_cast<int>(x / tile) + static_cast<int>(y / tile)) % 2 != 0;
            const ImU32 color = alternate ? IM_COL32(52, 56, 68, 255) : IM_COL32(72, 77, 91, 255);
            draw_list->AddRectFilled(
                {minimum.x + x, minimum.y + y},
                {minimum.x + std::min(x + tile, size.x), minimum.y + std::min(y + tile, size.y)},
                color
            );
        }
    }
    ImGui::Image(texture, size);
    draw_list->AddRect(
        minimum, {minimum.x + size.x, minimum.y + size.y},
        IM_COL32(100, 110, 135, 190), 3.0f
    );
}

inline bool draw_material_asset_field(
    const char* label,
    const char* id,
    AssetHandle& handle,
    std::shared_ptr<const Material>& material,
    SDL_Renderer* renderer,
    const MaterialInspectorActions& actions,
    std::size_t reference_count = 0
) {
    AssetDatabase& database = AssetDatabase::get();
    const AssetRecord* selected = handle.is_valid() ? database.find(handle) : nullptr;
    const std::string display_name = selected
        ? std::filesystem::path(selected->source_path).stem().string()
        : "None (Material)";
    bool changed = false;

    const auto assign = [&](AssetHandle candidate_handle) {
        if (!candidate_handle.is_valid()) {
            handle = {};
            material.reset();
            changed = true;
            return;
        }
        std::string compatibility_error;
        const auto candidate = MaterialAsset::load(candidate_handle, database);
        const bool compatible = candidate && (!renderer || SDLGPUMaterialBackend::supports(
            renderer, candidate->shader(), &compatibility_error
        ));
        if (!compatible) return;
        handle = candidate_handle;
        material = candidate;
        changed = true;
    };

    InspectorWidgets::begin_property_row(label);
    ImGui::PushID(id);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float icon_width = ImGui::GetFrameHeight();
    SDL_Texture* selected_preview = handle.is_valid() && actions.preview
        ? actions.preview(handle) : nullptr;
    constexpr float thumbnail_size = 34.0f;
    bool thumbnail_clicked = false;
    bool thumbnail_hovered = false;
    if (selected_preview) {
        const ImVec2 minimum = ImGui::GetCursorScreenPos();
        draw_material_preview_image(selected_preview, {thumbnail_size, thumbnail_size});
        thumbnail_hovered = ImGui::IsItemHovered();
        ImGui::SetCursorScreenPos(minimum);
        thumbnail_clicked = ImGui::InvisibleButton(
            "##material_thumbnail", {thumbnail_size, thumbnail_size}
        );
        thumbnail_hovered |= ImGui::IsItemHovered();
        ImGui::SameLine();
    }
    const float field_width = std::max(
        72.0f, ImGui::GetContentRegionAvail().x - icon_width * 2.0f - spacing * 2.0f
    );
    const bool show_reference_count = selected_preview && reference_count > 0;
    const std::string field_label = selected_preview
        ? "##field"
        : editor_icon_label(EditorIcon::Material, display_name.c_str()) + "##field";
    const bool unassigned = !handle.is_valid();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    if (unassigned) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    }
    const ImVec2 field_minimum = ImGui::GetCursorScreenPos();
    const bool field_clicked = ImGui::Button(
        field_label.c_str(), ImVec2(field_width, selected_preview ? thumbnail_size : 0.0f)
    );
    if (selected_preview) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 field_maximum{
            field_minimum.x + field_width, field_minimum.y + thumbnail_size
        };
        draw_list->PushClipRect(field_minimum, field_maximum, true);
        const float text_x = field_minimum.x + ImGui::GetStyle().FramePadding.x;
        if (show_reference_count) {
            draw_list->AddText(
                {text_x, field_minimum.y + 2.0f},
                ImGui::GetColorU32(ImGuiCol_Text), display_name.c_str()
            );
            const std::string usage = reference_count == 1
                ? "1 scene use" : std::to_string(reference_count) + " scene uses";
            draw_list->AddText(
                {text_x, field_minimum.y + 17.0f},
                ImGui::GetColorU32(ImGuiCol_TextDisabled), usage.c_str()
            );
        } else {
            const float text_y = field_minimum.y + (thumbnail_size - ImGui::GetFontSize()) * 0.5f;
            draw_list->AddText(
                {text_x, text_y}, ImGui::GetColorU32(ImGuiCol_Text), display_name.c_str()
            );
        }
        draw_list->PopClipRect();
    }
    if (unassigned) ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    const bool field_hovered = ImGui::IsItemHovered();
    const bool preview_hovered = field_hovered || thumbnail_hovered;
    const bool open_by_double_click = preview_hovered && handle.is_valid() && actions.open &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    static std::array<char, 128> search{};
    if (open_by_double_click) {
        actions.open(handle);
    } else if (field_clicked || thumbnail_clicked) {
        search.fill('\0');
        ImGui::OpenPopup("##material_picker");
    }
    if (preview_hovered) {
        if (selected) ImGui::SetTooltip("%s\nDouble-click to open", selected->source_path.c_str());
        else ImGui::SetTooltip("Select or drop a Material asset");
    }
    if (const auto dropped = accept_asset_drop_target(database, AssetType::Material)) {
        assign(*dropped);
    }

    ImGui::SameLine();
    const bool clear_disabled = !handle.is_valid();
    if (clear_disabled) ImGui::BeginDisabled();
    if (editor_icon_button(
            "##clear_material", EditorIcon::Clear, "Clear material", false,
            {icon_width, ImGui::GetFrameHeight()}
        )) assign({});
    if (clear_disabled) ImGui::EndDisabled();

    ImGui::SameLine();
    if (editor_icon_button("##material_actions", EditorIcon::Ellipsis, "Material actions", false, {
            icon_width, ImGui::GetFrameHeight()
        })) {
        ImGui::OpenPopup("##material_actions_popup");
    }
    if (ImGui::BeginPopup("##material_actions_popup")) {
        const std::string create_label = editor_icon_label(EditorIcon::Plus, "Create and Assign");
        if (ImGui::MenuItem(create_label.c_str()) && actions.create) {
            const AssetHandle created = actions.create();
            if (created.is_valid()) assign(created);
        }
        if (!handle.is_valid()) ImGui::BeginDisabled();
        const std::string open_label = editor_icon_label(EditorIcon::ExternalLink, "Open Material");
        if (ImGui::MenuItem(open_label.c_str()) && actions.open) actions.open(handle);
        const std::string duplicate_label = editor_icon_label(EditorIcon::Copy, "Duplicate and Assign");
        if (ImGui::MenuItem(duplicate_label.c_str()) && actions.duplicate) {
            const AssetHandle duplicate = actions.duplicate(handle);
            if (duplicate.is_valid()) assign(duplicate);
        }
        if (!handle.is_valid()) ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(330.0f, 0.0f), ImVec2(430.0f, 420.0f));
    if (ImGui::BeginPopup("##material_picker")) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##search", "Search materials...", search.data(), search.size());
        ImGui::Separator();
        if (ImGui::Selectable("None", !handle.is_valid())) {
            assign({});
            ImGui::CloseCurrentPopup();
        }
        std::vector<const AssetRecord*> materials;
        for (const AssetRecord& record : database.records()) {
            if (record.type == AssetType::Material && record.status == AssetStatus::Ready) {
                materials.push_back(&record);
            }
        }
        std::sort(materials.begin(), materials.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
            return lhs->source_path < rhs->source_path;
        });
        const std::string query = search.data();
        for (const AssetRecord* record : materials) {
            const std::string name = std::filesystem::path(record->source_path).stem().string();
            if (!query.empty() && name.find(query) == std::string::npos &&
                record->source_path.find(query) == std::string::npos) continue;
            std::string compatibility_error;
            const auto candidate = MaterialAsset::load(record->handle, database);
            const bool compatible = candidate && (!renderer || SDLGPUMaterialBackend::supports(
                renderer, candidate->shader(), &compatibility_error
            ));
            if (!compatible) ImGui::BeginDisabled();
            const std::string item_label = name + "##" + record->handle.to_string();
            if (ImGui::Selectable(item_label.c_str(), record->handle == handle)) {
                assign(record->handle);
                ImGui::CloseCurrentPopup();
            }
            if (!compatible) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (ImGui::BeginTooltip()) {
                    SDL_Texture* hover_preview = compatible && actions.preview
                        ? actions.preview(record->handle) : nullptr;
                    if (hover_preview) {
                        draw_material_preview_image(hover_preview, {56.0f, 56.0f});
                        ImGui::SameLine();
                    }
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::TextDisabled("%s", record->source_path.c_str());
                    if (!compatible) ImGui::TextColored(
                        ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Unavailable: %s",
                        compatibility_error.c_str()
                    );
                    ImGui::EndGroup();
                    ImGui::EndTooltip();
                }
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

} // namespace Engine
