#pragma once

#include "AssetDropResolver.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Engine {

inline std::optional<AssetHandle> accept_asset_drop_target(
    const AssetDatabase& database,
    std::span<const AssetType> accepted_types = {}
) {
    std::optional<AssetHandle> accepted_handle;
    if (!ImGui::BeginDragDropTarget()) return accepted_handle;

    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
        kContentBrowserAssetPayload,
        ImGuiDragDropFlags_AcceptBeforeDelivery
    );
    if (payload) {
        std::string_view path;
        if (payload->Data && payload->DataSize > 0) {
            const char* data = static_cast<const char*>(payload->Data);
            const std::size_t capacity = static_cast<std::size_t>(payload->DataSize);
            const auto terminator = std::find(data, data + capacity, '\0');
            path = std::string_view(data, static_cast<std::size_t>(terminator - data));
        }

        const AssetDropResolution resolution = resolve_asset_drop(
            database,
            std::filesystem::path(std::string(path)),
            accepted_types
        );
        ImGui::BeginTooltip();
        if (resolution.accepted()) {
            ImGui::TextColored(
                ImVec4(0.35f, 0.85f, 0.50f, 1.0f),
                "%s",
                asset_drop_status_message(resolution.status)
            );
            ImGui::TextDisabled("%s", resolution.record->source_path.c_str());
            if (payload->IsDelivery()) accepted_handle = resolution.record->handle;
        } else {
            ImGui::TextColored(
                ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                "%s",
                asset_drop_status_message(resolution.status)
            );
            if (resolution.record) {
                ImGui::TextDisabled(
                    "%s (%s)",
                    resolution.record->source_path.c_str(),
                    AssetDatabase::type_name(resolution.record->type)
                );
            }
        }
        ImGui::EndTooltip();
    }
    ImGui::EndDragDropTarget();
    return accepted_handle;
}

inline std::optional<AssetHandle> accept_asset_drop_target(
    const AssetDatabase& database,
    AssetType accepted_type
) {
    const std::array<AssetType, 1> accepted_types{accepted_type};
    return accept_asset_drop_target(database, accepted_types);
}

} // namespace Engine
