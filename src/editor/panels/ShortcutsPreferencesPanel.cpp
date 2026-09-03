#include "ShortcutsPreferencesPanel.hpp"

#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"

#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<std::string> split_keycaps(const std::string& label) {
    if (label.empty()) return {"None"};
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= label.size()) {
        const std::size_t separator = label.find('+', start);
        result.push_back(label.substr(start, separator - start));
        if (separator == std::string::npos) break;
        start = separator + 1;
    }
    return result;
}

bool draw_binding_button(
    const char* id,
    const std::string& label,
    bool capturing,
    bool conflicting
) {
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 size(
        ImGui::GetContentRegionAvail().x,
        metrics.row_height + 2.0f * metrics.scale
    );
    const bool pressed = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImVec4 background = palette.field;
    if (active) background = palette.selected;
    else if (hovered) background = palette.header;
    const ImVec4 border = conflicting
        ? palette.error
        : (capturing ? palette.accent_hovered : palette.border);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        position, ImVec2(position.x + size.x, position.y + size.y),
        ImGui::ColorConvertFloat4ToU32(background), metrics.small_rounding
    );
    draw_list->AddRect(
        position, ImVec2(position.x + size.x, position.y + size.y),
        ImGui::ColorConvertFloat4ToU32(border), metrics.small_rounding,
        0, capturing || conflicting ? 1.5f : 1.0f
    );

    if (capturing) {
        constexpr const char* prompt = "Press shortcut...";
        const ImVec2 text_size = ImGui::CalcTextSize(prompt);
        draw_list->AddText(
            ImVec2(
                position.x + (size.x - text_size.x) * 0.5f,
                position.y + (size.y - text_size.y) * 0.5f
            ),
            ImGui::ColorConvertFloat4ToU32(palette.accent_hovered), prompt
        );
        return pressed;
    }

    const std::vector<std::string> keycaps = split_keycaps(label);
    const float horizontal_padding = 6.0f * metrics.scale;
    const float gap = 4.0f * metrics.scale;
    float total_width = gap * static_cast<float>(keycaps.size() - 1);
    for (const std::string& keycap : keycaps) {
        total_width += ImGui::CalcTextSize(keycap.c_str()).x + horizontal_padding * 2.0f;
    }
    float x = position.x + std::max(4.0f * metrics.scale, (size.x - total_width) * 0.5f);
    const bool unassigned = label.empty();
    for (const std::string& keycap : keycaps) {
        const ImVec2 text_size = ImGui::CalcTextSize(keycap.c_str());
        const float cap_width = text_size.x + horizontal_padding * 2.0f;
        const float cap_height = text_size.y + 4.0f * metrics.scale;
        const float y = position.y + (size.y - cap_height) * 0.5f;
        const ImVec4 cap_background = unassigned
            ? palette.panel_raised
            : palette.header;
        draw_list->AddRectFilled(
            ImVec2(x, y), ImVec2(x + cap_width, y + cap_height),
            ImGui::ColorConvertFloat4ToU32(cap_background), metrics.small_rounding
        );
        draw_list->AddRect(
            ImVec2(x, y), ImVec2(x + cap_width, y + cap_height),
            ImGui::ColorConvertFloat4ToU32(palette.border), metrics.small_rounding
        );
        draw_list->AddText(
            ImVec2(x + horizontal_padding, y + 2.0f * metrics.scale),
            ImGui::ColorConvertFloat4ToU32(
                unassigned ? palette.text_muted : palette.text
            ),
            keycap.c_str()
        );
        x += cap_width + gap;
    }
    return pressed;
}

std::string action_names(
    const std::vector<std::string>& ids,
    const std::unordered_map<std::string, ShortcutBinding>& shortcuts
) {
    std::string result;
    for (const std::string& id : ids) {
        const auto found = shortcuts.find(id);
        if (!result.empty()) result += ", ";
        result += found == shortcuts.end() ? id : found->second.display_name;
    }
    return result;
}

} // namespace

ShortcutsPreferencesPanel::ShortcutsPreferencesPanel() {
    is_open = false;
}

