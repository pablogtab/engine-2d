#include "AssetBrowserPanel.hpp"
#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/Log.hpp"
#include "../../core/FileSystem.hpp"
#include "../../ecs/RenderSystem.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../particles/ParticleEffectAsset.hpp"
#include "../../renderer/Renderer2D.hpp"
#include "../../renderer/postprocess/PostProcessProfile.hpp"
#include "../../renderer/RenderLayerEffectProfile.hpp"
#include "../../renderer/SpriteSortKey.hpp"
#include "../../renderer/material/MaterialAsset.hpp"
#include "../../renderer/material/SDLGPUMaterialBackend.hpp"
#include "../../renderer/material/ShaderAsset.hpp"
#include "../../scene/EntitySubtreeArchive.hpp"
#include "../../scene/PrefabSerializer.hpp"
#include "../../scene/Scene.hpp"
#include "../commands/AssetFileCommands.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EditorCommandGroup.hpp"
#include "../commands/PostProcessProfileCommands.hpp"
#include "../inspector/AssetDropResolver.hpp"
#include "../inspector/MaterialInspectorWidget.hpp"
#include "../scripting/ScriptTemplates.hpp"
#include "../tools/ViewportNavigation.hpp"
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
#include <limits>
#include <sstream>
#include <nlohmann/json.hpp>

namespace Engine {

namespace {

constexpr const char* kAssetBrowserMultiPayload = "ASSET_BROWSER_MULTI";

std::string encode_paths(const std::vector<std::filesystem::path>& paths) {
    std::string payload;
    for (const auto& path : paths) {
        const std::string value = path.string();
        payload.append(value);
        payload.push_back('\0');
    }
    return payload;
}

std::vector<std::filesystem::path> decode_paths(const ImGuiPayload& payload) {
    std::vector<std::filesystem::path> paths;
    if (!payload.Data || payload.DataSize <= 0) return paths;
    const char* begin = static_cast<const char*>(payload.Data);
    const char* end = begin + payload.DataSize;
    while (begin < end) {
        const char* terminator = std::find(begin, end, '\0');
        if (terminator != begin) paths.emplace_back(std::string(begin, terminator));
        if (terminator == end) break;
        begin = terminator + 1;
    }
    return paths;
}

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
        case AssetType::ParticleEffect: return ImVec4(1.00f, 0.65f, 0.20f, 1.0f);
        case AssetType::Texture: return ImVec4(0.40f, 0.75f, 1.00f, 1.0f);
        case AssetType::Audio: return ImVec4(0.90f, 0.45f, 0.90f, 1.0f);
        case AssetType::Shader: return ImVec4(0.90f, 0.55f, 0.30f, 1.0f);
        case AssetType::Font: return ImVec4(0.65f, 0.85f, 0.65f, 1.0f);
        case AssetType::Data: return ImVec4(0.90f, 0.65f, 0.35f, 1.0f);
        case AssetType::Unknown: return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
    }
    return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
}

EditorIcon asset_icon(const AssetBrowserEntry& entry) {
    if (entry.is_directory) return EditorIcon::Folder;
    switch (entry.type) {
        case AssetType::Texture: return EditorIcon::Sprite;
        case AssetType::Material: return EditorIcon::Material;
        case AssetType::Script: return EditorIcon::Edit;
        case AssetType::ParticleEffect: return EditorIcon::Zap;
        case AssetType::Scene: return EditorIcon::Sprite;
        case AssetType::Prefab: return EditorIcon::Component;
        case AssetType::PostProcessProfile:
        case AssetType::RenderLayerEffectProfile: return EditorIcon::Settings;
        case AssetType::Shader: return EditorIcon::Zap;
        case AssetType::Font:
        case AssetType::Audio:
        case AssetType::Data:
        case AssetType::Unknown: return EditorIcon::File;
    }
    return EditorIcon::File;
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
    m_preferences_path = (m_asset_database ? m_asset_database->project_root() : m_root_path.parent_path()) /
        "ProjectSettings/AssetBrowser.json";
    load_preferences();
}

AssetBrowserPanel::~AssetBrowserPanel() {
    save_preferences();
    destroy_asset_previews();
}

void AssetBrowserPanel::set_renderer(SDL_Renderer* renderer) {
    if (m_renderer == renderer) return;
    destroy_asset_previews();
    m_prefab_preview_renderer.reset();
    m_renderer = renderer;
}

void AssetBrowserPanel::set_material_backend(std::shared_ptr<IMaterialBackend> backend) {
    m_material_backend = std::move(backend);
    if (m_prefab_preview_renderer) {
        m_prefab_preview_renderer->set_material_backend(m_material_backend);
    }
}

void AssetBrowserPanel::on_render_resources_invalidated() {
    destroy_asset_previews();
    m_prefab_preview_renderer.reset();
}

void AssetBrowserPanel::load_preferences() {
    const AssetBrowserPersistentState state = AssetBrowserState::load(m_preferences_path);
    m_view_mode = state.view_mode;
    // Collections are intentionally absent from the streamlined browser.
    // Never restore an invisible Favorites/Recent filter from an older session.
    m_collection = AssetBrowserCollection::All;
    m_search_scope = state.scope;
    m_type_filter = state.type_filter;
    m_status_filter = state.status_filter;
    m_sort = state.sort;
    m_sort_ascending = state.sort_ascending;
    m_thumbnail_size = state.thumbnail_size;
    m_favorites = state.favorites;
    m_recent = state.recent;
    if (m_collection != AssetBrowserCollection::All) {
        m_search_scope = AssetBrowserScope::Project;
    }
    if (m_asset_database) {
        AssetBrowserState::prune_missing(m_favorites, *m_asset_database);
        AssetBrowserState::prune_missing(m_recent, *m_asset_database);
    }
}

void AssetBrowserPanel::save_preferences() const {
    AssetBrowserPersistentState state;
    state.view_mode = m_view_mode;
    state.collection = m_collection;
    state.scope = m_search_scope;
    state.type_filter = m_type_filter;
    state.status_filter = m_status_filter;
    state.sort = m_sort;
    state.sort_ascending = m_sort_ascending;
    state.thumbnail_size = m_thumbnail_size;
    state.favorites = m_favorites;
    state.recent = m_recent;
    static_cast<void>(AssetBrowserState::save(m_preferences_path, state));
}

bool AssetBrowserPanel::is_selected(const AssetBrowserEntry& entry) const {
    return m_selected_files.contains(entry.absolute_path.string());
}

void AssetBrowserPanel::clear_selection() {
    m_selected_file.clear();
    m_selected_files.clear();
    m_selection_anchor.clear();
}

void AssetBrowserPanel::select_only(const AssetBrowserEntry& entry) {
    m_selected_files.clear();
    m_selected_file = entry.absolute_path.string();
    m_selected_files.insert(m_selected_file);
    m_selection_anchor = m_selected_file;
    m_revealed_reference.clear();
}

void AssetBrowserPanel::select_entry(const AssetBrowserEntry& entry) {
    const std::string path = entry.absolute_path.string();
    const ImGuiIO& io = ImGui::GetIO();
    const bool toggle = io.KeySuper || io.KeyCtrl;
    if (io.KeyShift && !m_selection_anchor.empty()) {
        const auto anchor = std::find(
            m_visible_entry_paths.begin(), m_visible_entry_paths.end(), m_selection_anchor
        );
        const auto target = std::find(m_visible_entry_paths.begin(), m_visible_entry_paths.end(), path);
        if (anchor != m_visible_entry_paths.end() && target != m_visible_entry_paths.end()) {
            if (!toggle) m_selected_files.clear();
            auto first = anchor < target ? anchor : target;
            auto last = anchor < target ? target : anchor;
            for (; first <= last; ++first) m_selected_files.insert(*first);
            m_selected_file = path;
            m_revealed_reference.clear();
            return;
        }
    }
    if (toggle) {
        if (m_selected_files.contains(path)) {
            m_selected_files.erase(path);
            if (m_selected_file == path) {
                m_selected_file = m_selected_files.empty() ? "" : *m_selected_files.begin();
            }
        } else {
            m_selected_files.insert(path);
            m_selected_file = path;
        }
        m_selection_anchor = path;
        m_revealed_reference.clear();
        return;
    }
    select_only(entry);
}

