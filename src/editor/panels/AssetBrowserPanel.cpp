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
#include "../inspector/AssetDropResolver.hpp"
#include "../inspector/MaterialInspectorWidget.hpp"
#include "../scripting/ScriptTemplates.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

namespace Engine {

namespace {

std::string file_url(const std::filesystem::path& path) {
    const std::string absolute = std::filesystem::absolute(path).generic_string();
    std::ostringstream encoded;
    encoded << "file://";
    if (absolute.empty() || absolute.front() != '/') encoded << '/';
    encoded << std::hex << std::uppercase;
    for (const unsigned char byte : absolute) {
        if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' ||
            byte == '~' || byte == '/' || byte == ':') {
            encoded << static_cast<char>(byte);
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
    }
    return encoded.str();
}

void open_script_externally(const std::filesystem::path& path) {
    if (!SDL_OpenURL(file_url(path).c_str())) {
        ENGINE_LOG_WARN("[ASSET BROWSER] Cannot open script '{}': {}", path.string(), SDL_GetError());
    }
}

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

bool path_is_within(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path relative =
        path.lexically_normal().lexically_relative(root.lexically_normal());
    return path.lexically_normal() == root.lexically_normal() ||
        (!relative.empty() && relative.begin()->string() != "..");
}

const char* asset_tag(const AssetBrowserEntry& entry) {
    if (entry.is_directory) return "[DIR]";
    switch (entry.type) {
        case AssetType::Scene: return "[SCENE]";
        case AssetType::Prefab: return "[PREFAB]";
        case AssetType::Texture: return "[IMAGE]";
        case AssetType::Font: return "[FONT]";
        case AssetType::Audio: return "[AUDIO]";
        case AssetType::Shader: return "[SHADER]";
        case AssetType::Material: return "[MATERIAL]";
        case AssetType::PostProcessProfile: return "[POST FX]";
        case AssetType::RenderLayerEffectProfile: return "[LAYER FX]";
        case AssetType::Script: return "[SCRIPT]";
        case AssetType::Data: return "[DATA]";
        case AssetType::Unknown: return "[FILE]";
    }
    return "[FILE]";
}

ImVec4 asset_tag_color(const AssetBrowserEntry& entry) {
    if (!entry.is_directory && entry.status != AssetStatus::Ready) {
        return ImVec4(0.95f, 0.42f, 0.32f, 1.0f);
    }
    if (entry.is_directory) return ImVec4(0.95f, 0.75f, 0.25f, 1.0f);
    switch (entry.type) {
        case AssetType::Scene: return ImVec4(0.35f, 0.90f, 0.50f, 1.0f);
        case AssetType::Prefab: return ImVec4(0.85f, 0.45f, 0.95f, 1.0f);
        case AssetType::Material: return ImVec4(0.95f, 0.60f, 0.25f, 1.0f);
        case AssetType::Script: return ImVec4(0.45f, 0.75f, 1.00f, 1.0f);
        case AssetType::PostProcessProfile: return ImVec4(0.35f, 0.85f, 0.85f, 1.0f);
        case AssetType::RenderLayerEffectProfile: return ImVec4(0.55f, 0.80f, 1.00f, 1.0f);
        case AssetType::Texture: return ImVec4(0.40f, 0.75f, 1.00f, 1.0f);
        case AssetType::Audio: return ImVec4(0.90f, 0.45f, 0.90f, 1.0f);
        case AssetType::Shader: return ImVec4(0.90f, 0.55f, 0.30f, 1.0f);
        case AssetType::Font: return ImVec4(0.65f, 0.85f, 0.65f, 1.0f);
        case AssetType::Data: return ImVec4(0.90f, 0.65f, 0.35f, 1.0f);
        case AssetType::Unknown: return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
    }
    return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
}

std::string human_file_size(std::uintmax_t size) {
    constexpr std::uintmax_t kilobyte = 1024;
    constexpr std::uintmax_t megabyte = kilobyte * 1024;
    std::ostringstream stream;
    if (size >= megabyte) {
        stream << std::fixed << std::setprecision(1)
               << static_cast<double>(size) / static_cast<double>(megabyte) << " MB";
    } else if (size >= kilobyte) {
        stream << std::fixed << std::setprecision(1)
               << static_cast<double>(size) / static_cast<double>(kilobyte) << " KB";
    } else {
        stream << size << " B";
    }
    return stream.str();
}

std::string modified_time_label(const std::filesystem::file_time_type& modified) {
    if (modified == std::filesystem::file_time_type{}) return "-";
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        modified - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now()
    );
    const std::time_t value = std::chrono::system_clock::to_time_t(system_time);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    char buffer[32]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local) == 0) return "-";
    return buffer;
}

std::filesystem::path unique_asset_path(
    const std::filesystem::path& directory,
    const std::string& base_name,
    const std::string& extension
) {
    std::filesystem::path path = directory / (base_name + extension);
    unsigned suffix = 2;
    while (std::filesystem::exists(path)) {
        path = directory / (base_name + "_" + std::to_string(suffix++) + extension);
    }
    return path;
}

