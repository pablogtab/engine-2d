#include "AssetBrowserPanel.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/Log.hpp"
#include "../../core/FileSystem.hpp"
#include "../../renderer/postprocess/PostProcessProfile.hpp"
#include "../commands/AssetFileCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include <imgui.h>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Engine {

AssetBrowserPanel::AssetBrowserPanel(AssetDatabase* asset_database, std::string root_path)
    : m_root_path(std::move(root_path)), m_current_path(m_root_path), m_asset_database(asset_database) {
    if (!std::filesystem::exists(m_root_path)) {
        std::filesystem::create_directories(m_root_path);
    }
}

void AssetBrowserPanel::refresh() {
    if (m_asset_database) {
        [[maybe_unused]] const AssetScanResult scan_result = m_asset_database->scan(m_root_path);
    }
}

void AssetBrowserPanel::on_gui() {
    if (!is_open) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    if (ImGui::Begin("Asset Browser", &is_open)) {
        // --- 1. Top Navigation Bar (Back button + Breadcrumbs + Refresh) ---
        bool is_root = (m_current_path == m_root_path);

        if (!is_root) {
            if (ImGui::Button("< Back")) {
                m_current_path = m_current_path.parent_path();
            }
            ImGui::SameLine();
        } else {
            ImGui::BeginDisabled();
            ImGui::Button("< Back");
            ImGui::EndDisabled();
            ImGui::SameLine();
        }

        // Clickable Breadcrumbs
        ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "Path: ");
        ImGui::SameLine();
        
        std::string rel_path = m_current_path.string();
        std::replace(rel_path.begin(), rel_path.end(), '\\', '/');
        ImGui::TextUnformatted(rel_path.c_str());

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60.0f);
        if (ImGui::Button("Refresh")) {
            refresh();
        }

        ImGui::Separator();
        ImGui::Spacing();

        // --- 2. Grid Items View ---
        float cell_size = m_thumbnail_size + m_padding;
        float panel_width = ImGui::GetContentRegionAvail().x;
        int column_count = static_cast<int>(panel_width / cell_size);
        if (column_count < 1) column_count = 1;

        if (ImGui::BeginTable("AssetGrid", column_count)) {
            try {
                for (auto& directory_entry : std::filesystem::directory_iterator(m_current_path)) {
                    const auto& path = directory_entry.path();
                    std::string filename = path.filename().string();
                    bool is_dir = directory_entry.is_directory();
                    std::string extension = path.extension().string();

                    // Metadata is engine bookkeeping, never user-facing content.
                    if (!is_dir && extension == ".meta") continue;

                    ImGui::TableNextColumn();
                    ImGui::PushID(filename.c_str());

                    // Determine Badge Label and Color
                    const char* tag = "[FILE]";
                    ImVec4 tag_col = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

                    if (is_dir) {
                        tag = "[DIR]";
                        tag_col = ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
                    } else if (extension == ".scene") {
                        tag = "[SCENE]";
                        tag_col = ImVec4(0.35f, 0.90f, 0.50f, 1.0f);
                    } else if (extension == ".prefab") {
                        tag = "[PREFAB]";
                        tag_col = ImVec4(0.85f, 0.45f, 0.95f, 1.0f);
                    } else if (extension == ".material") {
                        tag = "[MATERIAL]";
                        tag_col = ImVec4(0.95f, 0.60f, 0.25f, 1.0f);
                    } else if (extension == ".postprocess") {
                        tag = "[POST FX]";
                        tag_col = ImVec4(0.35f, 0.85f, 0.85f, 1.0f);
                    } else if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp") {
                        tag = "[IMAGE]";
                        tag_col = ImVec4(0.40f, 0.75f, 1.00f, 1.0f);
                    } else if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac") {
                        tag = "[AUDIO]";
                        tag_col = ImVec4(0.90f, 0.45f, 0.90f, 1.0f);
                    } else if (extension == ".json" || extension == ".sav") {
                        tag = "[DATA]";
                        tag_col = ImVec4(0.90f, 0.65f, 0.35f, 1.0f);
                    }

                    // Button style for item tile
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.15f, 0.20f, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.35f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.35f, 0.60f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, tag_col);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

                    std::string button_label = std::string(tag) + "\n\n" + filename;
                    if (ImGui::Button(button_label.c_str(), ImVec2(m_thumbnail_size + 14.0f, m_thumbnail_size + 18.0f))) {
                        m_selected_file = path.string();
                    }

                    // Drag and drop source for asset files
                    if (!is_dir && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                        std::string path_str = path.string();
                        ImGui::SetDragDropPayload("CONTENT_BROWSER_ASSET", path_str.c_str(), path_str.length() + 1);
                        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp") {
                            ImGui::Text("Spawn Sprite: %s", filename.c_str());
                        } else if (extension == ".prefab") {
                            ImGui::Text("Spawn Prefab: %s", filename.c_str());
                        } else if (extension == ".scene") {
                            ImGui::Text("Load Scene: %s", filename.c_str());
                        } else {
                            ImGui::Text("Asset: %s", filename.c_str());
                        }
                        ImGui::EndDragDropSource();
                    }

                    // Handle Double Click
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (is_dir) {
                            m_current_path /= path.filename();
                        } else if (extension == ".scene") {
                            if (m_on_load_scene) {
                                m_on_load_scene(path.string());
                            }
                        } else if (extension == ".prefab") {
                            if (m_on_open_prefab) {
                                m_on_open_prefab(path.string());
                            }
                        }
                    }

                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(4);

                    // Context menu per file item
                    if (ImGui::BeginPopupContextItem()) {
                        if (extension == ".scene") {
                            if (ImGui::MenuItem("Load Scene")) {
                                if (m_on_load_scene) m_on_load_scene(path.string());
                            }
                        }
                        if (extension == ".prefab") {
                            if (ImGui::MenuItem("Open Prefab")) {
                                if (m_on_open_prefab) m_on_open_prefab(path.string());
                            }
                        }
                        if (ImGui::MenuItem("Delete")) {
                            if (!is_dir && m_command_history && m_asset_database) {
                                m_command_history->execute_command(
                                    std::make_unique<DeleteAssetCommand>(m_asset_database, path)
                                );
                            }
                        }
                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }
            } catch (const std::exception& e) {
                ENGINE_LOG_WARN("[ASSET BROWSER] Exception browsing: {}", e.what());
            }

            ImGui::EndTable();
        }

        // --- 3. Context Menu on Blank Space ---
        if (ImGui::BeginPopupContextWindow("AssetBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("+ New Scene")) {
                m_show_new_scene_popup = true;
            }
            if (ImGui::MenuItem("+ New Material")) {
                m_show_new_material_popup = true;
            }
            if (ImGui::MenuItem("+ New Post-Process Profile")) {
                m_show_new_post_process_popup = true;
            }
            if (ImGui::MenuItem("+ New Folder")) {
                std::filesystem::create_directory(m_current_path / "NewFolder");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open in Finder / Explorer")) {
                std::string full_path = std::filesystem::absolute(m_current_path).string();
#ifdef __APPLE__
                std::string cmd = "open \"" + full_path + "\"";
                system(cmd.c_str());
#endif
            }
            ImGui::EndPopup();
        }

        // --- 4. Modal Popup: New Scene Dialog ---
        if (m_show_new_scene_popup) {
            ImGui::OpenPopup("Create New Scene");
            m_show_new_scene_popup = false;
        }
        if (m_show_new_material_popup) {
            ImGui::OpenPopup("Create New Material");
            m_show_new_material_popup = false;
        }
        if (m_show_new_post_process_popup) {
            ImGui::OpenPopup("Create New Post-Process Profile");
            m_show_new_post_process_popup = false;
        }

        if (ImGui::BeginPopupModal("Create New Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter Scene Name:");
            ImGui::InputText("##scenename", m_new_scene_name_buf, sizeof(m_new_scene_name_buf));
            ImGui::Spacing();

            if (ImGui::Button("Create", ImVec2(120, 0))) {
                std::string sname = m_new_scene_name_buf;
                if (!sname.empty()) {
                    if (sname.find(".scene") == std::string::npos) sname += ".scene";
                    std::filesystem::path new_file = m_current_path / sname;
                    const std::string scene_json = "{\n  \"scene_name\": \"" + std::string(m_new_scene_name_buf) + "\",\n  \"version\": 1,\n  \"entities\": []\n}\n";
                    if (m_command_history && m_asset_database) {
                        m_command_history->execute_command(
                            std::make_unique<CreateAssetCommand>(m_asset_database, new_file, scene_json)
                        );
                    }
                    if (std::filesystem::exists(new_file)) {
                        ENGINE_LOG_INFO("[ASSET BROWSER] Created new scene file: '{}'", new_file.string());
                    }
                    if (std::filesystem::exists(new_file) && m_on_load_scene) {
                        m_on_load_scene(new_file.string());
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Create New Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Enter Material Name:");
            ImGui::InputText("##materialname", m_new_material_name_buf, sizeof(m_new_material_name_buf));
            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                const std::string name = m_new_material_name_buf;
                if (!name.empty() && m_command_history && m_asset_database) {
                    const auto shader = std::find_if(m_asset_database->records().begin(), m_asset_database->records().end(), [](const AssetRecord& record) {
                        return record.type == AssetType::Shader && record.status == AssetStatus::Ready &&
                            std::filesystem::path(record.source_path).extension() == ".msl";
                    });
                    if (shader != m_asset_database->records().end()) {
                        const auto file = m_current_path / (name + ".material");
                        const nlohmann::json material_json{{"name", name}, {"shader_handle", shader->handle.to_string()}, {"uniforms", nlohmann::json::array()}};
                        m_command_history->execute_command(std::make_unique<CreateAssetCommand>(m_asset_database, file, material_json.dump(2) + "\n"));
                    } else {
                        ENGINE_LOG_WARN("[ASSET BROWSER] Cannot create material: no ready MSL shader asset is available.");
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Create New Post-Process Profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Enter Profile Name:");
            ImGui::InputText("##postprocessname", m_new_post_process_name_buf, sizeof(m_new_post_process_name_buf));
            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                const std::string name = m_new_post_process_name_buf;
                if (!name.empty() && m_command_history && m_asset_database) {
                    auto profile = PostProcessProfile::PresetDefault();
                    profile.name = name;
                    m_command_history->execute_command(std::make_unique<CreateAssetCommand>(
                        m_asset_database, m_current_path / (name + ".postprocess"), profile.to_json()
                    ));
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Engine