std::vector<std::filesystem::path> AssetBrowserPanel::selected_asset_paths() const {
    std::vector<std::filesystem::path> paths;
    for (const std::string& value : m_selected_files) {
        const std::filesystem::path path(value);
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

void AssetBrowserPanel::move_asset_paths(
    const std::vector<std::filesystem::path>& paths,
    const std::filesystem::path& directory
) {
    m_bulk_action_error.clear();
    if (!m_asset_database || !m_command_history || paths.empty()) return;
    std::error_code directory_error;
    if (!path_is_within(directory, m_root_path) ||
        !std::filesystem::is_directory(directory, directory_error)) {
        m_bulk_action_error = "Move cancelled: the destination is not an Assets folder.";
        return;
    }
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
    std::unordered_set<std::string> destinations;
    for (const auto& source : paths) {
        const std::filesystem::path destination = directory / source.filename();
        if (source.parent_path() == directory) continue;
        std::error_code error;
        if (!path_is_within(source, m_root_path) || !path_is_within(destination, m_root_path) ||
            std::filesystem::exists(destination, error) ||
            !destinations.insert(destination.string()).second) {
            m_bulk_action_error = "Move cancelled: a destination already exists or is outside Assets.";
            return;
        }
        moves.emplace_back(source, destination);
    }
    if (moves.empty()) return;
    auto group = std::make_unique<EditorCommandGroup>(
        moves.size() == 1 ? "Move Asset" : "Move Selected Assets"
    );
    for (const auto& [source, destination] : moves) {
        group->add_command(std::make_unique<MoveAssetCommand>(
            m_asset_database, source, destination
        ));
    }
    if (!m_command_history->execute_command(std::move(group))) {
        m_bulk_action_error = "Move failed; no assets were changed.";
        return;
    }
    clear_selection();
    for (const auto& [source, destination] : moves) {
        (void)source;
        m_selected_files.insert(destination.string());
        m_selected_file = destination.string();
    }
    m_selection_anchor = m_selected_file;
    destroy_asset_previews();
}

std::vector<std::string> AssetBrowserPanel::selected_asset_usages() const {
    std::vector<std::string> usages;
    if (!m_asset_database) return usages;
    for (const auto& path : selected_asset_paths()) {
        const AssetRecord* record = m_asset_database->find_by_path(path);
        if (!record) continue;
        for (const AssetHandle dependent_handle : m_asset_database->dependents(record->handle)) {
            const AssetRecord* dependent = m_asset_database->find(dependent_handle);
            usages.push_back(
                path.filename().string() + " <- " +
                (dependent ? dependent->source_path : dependent_handle.to_string())
            );
        }
        if (m_live_asset_usage_provider) {
            for (std::string usage : m_live_asset_usage_provider(record->handle)) {
                usages.push_back(path.filename().string() + " <- " + std::move(usage));
            }
        }
    }
    std::sort(usages.begin(), usages.end());
    usages.erase(std::unique(usages.begin(), usages.end()), usages.end());
    return usages;
}

void AssetBrowserPanel::delete_selected_assets(bool allow_referenced) {
    m_bulk_action_error.clear();
    const auto paths = selected_asset_paths();
    if (!m_asset_database || !m_command_history || paths.empty()) return;
    auto group = std::make_unique<EditorCommandGroup>(
        paths.size() == 1 ? "Delete Asset" : "Delete Selected Assets"
    );
    for (const auto& path : paths) {
        group->add_command(std::make_unique<DeleteAssetCommand>(
            m_asset_database, path, allow_referenced
        ));
    }
    if (m_command_history->execute_command(std::move(group))) {
        clear_selection();
        AssetBrowserState::prune_missing(m_favorites, *m_asset_database);
        AssetBrowserState::prune_missing(m_recent, *m_asset_database);
        save_preferences();
        destroy_asset_previews();
    } else {
        m_bulk_action_error = "Delete failed; no assets were changed.";
    }
}

void AssetBrowserPanel::toggle_selected_favorites() {
    if (!m_asset_database) return;
    std::vector<AssetHandle> handles;
    for (const auto& path : selected_asset_paths()) {
        if (const AssetRecord* record = m_asset_database->find_by_path(path)) {
            handles.push_back(record->handle);
        }
    }
    const bool all_favorites = !handles.empty() && std::all_of(
        handles.begin(), handles.end(), [this](AssetHandle handle) {
            return AssetBrowserState::contains(m_favorites, handle);
        }
    );
    for (const AssetHandle handle : handles) {
        const bool favorite = AssetBrowserState::contains(m_favorites, handle);
        if (all_favorites == favorite) {
            static_cast<void>(AssetBrowserState::toggle_favorite(m_favorites, handle));
        }
    }
    save_preferences();
}

void AssetBrowserPanel::record_recent(AssetHandle handle) {
    AssetBrowserState::record_recent(m_recent, handle);
    save_preferences();
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

SDL_Texture* AssetBrowserPanel::preview_asset(const AssetBrowserEntry& entry) {
    if (!m_renderer || entry.is_directory || entry.status != AssetStatus::Ready) return nullptr;
    if (entry.type == AssetType::Texture) {
        auto& preview = m_texture_previews[entry.handle];
        if (!preview) {
            preview = AssetManager::get().load_texture(m_renderer, entry.absolute_path.string());
        }
        return preview ? preview->get_sdl_texture() : nullptr;
    }
    if (entry.type == AssetType::Material) return preview_material_asset(entry.handle);
    if (entry.type == AssetType::Prefab) return render_prefab_preview(entry);
    if (entry.type == AssetType::Scene) return render_entity_asset_preview(entry);
    return nullptr;
}

SDL_Texture* AssetBrowserPanel::render_prefab_preview(const AssetBrowserEntry& entry) {
    if (const auto cached = m_entity_preview_outputs.find(entry.handle);
        cached != m_entity_preview_outputs.end()) return cached->second;
    if (!m_renderer || !m_asset_database) return nullptr;

    const std::string source = PrefabSerializer::read_json_from_file(entry.absolute_path.string());
    if (source.empty()) {
        m_entity_preview_outputs[entry.handle] = nullptr;
        return nullptr;
    }

    Scene preview_scene("Prefab Thumbnail");
    EntityManager& entities = preview_scene.get_entity_manager();
    ComponentSerializationContext context{
        .renderer = m_renderer,
        .asset_manager = &AssetManager::get(),
        .asset_database = m_asset_database
    };
    const SubtreeInstantiationResult instantiated = EntitySubtreeArchive::instantiate_from_json(
        entities, source, std::nullopt, 0, context
    );
    if (!instantiated.success || instantiated.root_entity == 0) {
        m_entity_preview_outputs[entry.handle] = nullptr;
        return nullptr;
    }

    TransformSystem::update(entities);
    std::vector<EntityID> visual_entities;
    entities.for_each<TransformComponent, SpriteComponent>(
        [&visual_entities](
            EntityID entity, const TransformComponent& transform, const SpriteComponent& sprite
        ) {
            const glm::vec2 world_size = sprite.size * glm::abs(transform.world_scale);
            if (transform.world_visible && sprite.visible && sprite.texture &&
                world_size.x > 0.01f && world_size.y > 0.01f) {
                visual_entities.push_back(entity);
            }
        }
    );
    if (visual_entities.empty()) {
        m_entity_preview_outputs[entry.handle] = nullptr;
        return nullptr;
    }

    constexpr int preview_width = 160;
    constexpr int preview_height = 96;
    Camera2D preview_camera(preview_width, preview_height);
    preview_camera.set_reference_size(preview_width, preview_height);
    if (!ViewportNavigation::frame_entities(
            &preview_scene, &preview_camera, visual_entities, 0.12f
        )) {
        m_entity_preview_outputs[entry.handle] = nullptr;
        return nullptr;
    }

    SDL_Texture* output = SDL_CreateTexture(
        m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
        preview_width, preview_height
    );
    if (!output) return nullptr;
    SDL_SetTextureBlendMode(output, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(output, SDL_SCALEMODE_LINEAR);

    if (!m_prefab_preview_renderer) {
        m_prefab_preview_renderer = std::make_unique<Renderer2D>(m_renderer);
        m_prefab_preview_renderer->set_material_backend(m_material_backend);
    }

    SDL_Texture* previous_target = SDL_GetRenderTarget(m_renderer);
    SDL_Rect previous_viewport{};
    const bool had_viewport = SDL_GetRenderViewport(m_renderer, &previous_viewport);
    float previous_scale_x = 1.0f;
    float previous_scale_y = 1.0f;
    static_cast<void>(SDL_GetRenderScale(m_renderer, &previous_scale_x, &previous_scale_y));
    Uint8 previous_r = 0, previous_g = 0, previous_b = 0, previous_a = 0;
    static_cast<void>(SDL_GetRenderDrawColor(
        m_renderer, &previous_r, &previous_g, &previous_b, &previous_a
    ));

    if (!SDL_SetRenderTarget(m_renderer, output)) {
        SDL_DestroyTexture(output);
        return nullptr;
    }
    static_cast<void>(SDL_SetRenderScale(m_renderer, 1.0f, 1.0f));
    static_cast<void>(SDL_SetRenderViewport(m_renderer, nullptr));
    static_cast<void>(SDL_SetRenderDrawColor(m_renderer, 18, 21, 28, 255));
    static_cast<void>(SDL_RenderClear(m_renderer));

    m_prefab_preview_renderer->set_custom_render_target(output);
    m_prefab_preview_renderer->set_scene_clear_color({18, 21, 28, 255});
    m_prefab_preview_renderer->begin_scene(preview_camera);
    RenderSystem::render(entities, *m_prefab_preview_renderer);
    m_prefab_preview_renderer->end_scene();
    m_prefab_preview_renderer->set_custom_render_target(previous_target);

    static_cast<void>(SDL_SetRenderTarget(m_renderer, previous_target));
    static_cast<void>(SDL_SetRenderScale(m_renderer, previous_scale_x, previous_scale_y));
    if (had_viewport) static_cast<void>(SDL_SetRenderViewport(m_renderer, &previous_viewport));
    static_cast<void>(SDL_SetRenderDrawColor(
        m_renderer, previous_r, previous_g, previous_b, previous_a
    ));

    m_entity_preview_outputs[entry.handle] = output;
    return output;
}

SDL_Texture* AssetBrowserPanel::render_entity_asset_preview(const AssetBrowserEntry& entry) {
    if (const auto cached = m_entity_preview_outputs.find(entry.handle);
        cached != m_entity_preview_outputs.end()) return cached->second;
    if (!m_renderer || !m_asset_database) return nullptr;

    struct PreviewSprite {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        float width{16.0f};
        float height{16.0f};
        float rotation{0.0f};
        float pivot_x{0.5f};
        float pivot_y{0.5f};
        SDL_Color color{125, 175, 235, 255};
        AssetHandle texture_handle{};
        SDL_FRect source{};
        bool use_source{false};
        bool flip_x{false};
        bool flip_y{false};
        std::int16_t sorting_layer{0};
        std::int16_t order_in_layer{0};
    };
    std::vector<PreviewSprite> sprites;
    // PrefabSerializer resolves variants before thumbnail extraction, while
    // scene files can be inspected directly. This remains an editor-only,
    // side-effect-free preview: scripts and physics are never started.
    const std::string source = entry.type == AssetType::Prefab
        ? PrefabSerializer::read_json_from_file(entry.absolute_path.string())
        : FileSystem::read_text(entry.absolute_path.string());
    const nlohmann::json root = nlohmann::json::parse(source, nullptr, false);
    if (root.is_discarded() || !root.contains("entities") || !root["entities"].is_array()) {
        m_entity_preview_outputs[entry.handle] = nullptr;
        return nullptr;
    }
    try {
    for (const auto& entity : root["entities"]) {
        if (!entity.is_object() || !entity.contains("transform") ||
            !entity.contains("sprite") || !entity["sprite"].is_object()) continue;
        const auto& transform = entity["transform"];
        if (!transform.value("visible", true)) continue;
        const auto position = transform.value("position", nlohmann::json::array());
        if (!position.is_array() || position.size() < 2 ||
            !position[0].is_number() || !position[1].is_number()) continue;
        PreviewSprite item;
        item.x = position[0].get<float>();
        item.y = position[1].get<float>();
        if (position.size() >= 3 && position[2].is_number()) item.z = position[2].get<float>();
        item.rotation = transform.value("rotation", 0.0f);
        const auto transform_scale = transform.value("scale", nlohmann::json::array());
        float scale_x = 1.0f;
        float scale_y = 1.0f;
        if (transform_scale.is_array() && transform_scale.size() >= 2 &&
            transform_scale[0].is_number() && transform_scale[1].is_number()) {
            scale_x = std::abs(transform_scale[0].get<float>());
            scale_y = std::abs(transform_scale[1].get<float>());
        }
        const auto pivot = transform.value("pivot", nlohmann::json::array());
        if (pivot.is_array() && pivot.size() >= 2 &&
            pivot[0].is_number() && pivot[1].is_number()) {
            item.pivot_x = pivot[0].get<float>();
            item.pivot_y = pivot[1].get<float>();
        }

        const auto& sprite = entity["sprite"];
        if (!sprite.value("visible", true)) continue;
        const auto size = sprite.value("size", nlohmann::json::array());
        if (size.is_array() && size.size() >= 2 && size[0].is_number() && size[1].is_number()) {
            item.width = std::abs(size[0].get<float>()) * scale_x;
            item.height = std::abs(size[1].get<float>()) * scale_y;
        }
        if (item.width < 0.01f || item.height < 0.01f) continue;
        const auto color = sprite.value("color", nlohmann::json::array());
        if (color.is_array() && color.size() >= 4) {
            item.color = {
                color[0].get<std::uint8_t>(), color[1].get<std::uint8_t>(),
                color[2].get<std::uint8_t>(), color[3].get<std::uint8_t>()
            };
        }
        item.sorting_layer = static_cast<std::int16_t>(sprite.value("sorting_layer", 0));
        item.order_in_layer = static_cast<std::int16_t>(sprite.value("order_in_layer", 0));
        item.flip_x = sprite.value("flip_x", false);
        item.flip_y = sprite.value("flip_y", false);
        if (const auto handle = AssetHandle::from_string(
                sprite.value("texture_handle", std::string{})
            )) item.texture_handle = *handle;
        const auto source_rect = sprite.value("src_rect", nlohmann::json::array());
        if (sprite.value("use_src_rect", false) && source_rect.is_array() &&
            source_rect.size() >= 4) {
            item.source = {
                source_rect[0].get<float>(), source_rect[1].get<float>(),
                source_rect[2].get<float>(), source_rect[3].get<float>()
            };
            item.use_source = item.source.w > 0.0f && item.source.h > 0.0f;
        }
        sprites.push_back(item);
    }
    } catch (const nlohmann::json::exception&) {
        m_entity_preview_outputs[entry.handle] = nullptr;
        return nullptr;
    }

    std::stable_sort(sprites.begin(), sprites.end(), [](const auto& left, const auto& right) {
        return SpriteSortKey::world(
            left.sorting_layer, left.z, left.y, left.order_in_layer
        ) < SpriteSortKey::world(
            right.sorting_layer, right.z, right.y, right.order_in_layer
        );
    });

    constexpr int preview_width = 160;
    constexpr int preview_height = 96;
    SDL_Texture* output = SDL_CreateTexture(
        m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
        preview_width, preview_height
    );
    if (!output) return nullptr;
    SDL_SetTextureBlendMode(output, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(output, SDL_SCALEMODE_LINEAR);

    SDL_Texture* previous_target = SDL_GetRenderTarget(m_renderer);
    SDL_Rect previous_viewport{};
    const bool had_viewport = SDL_GetRenderViewport(m_renderer, &previous_viewport);
    float previous_scale_x = 1.0f;
    float previous_scale_y = 1.0f;
    static_cast<void>(SDL_GetRenderScale(m_renderer, &previous_scale_x, &previous_scale_y));
    Uint8 previous_r = 0, previous_g = 0, previous_b = 0, previous_a = 0;
    static_cast<void>(SDL_GetRenderDrawColor(
        m_renderer, &previous_r, &previous_g, &previous_b, &previous_a
    ));
    if (!SDL_SetRenderTarget(m_renderer, output)) {
        SDL_DestroyTexture(output);
        return nullptr;
    }
    static_cast<void>(SDL_SetRenderScale(m_renderer, 1.0f, 1.0f));
    static_cast<void>(SDL_SetRenderViewport(m_renderer, nullptr));
    static_cast<void>(SDL_SetRenderDrawColor(m_renderer, 18, 21, 28, 255));
    static_cast<void>(SDL_RenderClear(m_renderer));

    const auto sprite_bounds = [](const PreviewSprite& sprite) {
        const float left = -sprite.pivot_x * sprite.width;
        const float right = (1.0f - sprite.pivot_x) * sprite.width;
        const float top = -sprite.pivot_y * sprite.height;
        const float bottom = (1.0f - sprite.pivot_y) * sprite.height;
        const float radians = sprite.rotation * 3.14159265358979323846f / 180.0f;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const std::array<ImVec2, 4> corners{{
            {left, top}, {right, top}, {right, bottom}, {left, bottom}
        }};
        const float screen_y = -sprite.y;
        std::array<float, 4> bounds{
            std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()
        };
        for (const ImVec2& corner : corners) {
            const float x = sprite.x + corner.x * cosine - corner.y * sine;
            const float y = screen_y + corner.x * sine + corner.y * cosine;
            bounds[0] = std::min(bounds[0], x);
            bounds[1] = std::max(bounds[1], x);
            bounds[2] = std::min(bounds[2], y);
            bounds[3] = std::max(bounds[3], y);
        }
        return bounds;
    };

    float minimum_x = 0.0f, maximum_x = 1.0f, minimum_y = 0.0f, maximum_y = 1.0f;
    if (!sprites.empty()) {
        const auto first_bounds = sprite_bounds(sprites.front());
        minimum_x = first_bounds[0];
        maximum_x = first_bounds[1];
        minimum_y = first_bounds[2];
        maximum_y = first_bounds[3];
        for (const auto& sprite : sprites) {
            const auto bounds = sprite_bounds(sprite);
            minimum_x = std::min(minimum_x, bounds[0]);
            maximum_x = std::max(maximum_x, bounds[1]);
            minimum_y = std::min(minimum_y, bounds[2]);
            maximum_y = std::max(maximum_y, bounds[3]);
        }
    }
    const float world_width = std::max(1.0f, maximum_x - minimum_x);
    const float world_height = std::max(1.0f, maximum_y - minimum_y);
    const float scale = std::min(
        (preview_width - 12.0f) / world_width,
        (preview_height - 12.0f) / world_height
    );
    const float offset_x = (preview_width - world_width * scale) * 0.5f;
    const float offset_y = (preview_height - world_height * scale) * 0.5f;
    for (const auto& sprite : sprites) {
        SDL_FRect destination{
            offset_x + (sprite.x - sprite.width * sprite.pivot_x - minimum_x) * scale,
            offset_y + (-sprite.y - sprite.height * sprite.pivot_y - minimum_y) * scale,
            std::max(2.0f, sprite.width * scale),
            std::max(2.0f, sprite.height * scale)
        };
        SDL_Texture* texture = nullptr;
        if (sprite.texture_handle.is_valid()) {
            if (const AssetRecord* texture_record = m_asset_database->find(sprite.texture_handle);
                texture_record && texture_record->status == AssetStatus::Ready &&
                texture_record->type == AssetType::Texture) {
                auto& cached_texture = m_texture_previews[sprite.texture_handle];
                if (!cached_texture) cached_texture = AssetManager::get().load_texture(
                    m_renderer, m_asset_database->resolve(sprite.texture_handle)
                );
                if (cached_texture) texture = cached_texture->get_sdl_texture();
            }
        }
        if (texture) {
            Uint8 old_r = 255, old_g = 255, old_b = 255, old_a = 255;
            static_cast<void>(SDL_GetTextureColorMod(texture, &old_r, &old_g, &old_b));
            static_cast<void>(SDL_GetTextureAlphaMod(texture, &old_a));
            static_cast<void>(SDL_SetTextureColorMod(
                texture, sprite.color.r, sprite.color.g, sprite.color.b
            ));
            static_cast<void>(SDL_SetTextureAlphaMod(texture, sprite.color.a));
            const SDL_FPoint pivot{
                destination.w * sprite.pivot_x,
                destination.h * sprite.pivot_y
            };
            const SDL_FlipMode flip = static_cast<SDL_FlipMode>(
                (sprite.flip_x ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) |
                (sprite.flip_y ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE)
            );
            static_cast<void>(SDL_RenderTextureRotated(
                m_renderer, texture, sprite.use_source ? &sprite.source : nullptr,
                &destination, sprite.rotation, &pivot, flip
            ));
            static_cast<void>(SDL_SetTextureColorMod(texture, old_r, old_g, old_b));
            static_cast<void>(SDL_SetTextureAlphaMod(texture, old_a));
        } else {
            static_cast<void>(SDL_SetRenderDrawColor(
                m_renderer, sprite.color.r, sprite.color.g, sprite.color.b, sprite.color.a
            ));
            static_cast<void>(SDL_RenderFillRect(m_renderer, &destination));
        }
    }

    static_cast<void>(SDL_SetRenderTarget(m_renderer, previous_target));
    static_cast<void>(SDL_SetRenderScale(m_renderer, previous_scale_x, previous_scale_y));
    if (had_viewport) static_cast<void>(SDL_SetRenderViewport(m_renderer, &previous_viewport));
    static_cast<void>(SDL_SetRenderDrawColor(
        m_renderer, previous_r, previous_g, previous_b, previous_a
    ));
    m_entity_preview_outputs[entry.handle] = output;
    return output;
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

void AssetBrowserPanel::destroy_asset_previews() {
    destroy_material_previews();
    for (auto& [handle, texture] : m_entity_preview_outputs) {
        (void)handle;
        if (texture) SDL_DestroyTexture(texture);
    }
    m_entity_preview_outputs.clear();
    m_texture_previews.clear();
}

void AssetBrowserPanel::refresh() {
    if (m_asset_database) {
        [[maybe_unused]] const AssetScanResult scan_result = m_asset_database->scan(m_root_path);
        AssetBrowserState::prune_missing(m_favorites, *m_asset_database);
        AssetBrowserState::prune_missing(m_recent, *m_asset_database);
        save_preferences();
    }
    destroy_asset_previews();
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
    clear_selection();
    m_selected_file = target.string();
    m_selected_files.insert(m_selected_file);
    m_selection_anchor = m_selected_file;
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
    m_collection = AssetBrowserCollection::All;
    m_search_scope = AssetBrowserScope::CurrentFolder;
    clear_selection();
    m_bulk_action_error.clear();
    save_preferences();
}

void AssetBrowserPanel::activate_entry(const AssetBrowserEntry& entry) {
    if (entry.is_directory) {
        navigate_to(entry.absolute_path);
        return;
    }
    if (entry.status != AssetStatus::Ready) return;
    record_recent(entry.handle);
    const std::string& extension = entry.extension;
    if (extension == ".scene") {
        if (m_on_load_scene) m_on_load_scene(entry.absolute_path.string());
    } else if (extension == ".prefab") {
        if (m_on_open_prefab) m_on_open_prefab(entry.absolute_path.string());
    } else if (extension == ".e2s") {
        open_script_externally(entry.absolute_path);
    } else if (extension == ".material") {
        open_material_asset(entry.handle);
    } else if (extension == ".particle") {
        if (m_on_open_particle_effect) m_on_open_particle_effect(entry.handle);
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
    const auto& palette = EditorTheme::palette();
    const auto& metrics = EditorTheme::metrics();
    ImGui::PushID(entry.project_path.c_str());
    const ImVec2 card_size{m_thumbnail_size + 14.0f, m_thumbnail_size + 38.0f};
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##asset_card", card_size)) select_entry(entry);
    const ImVec2 maximum{minimum.x + card_size.x, minimum.y + card_size.y};
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const bool selected = is_selected(entry);
    const bool hovered = ImGui::IsItemHovered();
    draw_list->AddRectFilled(
        minimum, maximum,
        ImGui::ColorConvertFloat4ToU32(
            selected ? palette.selected : (hovered ? palette.header : palette.panel_raised)
        ),
        metrics.small_rounding
    );
    const ImVec2 preview_min{minimum.x + 7.0f, minimum.y + 7.0f};
    const ImVec2 preview_max{
        preview_min.x + m_thumbnail_size,
        preview_min.y + m_thumbnail_size
    };
    draw_list->AddRectFilled(
        preview_min, preview_max,
        ImGui::ColorConvertFloat4ToU32(palette.window), metrics.small_rounding
    );
    draw_list->AddRect(
        preview_min, preview_max,
        ImGui::ColorConvertFloat4ToU32(palette.border), metrics.small_rounding
    );
    SDL_Texture* preview = ImGui::IsItemVisible() ? preview_asset(entry) : nullptr;
    if (preview) {
        float width = 1.0f;
        float height = 1.0f;
        static_cast<void>(SDL_GetTextureSize(preview, &width, &height));
        const float scale = std::min(
            m_thumbnail_size / std::max(1.0f, width),
            m_thumbnail_size / std::max(1.0f, height)
        );
        const ImVec2 size{width * scale, height * scale};
        const ImVec2 image_min{
            preview_min.x + (m_thumbnail_size - size.x) * 0.5f,
            preview_min.y + (m_thumbnail_size - size.y) * 0.5f
        };
        draw_list->AddImage(
            preview,
            image_min,
            {image_min.x + size.x, image_min.y + size.y}
        );
    } else {
        draw_editor_icon(
            draw_list, asset_icon(entry),
            ImVec2(
                preview_min.x + m_thumbnail_size * 0.5f,
                preview_min.y + m_thumbnail_size * 0.5f
            ),
            25.0f * metrics.scale,
            ImGui::ColorConvertFloat4ToU32(asset_tag_color(entry))
        );
    }
    if (!entry.is_directory && entry.status != AssetStatus::Ready) {
        draw_list->AddCircleFilled(
            ImVec2(preview_max.x - 7.0f, preview_min.y + 7.0f),
            3.5f * metrics.scale,
            ImGui::ColorConvertFloat4ToU32(palette.error)
        );
    }
    if (AssetBrowserState::contains(m_favorites, entry.handle)) {
        draw_editor_icon(
            draw_list, EditorIcon::Star,
            ImVec2(preview_min.x + 9.0f, preview_min.y + 9.0f),
            12.0f * metrics.scale,
            ImGui::ColorConvertFloat4ToU32(palette.warning)
        );
    }
    draw_list->PushClipRect(
        {minimum.x + 5.0f, preview_max.y + 3.0f},
        {maximum.x - 5.0f, maximum.y - 3.0f},
        true
    );
    const std::string display_name = entry.is_directory
        ? entry.name : entry.absolute_path.stem().string();
    const ImVec2 name_size = ImGui::CalcTextSize(display_name.c_str());
    draw_list->AddText(
        {minimum.x + std::max(6.0f, (card_size.x - name_size.x) * 0.5f), preview_max.y + 7.0f},
        ImGui::GetColorU32(ImGuiCol_Text), display_name.c_str()
    );
    draw_list->PopClipRect();
    draw_list->AddRect(
        minimum, maximum,
        ImGui::ColorConvertFloat4ToU32(selected ? palette.accent_hovered : palette.border),
        metrics.small_rounding, 0, selected ? 2.0f : 1.0f
    );
    draw_entry_interactions(entry);
    ImGui::PopID();
}

void AssetBrowserPanel::draw_entry_interactions(const AssetBrowserEntry& entry) {
    const std::filesystem::path& path = entry.absolute_path;
    const bool hovered = ImGui::IsItemHovered();
    const bool double_clicked = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool right_clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (!entry.is_directory && entry.status == AssetStatus::Ready &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        std::vector<std::filesystem::path> paths = is_selected(entry)
            ? selected_asset_paths()
            : std::vector<std::filesystem::path>{path};
        if (paths.size() > 1) {
            const std::string payload = encode_paths(paths);
            ImGui::SetDragDropPayload(
                kAssetBrowserMultiPayload, payload.data(), payload.size()
            );
            ImGui::Text("%zu assets", paths.size());
        } else {
            const std::string path_string = path.string();
            ImGui::SetDragDropPayload(
                kContentBrowserAssetPayload, path_string.c_str(), path_string.length() + 1
            );
            ImGui::Text("Asset: %s", entry.name.c_str());
        }
        ImGui::EndDragDropSource();
    }

    if (double_clicked) activate_entry(entry);

    if (entry.is_directory) draw_folder_drop_target(path);

    if (right_clicked && !is_selected(entry)) {
        select_only(entry);
    }

    if (ImGui::BeginPopupContextItem()) {
        if (entry.is_directory) {
            if (ImGui::MenuItem("Open Folder")) navigate_to(path);
        } else {
            const bool ready = entry.status == AssetStatus::Ready;
            ImGui::BeginDisabled(!ready);
            if (ImGui::MenuItem("Open")) activate_entry(entry);
            if (entry.type == AssetType::Prefab &&
                ImGui::MenuItem("Open in VFX Preview")) {
                record_recent(entry.handle);
                if (m_on_open_vfx_preview) m_on_open_vfx_preview(entry.handle);
            }
            if (entry.type == AssetType::Material && ImGui::MenuItem("Duplicate Material") &&
                m_command_history && m_asset_database) {
                static_cast<void>(duplicate_material_asset(entry.handle));
            }
            ImGui::BeginDisabled(m_selected_files.size() != 1);
            if (ImGui::MenuItem("Rename...")) request_rename(entry);
            ImGui::EndDisabled();
            ImGui::EndDisabled();

            const bool favorite = AssetBrowserState::contains(m_favorites, entry.handle);
            if (ImGui::MenuItem(favorite ? "Remove from Favorites" : "Add to Favorites")) {
                if (m_selected_files.size() > 1) toggle_selected_favorites();
                else {
                    static_cast<void>(AssetBrowserState::toggle_favorite(
                        m_favorites, entry.handle
                    ));
                    save_preferences();
                }
            }

            if (ImGui::MenuItem("Reveal in Folder")) {
                navigate_to(path.parent_path());
                m_search_query.fill('\0');
                m_type_filter = AssetBrowserTypeFilter::All;
                m_status_filter = AssetBrowserStatusFilter::All;
                select_only(entry);
            }
            if (ImGui::MenuItem("Copy Project Path")) {
                ImGui::SetClipboardText(entry.project_path.c_str());
            }
            if (entry.handle.is_valid() && ImGui::MenuItem("Copy Asset Handle")) {
                ImGui::SetClipboardText(entry.handle.to_string().c_str());
            }
            ImGui::BeginDisabled(!ready || !m_command_history || !m_asset_database);
            if (ImGui::MenuItem(m_selected_files.size() > 1 ? "Delete Selected..." : "Delete...")) {
                m_show_delete_confirmation = true;
            }
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }

    if (hovered && ImGui::GetDragDropPayload() == nullptr) {
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
}

void AssetBrowserPanel::draw_list_entry(const AssetBrowserEntry& entry) {
    ImGui::PushID(entry.project_path.c_str());
    const bool selected = is_selected(entry);
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable(
            "##asset_list_entry", selected, ImGuiSelectableFlags_SpanAllColumns,
            ImVec2(0.0f, EditorTheme::metrics().row_height)
        )) {
        select_entry(entry);
    }
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float row_height = ImGui::GetItemRectSize().y;
    draw_editor_icon(
        draw_list, asset_icon(entry), ImVec2(row_min.x + 10.0f, row_min.y + row_height * 0.5f),
        13.0f * EditorTheme::metrics().scale,
        ImGui::ColorConvertFloat4ToU32(asset_tag_color(entry))
    );
    draw_list->AddText(
        ImVec2(row_min.x + 23.0f, row_min.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_Text), entry.name.c_str()
    );
    if (!entry.is_directory && entry.status != AssetStatus::Ready) {
        draw_list->AddCircleFilled(
            ImVec2(ImGui::GetItemRectMax().x - 10.0f, row_min.y + row_height * 0.5f),
            3.0f, ImGui::ColorConvertFloat4ToU32(EditorTheme::palette().error)
        );
    }
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
            save_preferences();
        }
        specs->SpecsDirty = false;
    }

    for (const AssetBrowserEntry& entry : entries) {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, 24.0f);
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(entry.project_path.c_str());
        const bool selected = is_selected(entry);
        const ImVec2 name_min = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable(
                "##asset_details_entry", selected,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                ImVec2(0.0f, 22.0f)
            )) {
            select_entry(entry);
        }
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_editor_icon(
            draw_list, asset_icon(entry), ImVec2(name_min.x + 10.0f, name_min.y + 11.0f),
            13.0f * EditorTheme::metrics().scale,
            ImGui::ColorConvertFloat4ToU32(asset_tag_color(entry))
        );
        draw_list->AddText(
            ImVec2(name_min.x + 23.0f, name_min.y + (22.0f - ImGui::GetTextLineHeight()) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), entry.name.c_str()
        );
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

void AssetBrowserPanel::draw_folder_drop_target(const std::filesystem::path& directory) {
    if (!ImGui::BeginDragDropTarget()) return;
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
        kAssetBrowserMultiPayload, ImGuiDragDropFlags_AcceptBeforeDelivery
    );
    std::vector<std::filesystem::path> paths;
    if (payload) {
        paths = decode_paths(*payload);
    } else if (const ImGuiPayload* single = ImGui::AcceptDragDropPayload(
            kContentBrowserAssetPayload, ImGuiDragDropFlags_AcceptBeforeDelivery
        )) {
        paths = decode_paths(*single);
        payload = single;
    }
    if (payload) {
        bool valid = !paths.empty();
        for (const auto& source : paths) {
            const std::filesystem::path destination = directory / source.filename();
            std::error_code error;
            if (!path_is_within(source, m_root_path) || source.parent_path() == directory ||
                std::filesystem::exists(destination, error)) {
                valid = false;
                break;
            }
        }
        ImGui::BeginTooltip();
        if (valid) {
            ImGui::TextColored(
                ImVec4(0.35f, 0.85f, 0.50f, 1.0f),
                "Move %zu asset%s to %s",
                paths.size(), paths.size() == 1 ? "" : "s",
                directory.filename().string().c_str()
            );
            if (payload->IsDelivery()) move_asset_paths(paths, directory);
        } else {
            ImGui::TextColored(
                ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                "Assets cannot be moved to this folder"
            );
        }
        ImGui::EndTooltip();
    }
    ImGui::EndDragDropTarget();
}

void AssetBrowserPanel::draw_folder_tree_node(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> children;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(directory, error), end;
         iterator != end && !error; iterator.increment(error)) {
        if (!iterator->is_symlink(error) && iterator->is_directory(error)) {
            children.push_back(iterator->path());
        }
    }
    std::sort(children.begin(), children.end(), [](const auto& left, const auto& right) {
        return left.filename().string() < right.filename().string();
    });

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (m_collection == AssetBrowserCollection::All &&
        m_search_scope == AssetBrowserScope::CurrentFolder && directory == m_current_path) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (directory == m_root_path) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::PushID(directory.string().c_str());
    const std::string folder_label = editor_icon_label(
        EditorIcon::Folder, directory.filename().string().c_str()
    );
    const bool open = ImGui::TreeNodeEx(folder_label.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        m_collection = AssetBrowserCollection::All;
        navigate_to(directory);
        save_preferences();
    }
    draw_folder_drop_target(directory);
    if (open) {
        for (const auto& child : children) draw_folder_tree_node(child);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void AssetBrowserPanel::draw_folder_tree() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 3.0f));
    EditorUi::section_heading("FOLDERS");
    draw_folder_tree_node(m_root_path);
    ImGui::PopStyleVar();
}

void AssetBrowserPanel::draw_selected_asset_details() {
    if (m_selected_files.size() > 1) {
        ImGui::Separator();
        ImGui::Text("%zu assets selected", m_selected_files.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Toggle Favorites")) toggle_selected_favorites();
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy Paths")) {
            std::string paths;
            for (const auto& path : selected_asset_paths()) {
                if (!paths.empty()) paths += '\n';
                paths += path.lexically_relative(
                    m_asset_database ? m_asset_database->project_root() : m_root_path.parent_path()
                ).generic_string();
            }
            ImGui::SetClipboardText(paths.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete...")) m_show_delete_confirmation = true;
        return;
    }
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
    if (record->type == AssetType::Prefab && m_on_open_vfx_preview) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Preview VFX")) {
            record_recent(record->handle);
            m_on_open_vfx_preview(record->handle);
        }
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

void AssetBrowserPanel::draw_toolbar() {
    const auto& metrics = EditorTheme::metrics();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float icon_button_size = metrics.toolbar_height - 6.0f * metrics.scale;

    std::vector<std::string> breadcrumb_parts;
    const std::filesystem::path relative = m_current_path.lexically_relative(m_root_path);
    for (const auto& part : relative) {
        if (part != ".") breadcrumb_parts.push_back(part.string());
    }

    const auto text_button_width = [](const std::string& text) {
        return ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    };
    float navigation_width = icon_button_size * 2.0f + spacing * 3.0f +
        text_button_width(m_root_path.filename().string());
    for (const std::string& part : breadcrumb_parts) {
        navigation_width += ImGui::CalcTextSize(">").x + spacing * 2.0f +
            text_button_width(part);
    }

    constexpr float sort_width = 105.0f;
    constexpr float direction_width = 42.0f;
    const float controls_without_search = sort_width + direction_width +
        icon_button_size * 4.0f + spacing * 7.0f;
    const float available_width = ImGui::GetContentRegionAvail().x;
    const bool single_row = available_width >=
        navigation_width + controls_without_search + 170.0f * metrics.scale;

    const auto draw_navigation = [this, &breadcrumb_parts]() {
        const bool at_root = m_current_path == m_root_path;
        if (EditorUi::toolbar_button(
                "asset_up", EditorIcon::ArrowUp, "Parent folder", false, !at_root
            )) {
            navigate_to(m_current_path.parent_path());
        }
        ImGui::SameLine();

        std::filesystem::path breadcrumb = m_root_path;
        ImGui::PushID("asset_breadcrumb_root");
        if (ImGui::Button(m_root_path.filename().string().c_str())) navigate_to(m_root_path);
        ImGui::PopID();
        int breadcrumb_index = 0;
        for (const std::string& part : breadcrumb_parts) {
            breadcrumb /= part;
            ImGui::SameLine();
            ImGui::TextDisabled(">");
            ImGui::SameLine();
            ImGui::PushID(breadcrumb_index++);
            if (ImGui::Button(part.c_str())) navigate_to(breadcrumb);
            ImGui::PopID();
        }
        ImGui::SameLine();
        if (EditorUi::toolbar_button("asset_refresh", EditorIcon::Refresh, "Refresh assets")) {
            refresh();
        }
    };

    const auto draw_search = [this](float width) {
        const ImGuiIO& io = ImGui::GetIO();
        const bool focus_search = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            (io.KeySuper || io.KeyCtrl) && ImGui::IsKeyPressed(ImGuiKey_F);
        if (focus_search) {
            m_search_scope = AssetBrowserScope::Project;
            ImGui::SetKeyboardFocusHere();
        }
        EditorUi::search_field(
            "##asset_search",
            "Search assets; t:, path:, ext:, status:, id:...",
            m_search_query.data(),
            m_search_query.size(),
            width
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
    };

    const auto draw_sort = [this, sort_width, direction_width, icon_button_size]() {
        ImGui::SetNextItemWidth(sort_width);
        if (ImGui::BeginCombo("##asset_sort", AssetBrowserQuery::sort_name(m_sort))) {
            for (int value = static_cast<int>(AssetBrowserSort::Name);
                 value <= static_cast<int>(AssetBrowserSort::Size); ++value) {
                const auto sort = static_cast<AssetBrowserSort>(value);
                if (ImGui::Selectable(AssetBrowserQuery::sort_name(sort), sort == m_sort)) {
                    m_sort = sort;
                    save_preferences();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(
                m_sort_ascending ? "A-Z" : "Z-A",
                ImVec2(direction_width, icon_button_size)
            )) {
            m_sort_ascending = !m_sort_ascending;
            save_preferences();
        }
    };

    const auto draw_filters = [this]() {
        const bool filtered = m_type_filter != AssetBrowserTypeFilter::All ||
            m_status_filter != AssetBrowserStatusFilter::All ||
            (m_collection == AssetBrowserCollection::All &&
             m_search_scope == AssetBrowserScope::Project);
        if (EditorUi::toolbar_button(
                "asset_filters", EditorIcon::Settings, "Search scope and filters", filtered
            )) {
            ImGui::OpenPopup("AssetFilters");
        }
        if (ImGui::BeginPopup("AssetFilters")) {
            ImGui::TextUnformatted("Asset filters");
            ImGui::Separator();
            ImGui::SetNextItemWidth(180.0f);
            const char* scope_name = m_search_scope == AssetBrowserScope::CurrentFolder
                ? "Current folder" : "Entire project";
            if (ImGui::BeginCombo("Scope", scope_name)) {
                if (ImGui::Selectable(
                        "Current folder", m_search_scope == AssetBrowserScope::CurrentFolder
                    )) {
                    m_collection = AssetBrowserCollection::All;
                    m_search_scope = AssetBrowserScope::CurrentFolder;
                    save_preferences();
                }
                if (ImGui::Selectable(
                        "Entire project", m_search_scope == AssetBrowserScope::Project
                    )) {
                    m_collection = AssetBrowserCollection::All;
                    m_search_scope = AssetBrowserScope::Project;
                    save_preferences();
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::BeginCombo(
                    "Type", AssetBrowserQuery::type_filter_name(m_type_filter)
                )) {
                for (int value = static_cast<int>(AssetBrowserTypeFilter::All);
                     value <= static_cast<int>(AssetBrowserTypeFilter::Other); ++value) {
                    const auto filter = static_cast<AssetBrowserTypeFilter>(value);
                    if (ImGui::Selectable(
                            AssetBrowserQuery::type_filter_name(filter), filter == m_type_filter
                        )) {
                        m_type_filter = filter;
                        save_preferences();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::BeginCombo(
                    "Status", AssetBrowserQuery::status_filter_name(m_status_filter)
                )) {
                for (int value = static_cast<int>(AssetBrowserStatusFilter::All);
                     value <= static_cast<int>(AssetBrowserStatusFilter::Issues); ++value) {
                    const auto filter = static_cast<AssetBrowserStatusFilter>(value);
                    if (ImGui::Selectable(
                            AssetBrowserQuery::status_filter_name(filter), filter == m_status_filter
                        )) {
                        m_status_filter = filter;
                        save_preferences();
                    }
                }
                ImGui::EndCombo();
            }
            const bool has_filter = m_type_filter != AssetBrowserTypeFilter::All ||
                m_status_filter != AssetBrowserStatusFilter::All;
            ImGui::BeginDisabled(!has_filter);
            if (ImGui::Button("Clear filters")) {
                m_type_filter = AssetBrowserTypeFilter::All;
                m_status_filter = AssetBrowserStatusFilter::All;
                save_preferences();
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    };

    const auto draw_view = [this]() {
        const auto view_button = [this](
            const char* id, EditorIcon icon, const char* tooltip, AssetBrowserViewMode mode
        ) {
            if (EditorUi::toolbar_button(id, icon, tooltip, m_view_mode == mode)) {
                m_view_mode = mode;
                save_preferences();
            }
        };
        view_button("asset_grid", EditorIcon::Grid, "Grid view", AssetBrowserViewMode::Grid);
        ImGui::SameLine();
        view_button("asset_list", EditorIcon::List, "List view", AssetBrowserViewMode::List);
        ImGui::SameLine();
        view_button(
            "asset_details", EditorIcon::Details, "Details view", AssetBrowserViewMode::Details
        );
    };

    draw_navigation();
    if (single_row) ImGui::SameLine();

    const float search_width = std::max(
        140.0f * metrics.scale,
        ImGui::GetContentRegionAvail().x - controls_without_search
    );
    draw_search(search_width);
    ImGui::SameLine();
    draw_sort();
    ImGui::SameLine();
    draw_filters();
    ImGui::SameLine();
    draw_view();
}

void AssetBrowserPanel::on_gui() {
    if (!is_open) {
        m_is_focused = false;
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    if (ImGui::Begin("Asset Browser", &is_open)) {
        m_is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        draw_toolbar();
        const ImGuiIO& io = ImGui::GetIO();

        AssetBrowserQueryOptions query_options;
        query_options.text = m_search_query.data();
        query_options.scope = m_collection == AssetBrowserCollection::All
            ? m_search_scope : AssetBrowserScope::Project;
        query_options.type = m_type_filter;
        query_options.status = m_status_filter;
        query_options.sort = m_sort;
        query_options.ascending = m_sort_ascending;
        std::vector<AssetBrowserEntry> entries = m_asset_database
            ? AssetBrowserQuery::collect(
                *m_asset_database, m_root_path, m_current_path, query_options
            )
            : std::vector<AssetBrowserEntry>{};

        if (m_collection == AssetBrowserCollection::Favorites) {
            std::erase_if(entries, [this](const AssetBrowserEntry& entry) {
                return entry.is_directory ||
                    !AssetBrowserState::contains(m_favorites, entry.handle);
            });
        } else if (m_collection == AssetBrowserCollection::Recent) {
            std::erase_if(entries, [this](const AssetBrowserEntry& entry) {
                return entry.is_directory || !AssetBrowserState::contains(m_recent, entry.handle);
            });
            std::stable_sort(entries.begin(), entries.end(), [this](const auto& left, const auto& right) {
                const auto left_index = std::find(m_recent.begin(), m_recent.end(), left.handle);
                const auto right_index = std::find(m_recent.begin(), m_recent.end(), right.handle);
                return left_index < right_index;
            });
        }
        m_visible_entry_paths.clear();
        for (const auto& entry : entries) {
            m_visible_entry_paths.push_back(entry.absolute_path.string());
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput && (io.KeySuper || io.KeyCtrl) &&
            ImGui::IsKeyPressed(ImGuiKey_A)) {
            m_selected_files.clear();
            for (const auto& entry : entries) {
                m_selected_files.insert(entry.absolute_path.string());
            }
            if (!entries.empty()) {
                m_selected_file = entries.back().absolute_path.string();
                m_selection_anchor = entries.front().absolute_path.string();
            }
        }

        if (ImGui::BeginTable(
                "AssetBrowserLayout", 2,
                ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV
            )) {
            ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            draw_folder_tree();
            ImGui::TableSetColumnIndex(1);

        ImGui::TextDisabled(
            "%zu result%s%s",
            entries.size(),
            entries.size() == 1 ? "" : "s",
            query_options.scope == AssetBrowserScope::Project ? " across project" : ""
        );
        if (!m_revealed_reference.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("| Referenced: %s", m_revealed_reference.c_str());
        }
        if (!m_bulk_action_error.empty()) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.42f, 0.32f, 1.0f),
                "%s",
                m_bulk_action_error.c_str()
            );
        }

        if (entries.empty()) {
            EditorUi::empty_state(
                EditorIcon::Sprite,
                "No matching assets",
                "Change the search, collection, or filters"
            );
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

        // Match desktop asset browsers: clicking unoccupied panel space clears
        // the current asset selection without stealing clicks from controls,
        // rows, cards, scrollbars, or popup menus.
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
            clear_selection();
        }

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
            if (ImGui::MenuItem("+ New Particle Effect")) {
                m_show_new_particle_popup = true;
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
        ImGui::EndTable();
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
        if (m_show_new_particle_popup) {
            ImGui::OpenPopup("Create New Particle Effect");
            m_show_new_particle_popup = false;
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
        if (m_show_delete_confirmation) {
            ImGui::OpenPopup("Delete Assets");
            m_show_delete_confirmation = false;
        }

        if (ImGui::BeginPopupModal("Delete Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto paths = selected_asset_paths();
            const auto usages = selected_asset_usages();
            ImGui::Text("Delete %zu asset%s?", paths.size(), paths.size() == 1 ? "" : "s");
            ImGui::TextDisabled("This operation can be undone from the editor history.");
            if (!usages.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(
                    ImVec4(0.95f, 0.48f, 0.30f, 1.0f),
                    "Warning: selected assets are still in use."
                );
                ImGui::TextWrapped(
                    "Deleting them will leave missing references in scenes, prefabs or components."
                );
                ImGui::BeginChild(
                    "ReferencedAssetUsages",
                    ImVec2(520.0f, std::min(150.0f, 24.0f * static_cast<float>(usages.size()) + 8.0f)),
                    true
                );
                for (const std::string& usage : usages) ImGui::BulletText("%s", usage.c_str());
                ImGui::EndChild();
            }
            const char* delete_label = usages.empty() ? "Delete" : "Delete Anyway";
            if (ImGui::Button(delete_label, ImVec2(140.0f, 0.0f))) {
                delete_selected_assets(!usages.empty());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
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
                    clear_selection();
                    m_selected_file = destination.string();
                    m_selected_files.insert(m_selected_file);
                    m_selection_anchor = m_selected_file;
                    destroy_asset_previews();
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
                    if (!name.ends_with(".e2s")) name += ".e2s";
                    const std::filesystem::path file = m_current_path / name;
                    const auto template_str = DEFAULT_ENGINESCRIPT_TEMPLATE;
                    m_command_history->execute_command(std::make_unique<CreateAssetCommand>(
                        m_asset_database, file, std::string(template_str)
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
        if (ImGui::BeginPopupModal("Create New Particle Effect", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Enter Particle Effect Name:");
            ImGui::InputText("##particlename", m_new_particle_name_buf, sizeof(m_new_particle_name_buf));
            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(120, 0))) {
                const std::string name = m_new_particle_name_buf;
                if (!name.empty() && m_command_history && m_asset_database) {
                    ParticleEffectAsset asset;
                    asset.name = name;
                    m_command_history->execute_command(std::make_unique<CreateAssetCommand>(
                        m_asset_database, m_current_path / (name + ".particle"), asset.to_json().dump(2)
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
        // Material parameters are tuned against the live scene preview. Keep the
        // modal interaction model, but do not wash out the viewport behind it.
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        const bool edit_material_open = ImGui::BeginPopupModal(
            "Edit Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize
        );
        ImGui::PopStyleColor();
        if (edit_material_open) {
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
                std::size_t material_index = 0;
                for (std::size_t index = 0; index < shader_description->uniform_buffers.size(); ++index) {
                    const auto& layout = shader_description->uniform_buffers[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TextDisabled("%s", layout.name.c_str());
                    if (layout.source == ShaderUniformBufferSource::Engine) {
                        ImGui::TextDisabled("Provided automatically by the engine.");
                        ImGui::PopID();
                        continue;
                    }
                    if (!layout.parameters.empty() && material_index < m_edit_material_draft.uniforms.size()) {
                        for (const auto& parameter : layout.parameters) {
                            preview_changed |= draw_uniform_parameter(
                                parameter, m_edit_material_draft.uniforms[material_index]
                            );
                        }
                    } else if (material_index < m_edit_material_uniform_count) {
                        ImGui::TextDisabled("This shader exposes no friendly properties for this buffer.");
                        if (ImGui::InputText(
                                "Raw bytes", m_edit_material_uniforms[material_index].data(),
                                m_edit_material_uniforms[material_index].size()
                            )) {
                            std::vector<std::byte> parsed;
                            if (parse_hex_bytes(m_edit_material_uniforms[material_index].data(), parsed) &&
                                parsed.size() == layout.size) {
                                m_edit_material_draft.uniforms[material_index] = std::move(parsed);
                                m_edit_material_error.clear();
                                preview_changed = true;
                            } else {
                                m_edit_material_error = "Raw bytes must match the shader buffer size.";
                            }
                        }
                    }
                    ++material_index;
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
    } else {
        m_is_focused = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Engine