void ShortcutsPreferencesPanel::on_gui() {
    if (!is_open) return;

    ShortcutManager& manager = ShortcutManager::get();
    const auto& shortcuts = manager.get_shortcuts();
    const auto& metrics = EditorTheme::metrics();
    const auto& palette = EditorTheme::palette();

    if (!ImGui::IsPopupOpen(m_name.c_str())) ImGui::OpenPopup(m_name.c_str());
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)
    );
    ImGui::SetNextWindowSize(ImVec2(780.0f, 560.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(620.0f, 420.0f), ImVec2(1050.0f, 850.0f));
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::BeginPopupModal(m_name.c_str(), &is_open, flags)) return;

    bool request_reset_all = false;
    if (ImGui::BeginTable(
            "##shortcut_toolbar", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "Reset", ImGuiTableColumnFlags_WidthFixed, 148.0f * metrics.scale
        );
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const float search_width = ImGui::GetContentRegionAvail().x;
        if (EditorUi::search_field(
                "##search_shortcuts", "Search actions or categories...",
                m_search_filter, sizeof(m_search_filter), search_width) ||
            ImGui::IsItemActive()) {
            m_rebinding_action_id.clear();
        }
        ImGui::TableSetColumnIndex(1);
        request_reset_all = EditorUi::action_button(
            "reset_all_shortcuts", EditorIcon::History, "Reset Defaults",
            EditorUi::ButtonTone::Neutral, true, ImVec2(-1.0f, 0.0f)
        );
        ImGui::EndTable();
    }

    std::vector<const ShortcutBinding*> filtered;
    filtered.reserve(shortcuts.size());
    const std::string query = lowercase(m_search_filter);
    std::size_t modified_count = 0;
    std::size_t conflicting_count = 0;
    for (const auto& [id, binding] : shortcuts) {
        if (!manager.is_default(id)) ++modified_count;
        if (!manager.conflicts_for(
                id, binding.primary_key, binding.mod_cmd, binding.mod_ctrl,
                binding.mod_shift, binding.mod_alt).empty()) {
            ++conflicting_count;
        }
        if (!query.empty() &&
            lowercase(binding.display_name).find(query) == std::string::npos &&
            lowercase(binding.category).find(query) == std::string::npos &&
            lowercase(id).find(query) == std::string::npos) {
            continue;
        }
        filtered.push_back(&binding);
    }
    std::sort(filtered.begin(), filtered.end(), [](const auto* left, const auto* right) {
        if (left->category != right->category) return left->category < right->category;
        return left->display_name < right->display_name;
    });

    ImGui::TextDisabled(
        "%zu shortcut%s  |  %zu modified",
        filtered.size(), filtered.size() == 1 ? "" : "s", modified_count
    );
    if (conflicting_count > 0) {
        ImGui::SameLine();
        EditorUi::badge("CONFLICTS", EditorUi::BadgeTone::Error);
    }
    ImGui::Separator();

    bool request_conflict_popup = false;
    ImGui::BeginChild("ShortcutList", ImVec2(0.0f, 0.0f), false);
    if (filtered.empty()) {
        EditorUi::empty_state(
            EditorIcon::Settings,
            "No shortcuts found",
            "Try a different action or category"
        );
    } else if (ImGui::BeginTable(
            "ShortcutTable", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(
            "Binding", ImGuiTableColumnFlags_WidthFixed, 215.0f * metrics.scale
        );
        ImGui::TableSetupColumn(
            "Reset", ImGuiTableColumnFlags_WidthFixed, 30.0f * metrics.scale
        );
        ImGui::TableHeadersRow();

        std::string current_category;
        for (const ShortcutBinding* binding : filtered) {
            if (binding->category != current_category) {
                current_category = binding->category;
                ImGui::TableNextRow(ImGuiTableRowFlags_None, metrics.row_height);
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0,
                    ImGui::ColorConvertFloat4ToU32(palette.header)
                );
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", current_category.c_str());
            }

            const std::vector<std::string> conflicts = manager.conflicts_for(
                binding->id, binding->primary_key, binding->mod_cmd, binding->mod_ctrl,
                binding->mod_shift, binding->mod_alt
            );
            const bool conflicting = !conflicts.empty();
            const bool rebinding = m_rebinding_action_id == binding->id;
            ImGui::TableNextRow(
                ImGuiTableRowFlags_None, metrics.row_height + 8.0f * metrics.scale
            );
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(binding->display_name.c_str());
            if (conflicting) {
                ImGui::SameLine();
                EditorUi::badge("CONFLICT", EditorUi::BadgeTone::Error);
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(binding->id.c_str());
            if (draw_binding_button(
                    "##binding", manager.get_label(binding->id), rebinding, conflicting)) {
                m_rebinding_action_id = binding->id;
            }
            if (conflicting && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                const std::string names = action_names(conflicts, shortcuts);
                ImGui::SetTooltip("Also assigned to %s", names.c_str());
            }
            if (ImGui::BeginPopupContextItem("##binding_context")) {
                if (ImGui::MenuItem("Clear Binding")) {
                    manager.rebind(binding->id, EditorKey::None, false, false, false, false);
                    m_rebinding_action_id.clear();
                }
                const bool is_default = manager.is_default(binding->id);
                if (ImGui::MenuItem("Reset to Default", nullptr, false, !is_default)) {
                    manager.reset_to_default(binding->id);
                    m_rebinding_action_id.clear();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();

            if (rebinding) {
                const ImGuiIO& io = ImGui::GetIO();
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    m_rebinding_action_id.clear();
                } else {
                    const EditorKey key = manager.capture_pressed_key();
                    if (key != EditorKey::None) {
#ifdef __APPLE__
                        const bool command = io.ConfigMacOSXBehaviors
                            ? io.KeyCtrl
                            : io.KeySuper;
                        const bool control = io.ConfigMacOSXBehaviors
                            ? io.KeySuper
                            : io.KeyCtrl;
#else
                        const bool command = io.KeyCtrl;
                        const bool control = false;
#endif
                        m_pending = {
                            binding->id, key, command, control, io.KeyShift, io.KeyAlt,
                            manager.conflicts_for(
                                binding->id, key, command, control, io.KeyShift, io.KeyAlt
                            )
                        };
                        m_rebinding_action_id.clear();
                        if (m_pending.conflicts.empty()) {
                            manager.rebind(
                                m_pending.action_id, m_pending.key,
                                m_pending.command, m_pending.control,
                                m_pending.shift, m_pending.alt
                            );
                            m_pending = {};
                        } else {
                            request_conflict_popup = true;
                        }
                    }
                }
            }

            ImGui::TableSetColumnIndex(2);
            if (EditorUi::toolbar_button(
                    ("reset_" + binding->id).c_str(), EditorIcon::History,
                    "Reset to default", false, !manager.is_default(binding->id))) {
                manager.reset_to_default(binding->id);
                m_rebinding_action_id.clear();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (request_reset_all) ImGui::OpenPopup("Reset all shortcuts?");
    if (request_conflict_popup) ImGui::OpenPopup("Shortcut conflict");

    if (ImGui::BeginPopupModal(
            "Reset all shortcuts?", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("Restore every shortcut to its default binding?");
        ImGui::Spacing();
        if (EditorUi::action_button(
                "cancel_reset_shortcuts", EditorIcon::Clear, "Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorUi::action_button(
                "confirm_reset_shortcuts", EditorIcon::History, "Reset All",
                EditorUi::ButtonTone::Primary)) {
            manager.reset_to_defaults();
            m_rebinding_action_id.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(
            "Shortcut conflict", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings)) {
        const std::string names = action_names(m_pending.conflicts, shortcuts);
        const std::string pending_label = manager.format_label(
            m_pending.key, m_pending.command, m_pending.control,
            m_pending.shift, m_pending.alt
        );
        ImGui::Text("%s is already assigned to:", pending_label.c_str());
        ImGui::TextWrapped("%s", names.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("Reassigning clears the binding from those actions.");
        ImGui::Spacing();
        if (EditorUi::action_button(
                "cancel_shortcut_conflict", EditorIcon::Clear, "Cancel")) {
            m_pending = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (EditorUi::action_button(
                "replace_shortcut_conflict", EditorIcon::Edit, "Reassign",
                EditorUi::ButtonTone::Primary)) {
            manager.rebind(
                m_pending.action_id, m_pending.key,
                m_pending.command, m_pending.control,
                m_pending.shift, m_pending.alt, true
            );
            m_pending = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::EndPopup();
}

} // namespace Engine