bool draw_uniform_parameter(
    const ShaderUniformParameter& parameter,
    std::vector<std::byte>& bytes
) {
    const std::size_t count = ShaderAsset::parameter_component_count(parameter.type);
    if (count == 0 || parameter.offset + count * sizeof(float) > bytes.size()) return false;
    float values[4]{};
    std::memcpy(values, bytes.data() + parameter.offset, count * sizeof(float));
    bool changed = false;
    const bool has_range = parameter.minimum && parameter.maximum;
    const float minimum = has_range ? *parameter.minimum : 0.0f;
    const float maximum = has_range ? *parameter.maximum : 0.0f;
    switch (parameter.type) {
        case ShaderUniformParameterType::Float:
            changed = has_range
                ? ImGui::SliderFloat(parameter.name.c_str(), values, minimum, maximum)
                : ImGui::DragFloat(parameter.name.c_str(), values, 0.01f);
            break;
        case ShaderUniformParameterType::Vec2:
            changed = ImGui::DragFloat2(parameter.name.c_str(), values, 0.01f, minimum, maximum);
            break;
        case ShaderUniformParameterType::Vec4:
            changed = ImGui::DragFloat4(parameter.name.c_str(), values, 0.01f, minimum, maximum);
            break;
        case ShaderUniformParameterType::Color:
            changed = ImGui::ColorEdit4(parameter.name.c_str(), values);
            break;
    }
    if (changed) {
        std::memcpy(bytes.data() + parameter.offset, values, count * sizeof(float));
    }
    return changed;
}

} // namespace

AssetBrowserPanel::AssetBrowserPanel(AssetDatabase* asset_database, std::string root_path)
    : m_root_path(std::filesystem::absolute(std::move(root_path)).lexically_normal()),
      m_current_path(m_root_path),
      m_asset_database(asset_database) {
    if (!std::filesystem::exists(m_root_path)) {
        std::filesystem::create_directories(m_root_path);
    }
}

AssetBrowserPanel::~AssetBrowserPanel() {
    destroy_material_previews();
}

