#include "AssetBrowserPanel.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/Log.hpp"
#include "../../core/FileSystem.hpp"
#include "../../renderer/postprocess/PostProcessProfile.hpp"
#include "../../renderer/RenderLayerEffectProfile.hpp"
#include "../../renderer/material/MaterialAsset.hpp"
#include "../../renderer/material/SDLGPUMaterialBackend.hpp"
#include "../../renderer/material/ShaderAsset.hpp"
#include "../commands/AssetFileCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/PostProcessProfileCommands.hpp"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

namespace Engine {

namespace {

std::string format_bytes(const std::vector<std::byte>& bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index != 0) stream << ' ';
        stream << std::setw(2) << std::to_integer<unsigned>(bytes[index]);
    }
    return stream.str();
}

bool parse_hex_bytes(const char* text, std::vector<std::byte>& bytes) {
    bytes.clear();
    std::istringstream stream(text);
    std::string token;
    while (stream >> token) {
        if (token.size() != 2 || !std::isxdigit(static_cast<unsigned char>(token[0])) ||
            !std::isxdigit(static_cast<unsigned char>(token[1]))) return false;
        unsigned value = 0;
        std::istringstream parser(token);
        parser >> std::hex >> value;
        bytes.push_back(static_cast<std::byte>(value));
    }
    return true;
}

} // namespace

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

