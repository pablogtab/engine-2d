#include "ShortcutsPreferencesPanel.hpp"
#include <imgui.h>
#include <algorithm>

namespace Engine {

void ShortcutsPreferencesPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(520, 440), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "KEYBOARD SHORTCUTS & ACTION MAPPING");
        ImGui::TextDisabled("Customize shortcut keybindings across the editor.");
        ImGui::Separator();

        // Search Filter
        ImGui::InputTextWithHint("##search_shortcuts", "Filter actions (e.g. Undo, Save)...", m_search_filter, sizeof(m_search_filter));
        ImGui::SameLine();
        if (ImGui::Button(" Reset Defaults ")) {
            ShortcutManager::get().reset_to_defaults();
        }

        ImGui::Spacing();

        ImGui::BeginChild("ShortcutsTableScroll", ImVec2(0, 0), true);

        if (ImGui::BeginTable("ShortcutsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Shortcut Binding", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableHeadersRow();

            std::string filter_str = m_search_filter;
            std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

            const auto& shortcuts = ShortcutManager::get().get_shortcuts();
            ImGuiIO& io = ImGui::GetIO();

            for (const auto& [id, binding] : shortcuts) {
                std::string disp_lower = binding.display_name;
                std::transform(disp_lower.begin(), disp_lower.end(), disp_lower.begin(), ::tolower);
                std::string cat_lower = binding.category;
                std::transform(cat_lower.begin(), cat_lower.end(), cat_lower.begin(), ::tolower);

                if (!filter_str.empty()) {
                    if (disp_lower.find(filter_str) == std::string::npos &&
                        cat_lower.find(filter_str) == std::string::npos &&
                        id.find(filter_str) == std::string::npos) {
                        continue;
                    }
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", binding.display_name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", binding.category.c_str());

                ImGui::TableSetColumnIndex(2);

                bool is_rebinding = (m_rebinding_action_id == id);
                std::string btn_label = is_rebinding ? " [ Press Key... ] " : " " + ShortcutManager::get().get_label(id) + " ";
                if (btn_label.find("  ") != std::string::npos && !is_rebinding) {
                    btn_label = " [ None ] ";
                }

                if (is_rebinding) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.15f, 0.90f));
                }

                std::string btn_id = btn_label + "##" + id;
                if (ImGui::Button(btn_id.c_str(), ImVec2(130, 22))) {
                    m_rebinding_action_id = id;
                }

                if (is_rebinding) {
                    ImGui::PopStyleColor();

                    // Rebind key listener
                    for (int key_i = ImGuiKey_NamedKey_BEGIN; key_i < ImGuiKey_NamedKey_END; key_i++) {
                        ImGuiKey key = static_cast<ImGuiKey>(key_i);
                        if (ImGui::IsKeyPressed(key, false)) {
                            // Exclude pure modifier keypresses alone
                            if (key != ImGuiKey_LeftSuper && key != ImGuiKey_RightSuper &&
                                key != ImGuiKey_LeftCtrl && key != ImGuiKey_RightCtrl &&
                                key != ImGuiKey_LeftShift && key != ImGuiKey_RightShift &&
                                key != ImGuiKey_LeftAlt && key != ImGuiKey_RightAlt) {
                                
                                ShortcutManager::get().rebind(
                                    id, key,
                                    io.KeySuper || io.KeyCtrl,
                                    false,
                                    io.KeyShift,
                                    io.KeyAlt
                                );
                                m_rebinding_action_id = "";
                                break;
                            }
                        }
                    }

                    // Cancel on Escape
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        m_rebinding_action_id = "";
                    }
                }
            }

            ImGui::EndTable();
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace Engine