bool AssetBrowserPanel::ensure_material_preview_source() {
    if (m_material_preview_source) return true;
    if (!m_renderer) return false;
    constexpr int preview_size = 64;
    std::vector<std::uint8_t> pixels(preview_size * preview_size * 4, 0);
    const float light_x = -0.45f;
    const float light_y = -0.55f;
    const float light_z = 0.70f;
    for (int y = 0; y < preview_size; ++y) {
        for (int x = 0; x < preview_size; ++x) {
            const float nx = (static_cast<float>(x) + 0.5f) / (preview_size * 0.5f) - 1.0f;
            const float ny = (static_cast<float>(y) + 0.5f) / (preview_size * 0.5f) - 1.0f;
            const float radius_squared = nx * nx + ny * ny;
            if (radius_squared > 1.0f) continue;
            const float nz = std::sqrt(std::max(0.0f, 1.0f - radius_squared));
            const float diffuse = std::max(0.0f, nx * light_x + ny * light_y + nz * light_z);
            const float rim = std::pow(std::max(0.0f, 1.0f - nz), 2.0f);
            const float specular = std::pow(std::max(0.0f, diffuse), 18.0f);
            const float shade = std::clamp(0.18f + diffuse * 0.68f + specular * 0.32f + rim * 0.05f, 0.0f, 1.0f);
            const float edge = std::clamp((1.0f - std::sqrt(radius_squared)) * preview_size * 0.8f, 0.0f, 1.0f);
            const std::size_t offset = static_cast<std::size_t>((y * preview_size + x) * 4);
            const auto channel = static_cast<std::uint8_t>(shade * 255.0f);
            pixels[offset + 0] = channel;
            pixels[offset + 1] = channel;
            pixels[offset + 2] = channel;
            pixels[offset + 3] = static_cast<std::uint8_t>(edge * 255.0f);
        }
    }
    m_material_preview_source = SDL_CreateTexture(
        m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, preview_size, preview_size
    );
    if (!m_material_preview_source || !SDL_UpdateTexture(
            m_material_preview_source, nullptr, pixels.data(), preview_size * 4
        )) {
        destroy_material_previews();
        return false;
    }
    SDL_SetTextureBlendMode(m_material_preview_source, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(m_material_preview_source, SDL_SCALEMODE_LINEAR);
    return true;
}

SDL_Texture* AssetBrowserPanel::render_material_preview(
    AssetHandle handle,
    const std::shared_ptr<const Material>& material
) {
    if (!handle.is_valid() || !material || !m_material_backend ||
        !m_material_backend->is_available() || !ensure_material_preview_source()) return nullptr;
    constexpr int preview_size = 64;
    SDL_Texture*& output = m_material_preview_outputs[handle];
    if (!output) {
        output = SDL_CreateTexture(
            m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
            preview_size, preview_size
        );
        if (!output) return nullptr;
        SDL_SetTextureBlendMode(output, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(output, SDL_SCALEMODE_LINEAR);
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(m_renderer);
    SDL_Rect previous_viewport{};
    const bool had_viewport = SDL_GetRenderViewport(m_renderer, &previous_viewport);
    float previous_scale_x = 1.0f;
    float previous_scale_y = 1.0f;
    static_cast<void>(SDL_GetRenderScale(m_renderer, &previous_scale_x, &previous_scale_y));
    Uint8 previous_r = 0;
    Uint8 previous_g = 0;
    Uint8 previous_b = 0;
    Uint8 previous_a = 0;
    static_cast<void>(SDL_GetRenderDrawColor(
        m_renderer, &previous_r, &previous_g, &previous_b, &previous_a
    ));

    if (!SDL_SetRenderTarget(m_renderer, output)) return nullptr;
    static_cast<void>(SDL_SetRenderScale(m_renderer, 1.0f, 1.0f));
    static_cast<void>(SDL_SetRenderViewport(m_renderer, nullptr));
    static_cast<void>(SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0));
    static_cast<void>(SDL_RenderClear(m_renderer));
    const bool rendered = m_material_backend->draw_fullscreen(
        *material, m_material_preview_source, {preview_size, preview_size}
    );

    static_cast<void>(SDL_SetRenderTarget(m_renderer, previous_target));
    static_cast<void>(SDL_SetRenderScale(m_renderer, previous_scale_x, previous_scale_y));
    if (had_viewport) static_cast<void>(SDL_SetRenderViewport(m_renderer, &previous_viewport));
    static_cast<void>(SDL_SetRenderDrawColor(
        m_renderer, previous_r, previous_g, previous_b, previous_a
    ));
    return rendered ? output : nullptr;
}

SDL_Texture* AssetBrowserPanel::preview_material_asset(AssetHandle handle) {
    if (!m_asset_database || !handle.is_valid()) return nullptr;
    if (const auto cached = m_material_preview_outputs.find(handle);
        cached != m_material_preview_outputs.end() && cached->second) return cached->second;
    return render_material_preview(handle, MaterialAsset::load(handle, *m_asset_database));
}

void AssetBrowserPanel::destroy_material_previews() {
    for (auto& [handle, texture] : m_material_preview_outputs) {
        (void)handle;
        if (texture) SDL_DestroyTexture(texture);
    }
    m_material_preview_outputs.clear();
    if (m_material_preview_source) SDL_DestroyTexture(m_material_preview_source);
    m_material_preview_source = nullptr;
}

void AssetBrowserPanel::refresh() {
    if (m_asset_database) {
        [[maybe_unused]] const AssetScanResult scan_result = m_asset_database->scan(m_root_path);
    }
}

void AssetBrowserPanel::reveal_asset_path(const std::string& path) {
    if (path.empty()) return;

    std::filesystem::path target(path);
    if (target.is_relative() && m_asset_database) {
        target = m_asset_database->project_root() / target;
    }
    target = target.lexically_normal();
    std::filesystem::path directory = target.parent_path();
    while (!directory.empty() && !std::filesystem::exists(directory)) {
        directory = directory.parent_path();
    }
    navigate_to(directory.empty() ? m_root_path : directory);
    m_selected_file = target.string();
    m_revealed_reference = target.string();
    m_search_query.fill('\0');
    m_search_scope = AssetBrowserScope::CurrentFolder;
    m_type_filter = AssetBrowserTypeFilter::All;
    m_status_filter = AssetBrowserStatusFilter::All;
    is_open = true;
}

AssetHandle AssetBrowserPanel::create_material_asset(std::string base_name) {
    if (!m_asset_database || !m_command_history) return {};
    const auto shader = std::find_if(
        m_asset_database->records().begin(),
        m_asset_database->records().end(),
        [this](const AssetRecord& record) {
            return record.type == AssetType::Shader && record.status == AssetStatus::Ready &&
                ShaderAsset::read(record.handle, *m_asset_database).has_value();
        }
    );
    if (shader == m_asset_database->records().end()) {
        ENGINE_LOG_WARN("[ASSET BROWSER] Cannot create material: no ready shader asset is available.");
        return {};
    }
    const auto shader_description = ShaderAsset::read(shader->handle, *m_asset_database);
    if (!shader_description) return {};
    const std::filesystem::path file = unique_asset_path(m_current_path, base_name, ".material");
    MaterialAssetDescription description{
        file.stem().string(), shader->handle, ShaderAsset::make_default_uniforms(*shader_description)
    };
    std::string error;
    const auto content = MaterialAsset::serialize(description, *m_asset_database, &error);
    if (!content) {
        ENGINE_LOG_WARN("[ASSET BROWSER] Cannot create material: {}", error);
        return {};
    }
    m_command_history->execute_command(std::make_unique<CreateAssetCommand>(
        m_asset_database, file, *content
    ));
    const AssetRecord* created = m_asset_database->find_by_path(file);
    if (!created) return {};
    reveal_asset_path(created->source_path);
    return created->handle;
}

AssetHandle AssetBrowserPanel::duplicate_material_asset(AssetHandle source_handle) {
    if (!m_asset_database || !m_command_history || !source_handle.is_valid()) return {};
    const AssetRecord* source = m_asset_database->find(source_handle);
    const auto description = MaterialAsset::read(source_handle, *m_asset_database);
    if (!source || !description) return {};
    const std::filesystem::path source_path = m_asset_database->resolve(source_handle);
    const std::filesystem::path file = unique_asset_path(
        source_path.parent_path(), source_path.stem().string() + "_Copy", ".material"
    );
    MaterialAssetDescription copy = *description;
    copy.name = file.stem().string();
    std::string error;
    const auto content = MaterialAsset::serialize(copy, *m_asset_database, &error);
    if (!content) {
        ENGINE_LOG_WARN("[ASSET BROWSER] Cannot duplicate material: {}", error);
        return {};
    }
    m_command_history->execute_command(std::make_unique<CreateAssetCommand>(
        m_asset_database, file, *content
    ));
    const AssetRecord* created = m_asset_database->find_by_path(file);
    if (!created) return {};
    reveal_asset_path(created->source_path);
    return created->handle;
}

void AssetBrowserPanel::open_material_asset(AssetHandle handle) {
    if (!m_asset_database || !handle.is_valid()) return;
    const AssetRecord* material = m_asset_database->find(handle);
    std::string error;
    const auto description = MaterialAsset::read(handle, *m_asset_database, &error);
    if (!material || !description) {
        ENGINE_LOG_WARN("[ASSET BROWSER] Cannot edit material: {}", error);
        return;
    }
    reveal_asset_path(material->source_path);
    m_edit_material_handle = handle;
    m_edit_material_draft = *description;
    std::snprintf(
        m_edit_material_name_buf, sizeof(m_edit_material_name_buf), "%s", description->name.c_str()
    );
    m_edit_material_uniform_count = description->uniforms.size();
    for (std::size_t index = 0; index < m_edit_material_uniform_count; ++index) {
        const std::string text = format_bytes(description->uniforms[index]);
        std::snprintf(
            m_edit_material_uniforms[index].data(), m_edit_material_uniforms[index].size(), "%s", text.c_str()
        );
    }
    m_edit_material_error.clear();
    m_edit_material_preview_active = false;
    m_edit_material_preview_material = MaterialAsset::load(handle, *m_asset_database);
    m_show_edit_material_popup = true;
}

void AssetBrowserPanel::navigate_to(const std::filesystem::path& directory) {
    const std::filesystem::path normalized =
        std::filesystem::absolute(directory).lexically_normal();
    if (!path_is_within(normalized, m_root_path)) return;
    std::error_code error;
    if (!std::filesystem::is_directory(normalized, error)) return;
    m_current_path = normalized;
    m_search_scope = AssetBrowserScope::CurrentFolder;
}

void AssetBrowserPanel::activate_entry(const AssetBrowserEntry& entry) {
    if (entry.is_directory) {
        navigate_to(entry.absolute_path);
        return;
    }
    if (entry.status != AssetStatus::Ready) return;
    const std::string& extension = entry.extension;
    if (extension == ".scene") {
        if (m_on_load_scene) m_on_load_scene(entry.absolute_path.string());
    } else if (extension == ".prefab") {
        if (m_on_open_prefab) m_on_open_prefab(entry.absolute_path.string());
    } else if (extension == ".luau") {
        open_script_externally(entry.absolute_path);
    } else if (extension == ".material") {
        open_material_asset(entry.handle);
    } else if (extension == ".layereffect" && m_asset_database) {
        std::string error;
        const auto profile = RenderLayerEffectProfile::load_asset_for_authoring(
            entry.handle, *m_asset_database, &error
        );
        if (profile) {
            m_edit_layer_effect_handle = entry.handle;
            m_edit_layer_effect_original = *profile;
            m_edit_layer_effect_draft = *profile;
            std::snprintf(
                m_edit_layer_effect_name_buf,
                sizeof(m_edit_layer_effect_name_buf),
                "%s",
                profile->name.c_str()
            );
            m_edit_layer_effect_error.clear();
            m_show_edit_layer_effect_popup = true;
        } else {
            ENGINE_LOG_WARN(
                "[ASSET BROWSER] Cannot edit layer effect '{}': {}",
                entry.absolute_path.string(),
                error
            );
        }
    }
}

void AssetBrowserPanel::request_rename(const AssetBrowserEntry& entry) {
    if (entry.is_directory || entry.status != AssetStatus::Ready) return;
    m_rename_source = entry.absolute_path;
    m_rename_buffer.fill('\0');
    std::snprintf(
        m_rename_buffer.data(),
        m_rename_buffer.size(),
        "%s",
        entry.name.c_str()
    );
    m_rename_error.clear();
    m_show_rename_popup = true;
}

void AssetBrowserPanel::draw_entry(const AssetBrowserEntry& entry) {
    const std::filesystem::path& path = entry.absolute_path;
    ImGui::PushID(entry.project_path.c_str());

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.15f, 0.20f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.35f, 0.60f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, asset_tag_color(entry));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    const bool selected = path.string() == m_selected_file;
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.42f, 0.72f, 1.0f));
    }
    std::string button_label = std::string(asset_tag(entry)) + "\n\n" + entry.name;
    if (!entry.is_directory && entry.status != AssetStatus::Ready) button_label += "\n!";
    if (ImGui::Button(
            button_label.c_str(),
            ImVec2(m_thumbnail_size + 14.0f, m_thumbnail_size + 18.0f)
        )) {
        m_selected_file = path.string();
        m_revealed_reference.clear();
    }
    if (selected) ImGui::PopStyleColor();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    draw_entry_interactions(entry);
    ImGui::PopID();
}