void AssetBrowserPanel::reveal_asset_path(const std::string& path) {
    if (path.empty()) return;

    const std::filesystem::path target = std::filesystem::path(path).lexically_normal();
    std::filesystem::path directory = target.parent_path();
    while (!directory.empty() && !std::filesystem::exists(directory)) {
        directory = directory.parent_path();
    }
    m_current_path = directory.empty() ? m_root_path : directory;
    m_selected_file = target.string();
    m_revealed_reference = target.string();
    is_open = true;
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

        if (!m_revealed_reference.empty()) {
            ImGui::TextDisabled("Referenced asset: %s", m_revealed_reference.c_str());
            if (!std::filesystem::exists(m_revealed_reference)) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.25f, 1.0f), "(missing)");
            }
            ImGui::Spacing();
        }

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
                    } else if (extension == ".layereffect") {
                        tag = "[LAYER FX]";
                        tag_col = ImVec4(0.55f, 0.80f, 1.00f, 1.0f);
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
                    const bool is_revealed = path.string() == m_selected_file;
                    if (is_revealed) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.42f, 0.72f, 1.0f));
                    }
                    if (ImGui::Button(button_label.c_str(), ImVec2(m_thumbnail_size + 14.0f, m_thumbnail_size + 18.0f))) {
                        m_selected_file = path.string();
                        m_revealed_reference.clear();
                    }
                    if (is_revealed) ImGui::PopStyleColor();

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
                        } else if (extension == ".material" && m_asset_database) {
                            const AssetRecord* material = m_asset_database->find_by_path(path);
                            std::string error;
                            const auto description = material
                                ? MaterialAsset::read(material->handle, *m_asset_database, &error)
                                : std::nullopt;
                            if (description) {
                                m_edit_material_handle = material->handle;
                                m_edit_material_shader_handle = description->shader_handle;
                                std::snprintf(m_edit_material_name_buf, sizeof(m_edit_material_name_buf), "%s", description->name.c_str());
                                m_edit_material_uniform_count = description->uniforms.size();
                                for (std::size_t index = 0; index < m_edit_material_uniform_count; ++index) {
                                    const std::string text = format_bytes(description->uniforms[index]);
                                    std::snprintf(m_edit_material_uniforms[index].data(), m_edit_material_uniforms[index].size(), "%s", text.c_str());
                                }
                                m_edit_material_error.clear();
                                m_show_edit_material_popup = true;
                            } else {
                                ENGINE_LOG_WARN("[ASSET BROWSER] Cannot edit material '{}': {}", path.string(), error);
                            }
                        } else if (extension == ".layereffect" && m_asset_database) {
                            const AssetRecord* record = m_asset_database->find_by_path(path);
                            std::string error;
                            const auto profile = record
                                ? RenderLayerEffectProfile::load_asset_for_authoring(record->handle, *m_asset_database, &error)
                                : std::nullopt;
                            if (profile) {
                                m_edit_layer_effect_handle = record->handle;
                                m_edit_layer_effect_original = *profile;
                                m_edit_layer_effect_draft = *profile;
                                std::snprintf(m_edit_layer_effect_name_buf, sizeof(m_edit_layer_effect_name_buf), "%s", profile->name.c_str());
                                m_edit_layer_effect_error.clear();
                                m_show_edit_layer_effect_popup = true;
                            } else {
                                ENGINE_LOG_WARN("[ASSET BROWSER] Cannot edit layer effect '{}': {}", path.string(), error);
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
                        if (extension == ".material" && ImGui::MenuItem("Duplicate Material") &&
                            m_command_history && m_asset_database) {
                            std::filesystem::path copy = path.parent_path() / (path.stem().string() + "_Copy.material");
                            unsigned suffix = 2;
                            while (std::filesystem::exists(copy)) {
                                copy = path.parent_path() / (path.stem().string() + "_Copy_" + std::to_string(suffix++) + ".material");
                            }
                            m_command_history->execute_command(std::make_unique<CreateAssetCommand>(
                                m_asset_database, copy, FileSystem::read_text(path.string())
                            ));
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
            if (ImGui::MenuItem("+ New Render Layer Effect")) {
                m_show_new_layer_effect_popup = true;
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
        if (m_show_new_layer_effect_popup) {
            ImGui::OpenPopup("Create New Render Layer Effect");
            m_show_new_layer_effect_popup = false;
        }
        if (m_show_edit_material_popup) {
            ImGui::OpenPopup("Edit Material");
            m_show_edit_material_popup = false;
        }
        if (m_show_edit_layer_effect_popup) {
            ImGui::OpenPopup("Edit Render Layer Effect");
            m_show_edit_layer_effect_popup = false;
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
                    const auto shader = std::find_if(m_asset_database->records().begin(), m_asset_database->records().end(), [this](const AssetRecord& record) {
                        return record.type == AssetType::Shader && record.status == AssetStatus::Ready &&
                            ShaderAsset::read(record.handle, *m_asset_database).has_value();
                    });
                    if (shader != m_asset_database->records().end()) {
                        const auto file = m_current_path / (name + ".material");
                        const auto shader_description = ShaderAsset::read(shader->handle, *m_asset_database);
                        nlohmann::json uniforms = nlohmann::json::array();
                        for (const auto& layout : shader_description->uniform_buffers) {
                            uniforms.push_back(nlohmann::json::array());
                            for (std::size_t byte = 0; byte < layout.size; ++byte) uniforms.back().push_back(0);
                        }
                        const nlohmann::json material_json{{"name", name}, {"shader_handle", shader->handle.to_string()}, {"uniforms", uniforms}};
                        m_command_history->execute_command(std::make_unique<CreateAssetCommand>(m_asset_database, file, material_json.dump(2) + "\n"));
                    } else {
                        ENGINE_LOG_WARN("[ASSET BROWSER] Cannot create material: no ready shader asset is available.");
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
        if (ImGui::BeginPopupModal("Create New Render Layer Effect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Enter Profile Name:");
            ImGui::InputText("##layereffectname", m_new_layer_effect_name_buf, sizeof(m_new_layer_effect_name_buf));
            const AssetRecord* first_material = m_asset_database && m_new_layer_effect_material_handle.is_valid()
                ? m_asset_database->find(m_new_layer_effect_material_handle) : nullptr;
            if (ImGui::BeginCombo("First Material", first_material ? first_material->source_path.c_str() : "None")) {
                if (ImGui::Selectable("None", !m_new_layer_effect_material_handle.is_valid())) {
                    m_new_layer_effect_material_handle = {};
                }
                if (m_asset_database) {
                    for (const AssetRecord& record : m_asset_database->records()) {
                        if (record.type != AssetType::Material || record.status != AssetStatus::Ready) continue;
                        if (ImGui::Selectable(record.source_path.c_str(), record.handle == m_new_layer_effect_material_handle)) {
                            m_new_layer_effect_material_handle = record.handle;
                        }
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                const std::string name = m_new_layer_effect_name_buf;
                if (!name.empty() && m_command_history && m_asset_database) {
                    RenderLayerEffectProfile profile;
                    profile.name = name;
                    if (m_new_layer_effect_material_handle.is_valid()) {
                        profile.passes.push_back({m_new_layer_effect_material_handle, true});
                    }
                    m_command_history->execute_command(std::make_unique<CreateAssetCommand>(
                        m_asset_database, m_current_path / (name + ".layereffect"), profile.to_json()
                    ));
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Edit Render Layer Effect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", m_edit_layer_effect_name_buf, sizeof(m_edit_layer_effect_name_buf));
            for (std::size_t pass_index = 0; pass_index < m_edit_layer_effect_draft.passes.size(); ++pass_index) {
                ImGui::PushID(static_cast<int>(pass_index));
                auto& pass = m_edit_layer_effect_draft.passes[pass_index];
                ImGui::Checkbox("##enabled", &pass.enabled);
                ImGui::SameLine();
                const AssetRecord* material = m_asset_database ? m_asset_database->find(pass.material_handle) : nullptr;
                if (ImGui::BeginCombo("Material", material ? material->source_path.c_str() : "Missing material")) {
                    if (m_asset_database) {
                        for (const AssetRecord& record : m_asset_database->records()) {
                            if (record.type != AssetType::Material || record.status != AssetStatus::Ready) continue;
                            if (ImGui::Selectable(record.source_path.c_str(), record.handle == pass.material_handle)) {
                                pass.material_handle = record.handle;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                if (pass_index > 0 && ImGui::SmallButton("Up")) {
                    std::swap(m_edit_layer_effect_draft.passes[pass_index], m_edit_layer_effect_draft.passes[pass_index - 1]);
                }
                ImGui::SameLine();
                if (pass_index + 1 < m_edit_layer_effect_draft.passes.size() && ImGui::SmallButton("Down")) {
                    std::swap(m_edit_layer_effect_draft.passes[pass_index], m_edit_layer_effect_draft.passes[pass_index + 1]);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    m_edit_layer_effect_draft.passes.erase(
                        m_edit_layer_effect_draft.passes.begin() + static_cast<std::ptrdiff_t>(pass_index)
                    );
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (ImGui::BeginCombo("Add Material Pass", "Select material")) {
                if (m_asset_database) {
                    for (const AssetRecord& record : m_asset_database->records()) {
                        if (record.type != AssetType::Material || record.status != AssetStatus::Ready) continue;
                        if (ImGui::Selectable(record.source_path.c_str())) {
                            m_edit_layer_effect_draft.passes.push_back({record.handle, true});
                        }
                    }
                }
                ImGui::EndCombo();
            }
            if (!m_edit_layer_effect_error.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", m_edit_layer_effect_error.c_str());
            }
            if (ImGui::Button("Save", ImVec2(120, 0)) && m_asset_database && m_command_history) {
                m_edit_layer_effect_draft.name = m_edit_layer_effect_name_buf;
                if (m_edit_layer_effect_draft.name.empty()) {
                    m_edit_layer_effect_error = "Profile name cannot be empty.";
                } else {
                    m_command_history->execute_command(std::make_unique<ModifyRenderLayerEffectProfileCommand>(
                        nullptr, m_asset_database, m_edit_layer_effect_handle,
                        m_edit_layer_effect_original, m_edit_layer_effect_draft
                    ));
                    const auto saved = RenderLayerEffectProfile::load_asset_for_authoring(
                        m_edit_layer_effect_handle, *m_asset_database, &m_edit_layer_effect_error
                    );
                    if (saved && *saved == m_edit_layer_effect_draft) ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Edit Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", m_edit_material_name_buf, sizeof(m_edit_material_name_buf));
            const AssetRecord* shader = m_edit_material_shader_handle.is_valid() && m_asset_database
                ? m_asset_database->find(m_edit_material_shader_handle) : nullptr;
            const auto shader_description = shader && m_asset_database
                ? ShaderAsset::read(shader->handle, *m_asset_database)
                : std::nullopt;
            if (ImGui::BeginCombo("Shader", shader ? shader->source_path.c_str() : "Missing shader")) {
                if (m_asset_database) {
                    for (const AssetRecord& record : m_asset_database->records()) {
                        if (record.type != AssetType::Shader || record.status != AssetStatus::Ready) continue;
                        const bool selected = record.handle == m_edit_material_shader_handle;
                        if (ImGui::Selectable(record.source_path.c_str(), selected)) m_edit_material_shader_handle = record.handle;
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (!shader || shader->status != AssetStatus::Ready || shader->type != AssetType::Shader) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Select a ready shader asset before saving.");
            } else if (!m_renderer) {
                ImGui::TextDisabled("Shader compatibility is unavailable until the renderer starts.");
            } else {
                std::string compatibility_error;
                const auto runtime_shader = ShaderAsset::load(shader->handle, *m_asset_database, &compatibility_error);
                const bool compatible = runtime_shader && SDLGPUMaterialBackend::supports(
                    m_renderer, *runtime_shader, &compatibility_error
                );
                const ImVec4 color = compatible
                    ? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
                    : ImVec4(1.0f, 0.55f, 0.35f, 1.0f);
                ImGui::TextColored(color, "%s", compatible
                    ? "Compatible with the current renderer."
                    : compatibility_error.c_str());
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Uniform buffers (hex bytes):");
            for (std::size_t index = 0; index < m_edit_material_uniform_count; ++index) {
                ImGui::PushID(static_cast<int>(index));
                const std::string label = shader_description && index < shader_description->uniform_buffers.size()
                    ? shader_description->uniform_buffers[index].name + " (" +
                        std::to_string(shader_description->uniform_buffers[index].size) + " bytes)"
                    : "Uniform buffer";
                ImGui::InputText(label.c_str(), m_edit_material_uniforms[index].data(), m_edit_material_uniforms[index].size());
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    for (std::size_t next = index + 1; next < m_edit_material_uniform_count; ++next) {
                        m_edit_material_uniforms[next - 1] = m_edit_material_uniforms[next];
                    }
                    --m_edit_material_uniform_count;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (!shader_description && m_edit_material_uniform_count < m_edit_material_uniforms.size() && ImGui::Button("Add Uniform Buffer")) {
                m_edit_material_uniforms[m_edit_material_uniform_count++].fill('\0');
            }
            if (!m_edit_material_error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", m_edit_material_error.c_str());
            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120, 0)) && m_asset_database) {
                MaterialAssetDescription description;
                description.name = m_edit_material_name_buf;
                description.shader_handle = m_edit_material_shader_handle;
                bool valid = !description.name.empty();
                for (std::size_t index = 0; valid && index < m_edit_material_uniform_count; ++index) {
                    std::vector<std::byte> bytes;
                    valid = parse_hex_bytes(m_edit_material_uniforms[index].data(), bytes);
                    if (valid) description.uniforms.push_back(std::move(bytes));
                }
                if (!valid) {
                    m_edit_material_error = "Each uniform buffer must use two-digit hexadecimal bytes.";
                } else if (MaterialAsset::save(m_edit_material_handle, description, *m_asset_database, &m_edit_material_error)) {
                    ImGui::CloseCurrentPopup();
                }
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