void AssetBrowserPanel::draw_entry_interactions(const AssetBrowserEntry& entry) {
    const std::filesystem::path& path = entry.absolute_path;

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(entry.project_path.c_str());
        if (!entry.is_directory) {
            ImGui::Text("Type: %s", AssetDatabase::type_name(entry.type));
            ImGui::Text("Status: %s", AssetBrowserQuery::status_name(entry.status));
            ImGui::Text("Size: %s", human_file_size(entry.size).c_str());
            ImGui::Text("Handle: %s", entry.handle.to_string().c_str());
            if (entry.status == AssetStatus::Ready) ImGui::TextDisabled("Double-click to open");
        } else {
            ImGui::TextDisabled("Double-click to browse");
        }
        ImGui::EndTooltip();
    }

    if (!entry.is_directory && entry.status == AssetStatus::Ready &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        const std::string path_string = path.string();
        ImGui::SetDragDropPayload(
            kContentBrowserAssetPayload, path_string.c_str(), path_string.length() + 1
        );
        ImGui::Text("Asset: %s", entry.name.c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        activate_entry(entry);
    }

    if (ImGui::BeginPopupContextItem()) {
        if (entry.is_directory) {
            if (ImGui::MenuItem("Open Folder")) navigate_to(path);
        } else {
            const bool ready = entry.status == AssetStatus::Ready;
            ImGui::BeginDisabled(!ready);
            if (ImGui::MenuItem("Open")) activate_entry(entry);
            if (entry.type == AssetType::Material && ImGui::MenuItem("Duplicate Material") &&
                m_command_history && m_asset_database) {
                static_cast<void>(duplicate_material_asset(entry.handle));
            }
            if (ImGui::MenuItem("Rename...")) request_rename(entry);
            ImGui::EndDisabled();

            if (ImGui::MenuItem("Reveal in Folder")) {
                navigate_to(path.parent_path());
                m_search_query.fill('\0');
                m_type_filter = AssetBrowserTypeFilter::All;
                m_status_filter = AssetBrowserStatusFilter::All;
                m_selected_file = path.string();
            }
            if (ImGui::MenuItem("Copy Project Path")) {
                ImGui::SetClipboardText(entry.project_path.c_str());
            }
            if (entry.handle.is_valid() && ImGui::MenuItem("Copy Asset Handle")) {
                ImGui::SetClipboardText(entry.handle.to_string().c_str());
            }
            ImGui::BeginDisabled(!ready || !m_command_history || !m_asset_database);
            if (ImGui::MenuItem("Delete")) {
                m_command_history->execute_command(
                    std::make_unique<DeleteAssetCommand>(m_asset_database, path)
                );
                if (m_selected_file == path.string()) m_selected_file.clear();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }
}

void AssetBrowserPanel::draw_list_entry(const AssetBrowserEntry& entry) {
    ImGui::PushID(entry.project_path.c_str());
    const bool selected = entry.absolute_path.string() == m_selected_file;
    std::string label = std::string(asset_tag(entry)) + "  " + entry.name;
    if (!entry.is_directory && entry.status != AssetStatus::Ready) label += "  !";
    ImGui::PushStyleColor(ImGuiCol_Text, asset_tag_color(entry));
    if (ImGui::Selectable(
            label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0.0f, 24.0f)
        )) {
        m_selected_file = entry.absolute_path.string();
        m_revealed_reference.clear();
    }
    ImGui::PopStyleColor();
    draw_entry_interactions(entry);
    ImGui::PopID();
}

void AssetBrowserPanel::draw_details_entries(const std::vector<AssetBrowserEntry>& entries) {
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable;
    ImGui::PushID(static_cast<int>(m_sort) * 2 + (m_sort_ascending ? 1 : 0));
    if (!ImGui::BeginTable("AssetDetails", 6, flags)) {
        ImGui::PopID();
        return;
    }

    const auto user_id = [](AssetBrowserSort sort) {
        return static_cast<ImGuiID>(sort) + 1;
    };
    const auto sort_flags = [this](AssetBrowserSort sort) {
        ImGuiTableColumnFlags column_flags = ImGuiTableColumnFlags_None;
        if (m_sort == sort) {
            column_flags |= ImGuiTableColumnFlags_DefaultSort;
            column_flags |= m_sort_ascending
                ? ImGuiTableColumnFlags_PreferSortAscending
                : ImGuiTableColumnFlags_PreferSortDescending;
        }
        return column_flags;
    };
    ImGui::TableSetupColumn(
        "Name", sort_flags(AssetBrowserSort::Name) | ImGuiTableColumnFlags_WidthStretch,
        0.0f, user_id(AssetBrowserSort::Name)
    );
    ImGui::TableSetupColumn(
        "Type", sort_flags(AssetBrowserSort::Type) | ImGuiTableColumnFlags_WidthFixed,
        92.0f, user_id(AssetBrowserSort::Type)
    );
    ImGui::TableSetupColumn(
        "Status", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 82.0f
    );
    ImGui::TableSetupColumn(
        "Modified", sort_flags(AssetBrowserSort::Modified) |
            ImGuiTableColumnFlags_WidthFixed, 128.0f,
        user_id(AssetBrowserSort::Modified)
    );
    ImGui::TableSetupColumn(
        "Size", sort_flags(AssetBrowserSort::Size) | ImGuiTableColumnFlags_WidthFixed,
        72.0f, user_id(AssetBrowserSort::Size)
    );
    ImGui::TableSetupColumn(
        "Path", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort
    );
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
        specs && specs->SpecsDirty && specs->SpecsCount > 0) {
        const ImGuiTableColumnSortSpecs& primary = specs->Specs[0];
        if (primary.ColumnUserID > 0) {
            m_sort = static_cast<AssetBrowserSort>(primary.ColumnUserID - 1);
            m_sort_ascending = primary.SortDirection != ImGuiSortDirection_Descending;
        }
        specs->SpecsDirty = false;
    }

    for (const AssetBrowserEntry& entry : entries) {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(entry.project_path.c_str());
        const bool selected = entry.absolute_path.string() == m_selected_file;
        std::string label = std::string(asset_tag(entry)) + "  " + entry.name;
        if (!entry.is_directory && entry.status != AssetStatus::Ready) label += "  !";
        ImGui::PushStyleColor(ImGuiCol_Text, asset_tag_color(entry));
        if (ImGui::Selectable(
                label.c_str(), selected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0.0f, 22.0f)
            )) {
            m_selected_file = entry.absolute_path.string();
            m_revealed_reference.clear();
        }
        ImGui::PopStyleColor();
        draw_entry_interactions(entry);

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(entry.is_directory ? "Folder" : AssetDatabase::type_name(entry.type));
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(entry.is_directory ? "-" : AssetBrowserQuery::status_name(entry.status));
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(entry.is_directory ? "-" : modified_time_label(entry.modified).c_str());
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(entry.is_directory ? "-" : human_file_size(entry.size).c_str());
        ImGui::TableSetColumnIndex(5);
        ImGui::TextDisabled("%s", entry.project_path.c_str());
        ImGui::PopID();
    }
    ImGui::EndTable();
    ImGui::PopID();
}

void AssetBrowserPanel::draw_selected_asset_details() {
    if (m_selected_file.empty() || !m_asset_database) return;
    const AssetRecord* record = m_asset_database->find_by_path(m_selected_file);
    if (!record) return;

    ImGui::Separator();
    ImGui::TextUnformatted(std::filesystem::path(record->source_path).filename().string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s | %s",
        AssetDatabase::type_name(record->type),
        AssetBrowserQuery::status_name(record->status)
    );
    ImGui::TextDisabled("%s", record->source_path.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Path")) ImGui::SetClipboardText(record->source_path.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy Handle")) {
        ImGui::SetClipboardText(record->handle.to_string().c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reveal")) {
        const std::filesystem::path absolute =
            (m_asset_database->project_root() / record->source_path).lexically_normal();
        static_cast<void>(SDL_OpenURL(file_url(absolute.parent_path()).c_str()));
    }

    const auto& dependencies = m_asset_database->dependencies(record->handle);
    const auto& dependents = m_asset_database->dependents(record->handle);
    ImGui::TextDisabled(
        "Dependencies: %zu | Used by: %zu",
        dependencies.size(),
        dependents.size()
    );
    if (ImGui::IsItemHovered() && (!dependencies.empty() || !dependents.empty())) {
        ImGui::BeginTooltip();
        if (!dependencies.empty()) ImGui::TextUnformatted("Dependencies");
        for (const AssetHandle handle : dependencies) {
            const AssetRecord* dependency = m_asset_database->find(handle);
            ImGui::BulletText("%s", dependency ? dependency->source_path.c_str() : handle.to_string().c_str());
        }
        if (!dependents.empty()) ImGui::TextUnformatted("Used by");
        for (const AssetHandle handle : dependents) {
            const AssetRecord* dependent = m_asset_database->find(handle);
            ImGui::BulletText("%s", dependent ? dependent->source_path.c_str() : handle.to_string().c_str());
        }
        ImGui::EndTooltip();
    }
}

void AssetBrowserPanel::on_gui() {
    if (!is_open) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    if (ImGui::Begin("Asset Browser", &is_open)) {
        const bool at_root = m_current_path == m_root_path;
        ImGui::BeginDisabled(at_root);
        if (ImGui::Button("< Up")) navigate_to(m_current_path.parent_path());
        ImGui::EndDisabled();
        ImGui::SameLine();

        std::filesystem::path breadcrumb = m_root_path;
        ImGui::PushID("asset_breadcrumb_root");
        if (ImGui::SmallButton(m_root_path.filename().string().c_str())) navigate_to(m_root_path);
        ImGui::PopID();
        const std::filesystem::path relative = m_current_path.lexically_relative(m_root_path);
        int breadcrumb_index = 0;
        for (const auto& part : relative) {
            if (part == ".") continue;
            breadcrumb /= part;
            ImGui::SameLine();
            ImGui::TextDisabled("/");
            ImGui::SameLine();
            ImGui::PushID(breadcrumb_index++);
            if (ImGui::SmallButton(part.string().c_str())) navigate_to(breadcrumb);
            ImGui::PopID();
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) refresh();

        const ImGuiIO& io = ImGui::GetIO();
        const bool focus_search = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            (io.KeySuper || io.KeyCtrl) && ImGui::IsKeyPressed(ImGuiKey_F);
        if (focus_search) {
            m_search_scope = AssetBrowserScope::Project;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x * 0.34f));
        ImGui::InputTextWithHint(
            "##asset_search",
            "Search name/path; t:, ext:, status:, id:...",
            m_search_query.data(),
            m_search_query.size()
        );
        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_search_query.fill('\0');
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Examples:\n"
                "t:material hero\n"
                "path:characters ext:png\n"
                "status:issues\n"
                "id:12ab"
            );
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_search_query.front() == '\0');
        if (ImGui::Button("Clear")) m_search_query.fill('\0');
        ImGui::EndDisabled();
        ImGui::SameLine();

        const char* scope_label = m_search_scope == AssetBrowserScope::CurrentFolder
            ? "Current folder" : "Entire project";
        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::BeginCombo("##asset_scope", scope_label)) {
            if (ImGui::Selectable(
                    "Current folder", m_search_scope == AssetBrowserScope::CurrentFolder
                )) m_search_scope = AssetBrowserScope::CurrentFolder;
            if (ImGui::Selectable(
                    "Entire project", m_search_scope == AssetBrowserScope::Project
                )) m_search_scope = AssetBrowserScope::Project;
            ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(125.0f);
        if (ImGui::BeginCombo("##asset_type", AssetBrowserQuery::type_filter_name(m_type_filter))) {
            for (int value = static_cast<int>(AssetBrowserTypeFilter::All);
                 value <= static_cast<int>(AssetBrowserTypeFilter::Other); ++value) {
                const auto filter = static_cast<AssetBrowserTypeFilter>(value);
                if (ImGui::Selectable(
                        AssetBrowserQuery::type_filter_name(filter), filter == m_type_filter
                    )) m_type_filter = filter;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(115.0f);
        if (ImGui::BeginCombo(
                "##asset_status", AssetBrowserQuery::status_filter_name(m_status_filter)
            )) {
            for (int value = static_cast<int>(AssetBrowserStatusFilter::All);
                 value <= static_cast<int>(AssetBrowserStatusFilter::Issues); ++value) {
                const auto filter = static_cast<AssetBrowserStatusFilter>(value);
                if (ImGui::Selectable(
                        AssetBrowserQuery::status_filter_name(filter), filter == m_status_filter
                    )) m_status_filter = filter;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105.0f);
        if (ImGui::BeginCombo("##asset_sort", AssetBrowserQuery::sort_name(m_sort))) {
            for (int value = static_cast<int>(AssetBrowserSort::Name);
                 value <= static_cast<int>(AssetBrowserSort::Size); ++value) {
                const auto sort = static_cast<AssetBrowserSort>(value);
                if (ImGui::Selectable(AssetBrowserQuery::sort_name(sort), sort == m_sort)) {
                    m_sort = sort;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(m_sort_ascending ? "Ascending" : "Descending")) {
            m_sort_ascending = !m_sort_ascending;
        }

        ImGui::TextDisabled("View");
        ImGui::SameLine();
        const auto view_button = [this](const char* label, AssetBrowserViewMode mode) {
            const bool active = m_view_mode == mode;
            if (active) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
                );
            }
            if (ImGui::Button(label)) m_view_mode = mode;
            if (active) ImGui::PopStyleColor();
        };
        view_button("Grid", AssetBrowserViewMode::Grid);
        ImGui::SameLine();
        view_button("List", AssetBrowserViewMode::List);
        ImGui::SameLine();
        view_button("Details", AssetBrowserViewMode::Details);
        if (m_view_mode == AssetBrowserViewMode::Grid) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderFloat(
                "##asset_size", &m_thumbnail_size, 48.0f, 112.0f, "Tiles %.0f"
            );
        }

        AssetBrowserQueryOptions query_options;
        query_options.text = m_search_query.data();
        query_options.scope = m_search_scope;
        query_options.type = m_type_filter;
        query_options.status = m_status_filter;
        query_options.sort = m_sort;
        query_options.ascending = m_sort_ascending;
        const std::vector<AssetBrowserEntry> entries = m_asset_database
            ? AssetBrowserQuery::collect(
                *m_asset_database, m_root_path, m_current_path, query_options
            )
            : std::vector<AssetBrowserEntry>{};

        ImGui::TextDisabled(
            "%zu result%s%s",
            entries.size(),
            entries.size() == 1 ? "" : "s",
            m_search_scope == AssetBrowserScope::Project ? " across project" : ""
        );
        if (!m_revealed_reference.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("| Referenced: %s", m_revealed_reference.c_str());
        }

        if (entries.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("No assets match the current search and filters.");
        } else if (m_view_mode == AssetBrowserViewMode::Grid) {
            const float cell_size = m_thumbnail_size + m_padding;
            const float panel_width = ImGui::GetContentRegionAvail().x;
            const int column_count = std::max(1, static_cast<int>(panel_width / cell_size));
            if (ImGui::BeginTable("AssetGrid", column_count)) {
                for (const AssetBrowserEntry& entry : entries) {
                    ImGui::TableNextColumn();
                    draw_entry(entry);
                }
                ImGui::EndTable();
            }
        } else if (m_view_mode == AssetBrowserViewMode::List) {
            if (ImGui::BeginTable(
                    "AssetList", 1,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
                )) {
                for (const AssetBrowserEntry& entry : entries) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    draw_list_entry(entry);
                }
                ImGui::EndTable();
            }
        } else {
            draw_details_entries(entries);
        }
        draw_selected_asset_details();

        if (!m_selected_file.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_F2)) {
            const auto selected = std::find_if(entries.begin(), entries.end(), [this](const auto& entry) {
                return entry.absolute_path.string() == m_selected_file;
            });
            if (selected != entries.end()) request_rename(*selected);
        }

        // --- 3. Context Menu on Blank Space ---
        if (ImGui::BeginPopupContextWindow("AssetBrowserContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("+ New Scene")) {
                m_show_new_scene_popup = true;
            }
            if (ImGui::MenuItem("+ New Script")) {
                m_show_new_script_popup = true;
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
                static_cast<void>(SDL_OpenURL(file_url(m_current_path).c_str()));
            }
            ImGui::EndPopup();
        }

        // --- 4. Modal Popup: New Scene Dialog ---
        if (m_show_new_scene_popup) {
            ImGui::OpenPopup("Create New Scene");
            m_show_new_scene_popup = false;
        }
        if (m_show_new_script_popup) {
            ImGui::OpenPopup("Create New Script");
            m_show_new_script_popup = false;
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
        if (m_show_rename_popup) {
            ImGui::OpenPopup("Rename Asset");
            m_show_rename_popup = false;
        }

        if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextDisabled("%s", m_rename_source.filename().string().c_str());
            ImGui::SetNextItemWidth(360.0f);
            ImGui::InputText(
                "New Name",
                m_rename_buffer.data(),
                m_rename_buffer.size(),
                ImGuiInputTextFlags_AutoSelectAll
            );
            if (!m_rename_error.empty()) {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.42f, 0.32f, 1.0f),
                    "%s",
                    m_rename_error.c_str()
                );
            }
            const std::string requested_name = m_rename_buffer.data();
            const bool valid_input = !requested_name.empty() &&
                requested_name != "." && requested_name != ".." &&
                requested_name.find('/') == std::string::npos &&
                requested_name.find('\\') == std::string::npos;
            ImGui::BeginDisabled(!valid_input || !m_command_history || !m_asset_database);
            if (ImGui::Button("Rename", ImVec2(120.0f, 0.0f))) {
                std::filesystem::path destination = m_rename_source.parent_path() / requested_name;
                if (destination.extension().empty()) destination += m_rename_source.extension();
                std::error_code error;
                if (destination == m_rename_source) {
                    ImGui::CloseCurrentPopup();
                } else if (std::filesystem::exists(destination, error)) {
                    m_rename_error = "An asset with that name already exists.";
                } else if (!path_is_within(destination, m_root_path)) {
                    m_rename_error = "The destination must stay inside Assets.";
                } else if (m_command_history->execute_command(std::make_unique<MoveAssetCommand>(
                        m_asset_database, m_rename_source, destination
                    ))) {
                    m_selected_file = destination.string();
                    m_rename_error.clear();
                    ImGui::CloseCurrentPopup();
                } else {
                    m_rename_error = "The asset could not be renamed.";
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                m_rename_error.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
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
        if (ImGui::BeginPopupModal("Create New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Enter Script Name:");
            ImGui::InputText("##scriptname", m_new_script_name_buf, sizeof(m_new_script_name_buf));
            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                std::string name = m_new_script_name_buf;
                if (!name.empty() && m_command_history && m_asset_database) {
                    if (!name.ends_with(".luau")) name += ".luau";
                    const std::filesystem::path file = m_current_path / name;
                    m_command_history->execute_command(std::make_unique<CreateAssetCommand>(
                        m_asset_database, file, std::string(DEFAULT_SCRIPT_TEMPLATE)
                    ));
                    if (const AssetRecord* created = m_asset_database->find_by_path(file)) {
                        reveal_asset_path(created->source_path);
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Create New Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Enter Material Name:");
            ImGui::InputText("##materialname", m_new_material_name_buf, sizeof(m_new_material_name_buf));
            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                const std::string name = m_new_material_name_buf;
                if (!name.empty()) static_cast<void>(create_material_asset(name));
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
            SDL_Texture* editor_preview = render_material_preview(
                m_edit_material_handle, m_edit_material_preview_material
            );
            if (editor_preview) {
                draw_material_preview_image(editor_preview, {46.0f, 46.0f});
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(320.0f);
            if (ImGui::InputText("Name", m_edit_material_name_buf, sizeof(m_edit_material_name_buf))) {
                m_edit_material_draft.name = m_edit_material_name_buf;
            }
            const AssetRecord* current_shader = m_edit_material_draft.shader_handle.is_valid() && m_asset_database
                ? m_asset_database->find(m_edit_material_draft.shader_handle) : nullptr;
            bool preview_changed = false;
            if (ImGui::BeginCombo("Shader", current_shader ? current_shader->source_path.c_str() : "Missing shader")) {
                if (m_asset_database) {
                    for (const AssetRecord& record : m_asset_database->records()) {
                        if (record.type != AssetType::Shader || record.status != AssetStatus::Ready) continue;
                        const auto candidate = ShaderAsset::read(record.handle, *m_asset_database);
                        if (!candidate) continue;
                        const bool selected = record.handle == m_edit_material_draft.shader_handle;
                        if (ImGui::Selectable(record.source_path.c_str(), selected)) {
                            m_edit_material_draft.shader_handle = record.handle;
                            m_edit_material_draft.uniforms = ShaderAsset::make_default_uniforms(*candidate);
                            preview_changed = true;
                            m_edit_material_uniform_count = m_edit_material_draft.uniforms.size();
                            for (std::size_t index = 0; index < m_edit_material_uniform_count; ++index) {
                                const std::string text = format_bytes(m_edit_material_draft.uniforms[index]);
                                std::snprintf(
                                    m_edit_material_uniforms[index].data(),
                                    m_edit_material_uniforms[index].size(), "%s", text.c_str()
                                );
                            }
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            const AssetRecord* shader = m_edit_material_draft.shader_handle.is_valid() && m_asset_database
                ? m_asset_database->find(m_edit_material_draft.shader_handle) : nullptr;
            const auto shader_description = shader && m_asset_database
                ? ShaderAsset::read(shader->handle, *m_asset_database)
                : std::nullopt;
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
            ImGui::EndGroup();
            ImGui::Separator();
            if (shader_description) {
                for (std::size_t index = 0; index < shader_description->uniform_buffers.size(); ++index) {
                    const auto& layout = shader_description->uniform_buffers[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TextDisabled("%s", layout.name.c_str());
                    if (!layout.parameters.empty() && index < m_edit_material_draft.uniforms.size()) {
                        for (const auto& parameter : layout.parameters) {
                            preview_changed |= draw_uniform_parameter(
                                parameter, m_edit_material_draft.uniforms[index]
                            );
                        }
                    } else if (index < m_edit_material_uniform_count) {
                        ImGui::TextDisabled("This shader exposes no friendly properties for this buffer.");
                        if (ImGui::InputText(
                                "Raw bytes", m_edit_material_uniforms[index].data(),
                                m_edit_material_uniforms[index].size()
                            )) {
                            std::vector<std::byte> parsed;
                            if (parse_hex_bytes(m_edit_material_uniforms[index].data(), parsed) &&
                                parsed.size() == layout.size) {
                                m_edit_material_draft.uniforms[index] = std::move(parsed);
                                m_edit_material_error.clear();
                                preview_changed = true;
                            } else {
                                m_edit_material_error = "Raw bytes must match the shader buffer size.";
                            }
                        }
                    }
                    ImGui::PopID();
                }
            } else {
                ImGui::TextDisabled("Select a valid shader to edit its properties.");
            }
            if (preview_changed && m_asset_database) {
                m_edit_material_draft.name = m_edit_material_name_buf;
                std::string preview_error;
                const auto preview = MaterialAsset::instantiate(
                    m_edit_material_draft, *m_asset_database, &preview_error
                );
                if (preview) {
                    m_edit_material_preview_material = preview;
                    if (m_on_material_preview) m_on_material_preview(m_edit_material_handle, preview);
                    m_edit_material_preview_active = true;
                    m_edit_material_error.clear();
                } else {
                    m_edit_material_error = preview_error;
                }
            }
            if (!m_edit_material_error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", m_edit_material_error.c_str());
            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120, 0)) && m_asset_database && m_command_history) {
                m_edit_material_draft.name = m_edit_material_name_buf;
                const auto content = MaterialAsset::serialize(
                    m_edit_material_draft, *m_asset_database, &m_edit_material_error
                );
                const AssetRecord* material = m_asset_database->find(m_edit_material_handle);
                if (content && material) {
                    const std::filesystem::path path = m_asset_database->resolve(m_edit_material_handle);
                    const std::string before = FileSystem::read_text(path.string());
                    if (before != *content) {
                        m_command_history->execute_command(std::make_unique<ModifyAssetCommand>(
                            m_asset_database, path, before, *content, "Edit Material"
                        ));
                    }
                    if (FileSystem::read_text(path.string()) == *content) {
                        m_edit_material_preview_active = false;
                        m_edit_material_preview_material.reset();
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                if (m_edit_material_preview_active && m_on_material_preview_reset) {
                    m_on_material_preview_reset();
                }
                if (m_asset_database) {
                    static_cast<void>(render_material_preview(
                        m_edit_material_handle,
                        MaterialAsset::load(m_edit_material_handle, *m_asset_database)
                    ));
                }
                m_edit_material_preview_active = false;
                m_edit_material_preview_material.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Engine
