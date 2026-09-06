#include "LevelComposerPanel.hpp"

#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/Log.hpp"
#include "../../renderer/Texture.hpp"
#include "../state/EditorEntityStateStore.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <vector>

namespace Engine {

namespace {

constexpr const char* CONTENT_BROWSER_ASSET = "CONTENT_BROWSER_ASSET";
constexpr const char* ASSET_BROWSER_MULTI = "ASSET_BROWSER_MULTI";

bool is_palette_path(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".levelpalette";
}

bool path_is_inside(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path normalized_path =
        std::filesystem::absolute(path).lexically_normal();
    const std::filesystem::path normalized_root =
        std::filesystem::absolute(root).lexically_normal();
    const std::filesystem::path relative = normalized_path.lexically_relative(normalized_root);
    return normalized_path == normalized_root ||
        (!relative.empty() && relative.begin()->string() != "..");
}

std::vector<std::filesystem::path> decode_asset_paths(const ImGuiPayload& payload) {
    std::vector<std::filesystem::path> paths;
    if (!payload.Data || payload.DataSize <= 0) return paths;
    const char* cursor = static_cast<const char*>(payload.Data);
    const char* end = cursor + payload.DataSize;
    while (cursor < end) {
        const char* terminator = std::find(cursor, end, '\0');
        if (terminator != cursor) paths.emplace_back(std::string(cursor, terminator));
        if (terminator == end) break;
        cursor = terminator + 1;
    }
    return paths;
}

int layer_preset_for(const LevelPaletteEntry& entry) {
    constexpr std::int16_t layers[]{-10, -5, 0, 1, 3};
    for (int index = 0; index < 5; ++index) {
        if (entry.placement.sorting_layer == layers[index]) return index + 1;
    }
    return 0;
}

int clamped_cell_origin(float coordinate, int margin, int cell, int spacing, int limit) {
    const int safe_cell = std::max(1, cell);
    const int stride = safe_cell + std::max(0, spacing);
    const float after_margin = std::max(0.0f, coordinate - static_cast<float>(std::max(0, margin)));
    const int index = static_cast<int>(std::floor(after_margin / static_cast<float>(stride)));
    return std::clamp(std::max(0, margin) + index * stride, 0, std::max(0, limit - 1));
}

SDL_FRect normalized_rect(glm::vec2 first, glm::vec2 second) {
    return {
        std::min(first.x, second.x),
        std::min(first.y, second.y),
        std::abs(second.x - first.x),
        std::abs(second.y - first.y)
    };
}

void clamp_region(glm::ivec4& rect, int texture_width, int texture_height) {
    rect.z = std::clamp(rect.z, 1, std::max(1, texture_width));
    rect.w = std::clamp(rect.w, 1, std::max(1, texture_height));
    rect.x = std::clamp(rect.x, 0, std::max(0, texture_width - rect.z));
    rect.y = std::clamp(rect.y, 0, std::max(0, texture_height - rect.w));
}

bool same_rect(const glm::ivec4& lhs, const glm::ivec4& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

const char* entry_kind_label(LevelPaletteEntryKind kind) {
    return kind == LevelPaletteEntryKind::SpriteRegion ? "ATLAS" : "PREFAB";
}

} // namespace

LevelComposerPanel::LevelComposerPanel(
    SDL_Renderer* renderer,
    SceneManager* scene_manager,
    std::shared_ptr<EditorSelection> selection
) : m_renderer(renderer),
    m_scene_manager(scene_manager),
    m_selection(std::move(selection)) {
    std::snprintf(
        m_palette_path_buf.data(), m_palette_path_buf.size(),
        "%s", "Assets/LevelPalettes/Level.levelpalette"
    );
    ensure_default_categories();
    std::snprintf(
        m_palette_name_buf.data(), m_palette_name_buf.size(), "%s", m_palette.name.c_str()
    );
    m_clean_palette = m_palette;
    const std::filesystem::path default_palette =
        AssetDatabase::get().project_root() / "Assets/LevelPalettes/Level.levelpalette";
    if (std::filesystem::is_regular_file(default_palette)) {
        static_cast<void>(load_palette(default_palette));
    }
}

void LevelComposerPanel::on_context_changed(const EditorPanelContext& context) {
    if (m_selection && m_selection != context.selection) {
        m_target_parent = 0;
        m_placement_active = false;
    }
    m_selection = context.selection;
    m_entity_state_store = context.entity_state_store;
}

void LevelComposerPanel::on_render_resources_invalidated() {
    // Texture ownership belongs to AssetManager. The panel resolves previews on
    // demand, so there is no stale SDL resource to retain here.
}

void LevelComposerPanel::ensure_default_categories() {
    if (!m_palette.categories.empty()) return;
    m_palette.categories = {
        {"ground", "Ground"},
        {"architecture", "Architecture"},
        {"decoration", "Decoration"},
        {"prefabs", "Prefabs"}
    };
}

const LevelPaletteEntry* LevelComposerPanel::active_entry() const {
    if (!m_selected_entry || *m_selected_entry >= m_palette.entries.size()) return nullptr;
    return &m_palette.entries[*m_selected_entry];
}

bool LevelComposerPanel::is_entry_asset_ready(const LevelPaletteEntry& entry) const {
    const LevelPaletteAssetReference& reference =
        entry.kind == LevelPaletteEntryKind::SpriteRegion
        ? entry.sprite_region.texture
        : entry.prefab.prefab;
    const AssetType expected_type = entry.kind == LevelPaletteEntryKind::SpriteRegion
        ? AssetType::Texture
        : AssetType::Prefab;
    const auto& database = AssetDatabase::get();
    const AssetRecord* record = reference.handle.is_valid()
        ? database.find(reference.handle)
        : nullptr;
    if (!record && !reference.path.empty()) record = database.find_by_path(reference.path);
    return record && record->type == expected_type && record->status == AssetStatus::Ready;
}

std::vector<std::string> LevelComposerPanel::live_asset_usages(AssetHandle handle) const {
    std::vector<std::string> usages;
    if (!m_palette_dirty || !handle.is_valid()) return usages;

    const auto& database = AssetDatabase::get();
    for (const LevelPaletteEntry& entry : m_palette.entries) {
        const LevelPaletteAssetReference& reference =
            entry.kind == LevelPaletteEntryKind::SpriteRegion
            ? entry.sprite_region.texture
            : entry.prefab.prefab;
        const AssetRecord* referenced = reference.handle.is_valid()
            ? database.find(reference.handle)
            : nullptr;
        if (!referenced && !reference.path.empty()) {
            referenced = database.find_by_path(reference.path);
        }
        if (referenced && referenced->handle == handle) {
            usages.push_back(
                "Unsaved Level Palette '" + m_palette.name + "' / stamp '" +
                (entry.label.empty() ? entry.id : entry.label) + "'"
            );
        }
    }
    return usages;
}

bool LevelComposerPanel::is_placement_active() const {
    const LevelPaletteEntry* entry = active_entry();
    return is_open && m_placement_active && entry && is_entry_asset_ready(*entry);
}

void LevelComposerPanel::rotate_active(float degrees) {
    if (!m_selected_entry || *m_selected_entry >= m_palette.entries.size()) return;
    auto& rotation = m_palette.entries[*m_selected_entry].placement.rotation_degrees;
    rotation = std::fmod(rotation + degrees, 360.0f);
    if (rotation < 0.0f) rotation += 360.0f;
    m_palette_dirty = true;
}

void LevelComposerPanel::toggle_active_flip_x() {
    if (!m_selected_entry || *m_selected_entry >= m_palette.entries.size()) return;
    auto& entry = m_palette.entries[*m_selected_entry];
    if (entry.kind != LevelPaletteEntryKind::SpriteRegion) return;
    entry.placement.flip_x = !entry.placement.flip_x;
    m_palette_dirty = true;
}

void LevelComposerPanel::toggle_active_flip_y() {
    if (!m_selected_entry || *m_selected_entry >= m_palette.entries.size()) return;
    auto& entry = m_palette.entries[*m_selected_entry];
    if (entry.kind != LevelPaletteEntryKind::SpriteRegion) return;
    entry.placement.flip_y = !entry.placement.flip_y;
    m_palette_dirty = true;
}

void LevelComposerPanel::report_tool_status(std::string message, bool is_error) {
    m_status = std::move(message);
    m_status_is_error = is_error;
}

std::filesystem::path LevelComposerPanel::resolve_palette_path() const {
    std::filesystem::path path(m_palette_path_buf.data());
    if (path.empty()) path = "Assets/LevelPalettes/Level.levelpalette";
    if (!path.has_extension()) path += ".levelpalette";
    if (path.is_relative()) path = AssetDatabase::get().project_root() / path;
    return std::filesystem::absolute(path).lexically_normal();
}

bool LevelComposerPanel::is_project_palette_path(const std::filesystem::path& path) const {
    return is_palette_path(path) && path_is_inside(
        path, AssetDatabase::get().project_root() / "Assets"
    );
}

std::optional<std::filesystem::path> LevelComposerPanel::current_palette_path() const {
    if (!m_current_palette_handle.is_valid()) return std::nullopt;
    const AssetRecord* record = AssetDatabase::get().find(m_current_palette_handle);
    if (!record || record->type != AssetType::LevelPalette) return std::nullopt;
    return (AssetDatabase::get().project_root() / record->source_path).lexically_normal();
}

std::string LevelComposerPanel::current_palette_label() const {
    if (const auto path = current_palette_path()) return path->filename().string();
    return m_palette.name.empty() ? "New Level Palette" : m_palette.name;
}

void LevelComposerPanel::open_palette(const std::filesystem::path& path) {
    const std::filesystem::path normalized = std::filesystem::absolute(path).lexically_normal();
    is_open = true;
    if (!is_project_palette_path(normalized)) {
        report_tool_status("Level palettes must be .levelpalette assets under Assets/.", true);
        return;
    }
    if (m_palette_dirty) {
        m_pending_load_path = normalized;
        m_open_pending_load_modal = true;
        return;
    }
    static_cast<void>(load_palette(normalized));
}

bool LevelComposerPanel::load_palette(const std::filesystem::path& path) {
    if (!is_project_palette_path(path)) {
        report_tool_status("Level palettes must be loaded from this project's Assets folder.", true);
        return false;
    }
    std::string error;
    // Authoring load is intentionally tolerant of missing dependencies so a
    // damaged palette can still be opened, inspected, and repaired. Saving and
    // stamping remain strict about resolved, ready assets.
    auto loaded = LevelPaletteAsset::load_from_file(path, &error);
    if (!loaded) {
        m_status = error.empty() ? "Could not load level palette." : error;
        m_status_is_error = true;
        return false;
    }
    m_palette = std::move(*loaded);
    ensure_default_categories();
    if (m_palette.entries.empty()) {
        m_selected_entry.reset();
        sync_entry_buffers();
    } else {
        select_entry(0);
    }
    m_target_parent = 0;
    m_placement_active = false;
    m_palette_dirty = false;
    std::snprintf(
        m_palette_name_buf.data(), m_palette_name_buf.size(), "%s", m_palette.name.c_str()
    );

    const AssetRecord* record = AssetDatabase::get().find_by_path(path);
    if (!record) {
        if (const auto imported = AssetDatabase::get().import_asset(path)) {
            record = AssetDatabase::get().find(*imported);
        }
    }
    m_current_palette_handle = record && record->type == AssetType::LevelPalette
        ? record->handle
        : AssetHandle{};

    const std::filesystem::path root = AssetDatabase::get().project_root();
    std::filesystem::path displayed = path.lexically_normal();
    const std::filesystem::path relative = displayed.lexically_relative(root);
    if (!relative.empty() && relative.begin()->string() != "..") displayed = relative;
    std::snprintf(
        m_palette_path_buf.data(), m_palette_path_buf.size(), "%s",
        displayed.generic_string().c_str()
    );
    const std::size_t unresolved = static_cast<std::size_t>(std::count_if(
        m_palette.entries.begin(), m_palette.entries.end(),
        [this](const LevelPaletteEntry& entry) { return !is_entry_asset_ready(entry); }
    ));
    if (unresolved == 0) {
        m_status = "Loaded " + path.filename().string();
        m_status_is_error = false;
    } else {
        m_status = "Loaded with " + std::to_string(unresolved) +
            " unavailable stamp" + (unresolved == 1 ? "." : "s.") +
            " Remove or replace before saving.";
        m_status_is_error = true;
    }
    m_clean_palette = m_palette;
    return true;
}

bool LevelComposerPanel::save_palette() {
    if (const auto current = current_palette_path()) return save_palette_to(*current);
    return save_palette_as_requested();
}

bool LevelComposerPanel::save_palette_as_requested() {
    return save_palette_to(resolve_palette_path());
}

bool LevelComposerPanel::save_palette_to(const std::filesystem::path& requested_path) {
    ensure_default_categories();
    m_palette.name = m_palette_name_buf.data();
    std::string error;
    if (!m_palette.canonicalize_references(AssetDatabase::get(), &error)) {
        m_status = error;
        m_status_is_error = true;
        return false;
    }
    const std::filesystem::path path = std::filesystem::absolute(requested_path).lexically_normal();
    if (!is_project_palette_path(path)) {
        report_tool_status("Save the palette as Assets/.../*.levelpalette inside this project.", true);
        return false;
    }
    const AssetRecord* destination_record = AssetDatabase::get().find_by_path(path);
    const bool destination_is_current = destination_record &&
        destination_record->handle == m_current_palette_handle;
    std::error_code exists_error;
    if (!destination_is_current && std::filesystem::exists(path, exists_error)) {
        report_tool_status(
            "Save As refused: that palette already exists. Load it first or choose a new path.",
            true
        );
        return false;
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(path.parent_path(), filesystem_error);
    if (filesystem_error || !m_palette.save_to_file(path, &error, &AssetDatabase::get())) {
        m_status = filesystem_error ? filesystem_error.message() : error;
        m_status_is_error = true;
        return false;
    }
    const auto imported = AssetDatabase::get().import_asset(path);
    const AssetRecord* record = imported ? AssetDatabase::get().find(*imported) : nullptr;
    if (!record || record->type != AssetType::LevelPalette ||
        record->status != AssetStatus::Ready) {
        report_tool_status(
            "Palette file was written, but Asset Database registration failed.", true
        );
        return false;
    }
    m_current_palette_handle = record->handle;
    const std::filesystem::path displayed =
        path.lexically_relative(AssetDatabase::get().project_root());
    std::snprintf(
        m_palette_path_buf.data(), m_palette_path_buf.size(), "%s",
        displayed.generic_string().c_str()
    );
    m_palette_dirty = false;
    m_clean_palette = m_palette;
    m_status = "Saved " + path.filename().string();
    m_status_is_error = false;
    return true;
}

bool LevelComposerPanel::discard_unsaved_changes() {
    // Restore the last successfully loaded/saved state from memory. Reading the
    // file again here could fail after an external edit and must never bypass a
    // destructive-action guard.
    m_palette = m_clean_palette;
    ensure_default_categories();
    if (m_palette.entries.empty()) {
        m_selected_entry.reset();
        sync_entry_buffers();
    } else {
        select_entry(0);
    }
    m_target_parent = 0;
    m_placement_active = false;
    m_palette_dirty = false;
    std::snprintf(
        m_palette_name_buf.data(), m_palette_name_buf.size(), "%s", m_palette.name.c_str()
    );
    report_tool_status("Discarded unsaved palette changes.");
    return true;
}

void LevelComposerPanel::render_pending_load_modal() {
    if (m_open_pending_load_modal) {
        ImGui::OpenPopup("Unsaved level palette");
        m_open_pending_load_modal = false;
    }
    if (!ImGui::BeginPopupModal(
            "Unsaved level palette", nullptr, ImGuiWindowFlags_AlwaysAutoResize
        )) {
        return;
    }
    ImGui::TextWrapped(
        "The current palette has unsaved changes. Save it before opening another palette?"
    );
    ImGui::Spacing();
    if (ImGui::Button("Save and open", ImVec2(120.0f, 0.0f))) {
        if (save_palette() && m_pending_load_path) {
            const std::filesystem::path pending = *m_pending_load_path;
            m_pending_load_path.reset();
            static_cast<void>(load_palette(pending));
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard and open", ImVec2(128.0f, 0.0f))) {
        if (m_pending_load_path) {
            const std::filesystem::path pending = *m_pending_load_path;
            if (load_palette(pending)) {
                m_pending_load_path.reset();
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_pending_load_path.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

std::string LevelComposerPanel::unique_entry_id(std::string base) const {
    for (char& character : base) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isalnum(value)) {
            character = static_cast<char>(std::tolower(value));
        } else {
            character = '_';
        }
    }
    while (!base.empty() && base.back() == '_') base.pop_back();
    if (base.empty()) base = "stamp";

    auto exists = [this](const std::string& id) {
        return std::any_of(m_palette.entries.begin(), m_palette.entries.end(),
            [&id](const LevelPaletteEntry& entry) { return entry.id == id; });
    };
    if (!exists(base)) return base;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = base + "_" + std::to_string(suffix);
        if (!exists(candidate)) return candidate;
    }
    return base + "_copy";
}

void LevelComposerPanel::add_texture_entry(const AssetRecord& record) {
    Texture* texture = AssetManager::get().get_texture(m_renderer, record.source_path);
    if (!texture) {
        m_status = "Texture could not be loaded: " + record.source_path;
        m_status_is_error = true;
        return;
    }

    const int width = std::max(1, texture->get_width());
    const int height = std::max(1, texture->get_height());
    LevelPaletteEntry entry;
    entry.id = unique_entry_id(std::filesystem::path(record.source_path).stem().string());
    entry.label = std::filesystem::path(record.source_path).stem().string();
    entry.category = "architecture";
    entry.kind = LevelPaletteEntryKind::SpriteRegion;
    entry.sprite_region.texture = {record.handle, record.source_path};
    entry.sprite_region.rect_px = {0, 0, width, height};
    entry.sprite_region.world_size = {static_cast<float>(width), static_cast<float>(height)};
    entry.sprite_region.pivot = {0.5f, 0.5f};
    entry.placement.snap = {64.0f, 64.0f};
    entry.placement.spacing = 64.0f;
    m_palette.entries.push_back(std::move(entry));
    m_selected_entry = m_palette.entries.size() - 1;
    m_placement_active = false;
    m_palette_dirty = true;
    m_slice_cell = {
        width >= 4 && width % 4 == 0 ? width / 4 : std::min(256, width),
        height >= 4 && height % 4 == 0 ? height / 4 : std::min(256, height)
    };
    sync_entry_buffers();
    m_status = "Atlas added. Select one or more cells, then enable Stamp.";
    m_status_is_error = false;
}

void LevelComposerPanel::add_prefab_entry(const AssetRecord& record) {
    LevelPaletteEntry entry;
    entry.id = unique_entry_id(std::filesystem::path(record.source_path).stem().string());
    entry.label = std::filesystem::path(record.source_path).stem().string();
    entry.category = "prefabs";
    entry.kind = LevelPaletteEntryKind::Prefab;
    entry.prefab.prefab = {record.handle, record.source_path};
    entry.placement.snap = {64.0f, 64.0f};
    entry.placement.spacing = 64.0f;
    m_palette.entries.push_back(std::move(entry));
    select_entry(m_palette.entries.size() - 1);
    m_palette_dirty = true;
    m_status = "Prefab added to palette.";
    m_status_is_error = false;
}

void LevelComposerPanel::accept_asset_drop() {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(CONTENT_BROWSER_ASSET)) {
        const char* dropped = static_cast<const char*>(payload->Data);
        if (dropped && payload->IsDelivery()) {
            const std::filesystem::path path(dropped);
            if (is_palette_path(path)) {
                open_palette(path);
            } else if (const AssetRecord* record = AssetDatabase::get().find_by_path(path)) {
                if (record->type == AssetType::Texture) {
                    add_texture_entry(*record);
                } else if (record->type == AssetType::Prefab) {
                    add_prefab_entry(*record);
                } else {
                    m_status = "Drop a texture, prefab, or .levelpalette asset.";
                    m_status_is_error = true;
                }
            } else {
                m_status = "Dropped asset is not registered in this project.";
                m_status_is_error = true;
            }
        }
    } else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ASSET_BROWSER_MULTI)) {
        if (payload->IsDelivery()) {
            std::size_t accepted = 0;
            for (const auto& path : decode_asset_paths(*payload)) {
                const AssetRecord* record = AssetDatabase::get().find_by_path(path);
                if (!record) continue;
                if (record->type == AssetType::Texture) {
                    add_texture_entry(*record);
                    ++accepted;
                } else if (record->type == AssetType::Prefab) {
                    add_prefab_entry(*record);
                    ++accepted;
                }
            }
            if (accepted == 0) {
                report_tool_status("The selected assets contain no textures or prefabs.", true);
            } else {
                report_tool_status(
                    "Added " + std::to_string(accepted) + " assets to the palette."
                );
            }
        }
    }
    ImGui::EndDragDropTarget();
}

void LevelComposerPanel::sync_entry_buffers() {
    const LevelPaletteEntry* entry = active_entry();
    std::snprintf(m_entry_label_buf.data(), m_entry_label_buf.size(), "%s", entry ? entry->label.c_str() : "");
    std::snprintf(m_entry_id_buf.data(), m_entry_id_buf.size(), "%s", entry ? entry->id.c_str() : "");
    std::snprintf(m_entry_category_buf.data(), m_entry_category_buf.size(), "%s", entry ? entry->category.c_str() : "");
}

void LevelComposerPanel::apply_entry_buffers(LevelPaletteEntry& entry) {
    entry.label = m_entry_label_buf.data();
    entry.id = m_entry_id_buf.data();
    entry.category = m_entry_category_buf.data();
    m_palette_dirty = true;
}

void LevelComposerPanel::select_entry(std::size_t index) {
    if (index >= m_palette.entries.size()) return;
    m_selected_entry = index;
    sync_entry_buffers();
}

void LevelComposerPanel::duplicate_selected_entry() {
    const LevelPaletteEntry* selected = active_entry();
    if (!selected) return;
    LevelPaletteEntry copy = *selected;
    copy.id = unique_entry_id(copy.id);
    copy.label += " Copy";
    m_palette.entries.push_back(std::move(copy));
    select_entry(m_palette.entries.size() - 1);
    m_placement_active = false;
    m_palette_dirty = true;
}

void LevelComposerPanel::remove_selected_entry() {
    if (!m_selected_entry || *m_selected_entry >= m_palette.entries.size()) return;
    m_palette.entries.erase(m_palette.entries.begin() + static_cast<std::ptrdiff_t>(*m_selected_entry));
    if (m_palette.entries.empty()) {
        m_selected_entry.reset();
    } else {
        m_selected_entry = std::min(*m_selected_entry, m_palette.entries.size() - 1);
    }
    m_placement_active = false;
    sync_entry_buffers();
    m_palette_dirty = true;
}

Texture* LevelComposerPanel::resolve_texture(const LevelPaletteEntry& entry) const {
    if (entry.kind != LevelPaletteEntryKind::SpriteRegion || !m_renderer) return nullptr;
    const auto& reference = entry.sprite_region.texture;
    std::string path;
    if (reference.handle.is_valid()) path = AssetDatabase::get().resolve(reference.handle);
    if (path.empty()) path = reference.path;
    return path.empty() ? nullptr : AssetManager::get().get_texture(m_renderer, path);
}

void LevelComposerPanel::render_palette_toolbar() {
    EditorUi::section_heading("PALETTE", m_palette_dirty ? "UNSAVED" : "PROJECT ASSET");
    if (ImGui::InputText(
            "Palette name", m_palette_name_buf.data(), m_palette_name_buf.size()
        )) {
        m_palette.name = m_palette_name_buf.data();
        m_palette_dirty = true;
    }
    float origin[2]{m_palette.grid_origin.x, m_palette.grid_origin.y};
    if (ImGui::DragFloat2("Grid origin", origin, 1.0f, 0.0f, 0.0f, "%.0f")) {
        m_palette.grid_origin = {origin[0], origin[1]};
        m_palette_dirty = true;
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##level_palette_path", m_palette_path_buf.data(), m_palette_path_buf.size());
    accept_asset_drop();
    if (ImGui::Button("Load", ImVec2(72.0f, 0.0f))) {
        open_palette(resolve_palette_path());
    }
    ImGui::SameLine();
    if (ImGui::Button("Save", ImVec2(72.0f, 0.0f))) {
        static_cast<void>(save_palette());
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As", ImVec2(78.0f, 0.0f))) {
        static_cast<void>(save_palette_as_requested());
    }

    ImGui::Button("Drop texture, prefab, or palette here", ImVec2(-1.0f, 34.0f));
    accept_asset_drop();
    if (!m_status.empty()) {
        const ImVec4 color = m_status_is_error
            ? ImVec4(0.95f, 0.35f, 0.30f, 1.0f)
            : ImVec4(0.35f, 0.80f, 0.55f, 1.0f);
        ImGui::TextColored(color, "%s", m_status.c_str());
    }
}

void LevelComposerPanel::render_entry_list() {
    EditorUi::section_heading("STAMPS", std::to_string(m_palette.entries.size()).c_str());
    ImGui::BeginChild("LevelComposerEntries", ImVec2(0.0f, 154.0f), true);
    for (std::size_t index = 0; index < m_palette.entries.size(); ++index) {
        const LevelPaletteEntry& entry = m_palette.entries[index];
        ImGui::PushID(static_cast<int>(index));
        const bool selected = m_selected_entry && *m_selected_entry == index;
        const std::string label = std::string(entry_kind_label(entry.kind)) + "  " + entry.label + "##entry";
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            select_entry(index);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                is_entry_asset_ready(entry)) {
                m_placement_active = true;
            }
        }
        ImGui::PopID();
    }
    if (m_palette.entries.empty()) {
        ImGui::TextDisabled("Drop a texture or prefab to create the first stamp.");
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(!active_entry());
    if (ImGui::Button("Duplicate")) duplicate_selected_entry();
    ImGui::SameLine();
    if (ImGui::Button("Remove")) remove_selected_entry();
    ImGui::EndDisabled();
}

void LevelComposerPanel::render_parent_target() {
    ImGui::SeparatorText("Placement target");
    Scene* scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
    EntityManager* entities = scene ? &scene->get_entity_manager() : nullptr;
    if (m_target_parent != 0 &&
        (!entities || !entities->is_alive(m_target_parent) ||
         !entities->has_component<TransformComponent>(m_target_parent) ||
         (m_entity_state_store && m_entity_state_store->is_locked(m_target_parent)))) {
        m_target_parent = 0;
        report_tool_status("Placement parent became unavailable; using scene root.", true);
    }

    if (m_target_parent != 0 && entities) {
        const std::string label = entities->has_component<TagComponent>(m_target_parent)
            ? entities->get_component<TagComponent>(m_target_parent).tag
            : "Entity #" + std::to_string(m_target_parent);
        ImGui::Text("Parent: %s (#%u)", label.c_str(), m_target_parent);
    } else {
        ImGui::TextDisabled("Parent: Scene root");
    }
    EntityID candidate = 0;
    if (entities && m_selection && m_selection->count() == 1) {
        candidate = m_selection->get_primary();
    }
    const bool can_capture = candidate != 0 && entities->is_alive(candidate) &&
        entities->has_component<TransformComponent>(candidate) &&
        (!m_entity_state_store || !m_entity_state_store->is_locked(candidate));
    ImGui::BeginDisabled(!can_capture);
    if (ImGui::Button("Use selected as parent")) {
        m_target_parent = candidate;
    }
    ImGui::EndDisabled();
    if (!can_capture && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Select exactly one unlocked entity with a Transform.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_target_parent == 0);
    if (ImGui::Button("Scene root")) m_target_parent = 0;
    ImGui::EndDisabled();
}

void LevelComposerPanel::render_atlas_canvas(LevelPaletteEntry& entry, Texture& texture) {
    const int texture_width = std::max(1, texture.get_width());
    const int texture_height = std::max(1, texture.get_height());
    const glm::ivec4 region_before_clamp = entry.sprite_region.rect_px;
    clamp_region(entry.sprite_region.rect_px, texture_width, texture_height);
    if (!same_rect(region_before_clamp, entry.sprite_region.rect_px)) {
        if (m_auto_size_from_region) {
            entry.sprite_region.world_size = {
                static_cast<float>(entry.sprite_region.rect_px.z),
                static_cast<float>(entry.sprite_region.rect_px.w)
            };
        }
        m_palette_dirty = true;
        report_tool_status("Atlas changed size; the selected region was clamped.", true);
    }

    ImGui::Checkbox("Snap slice to grid", &m_slice_to_grid);
    if (m_slice_to_grid) {
        int cell[2]{m_slice_cell.x, m_slice_cell.y};
        int spacing[2]{m_slice_spacing.x, m_slice_spacing.y};
        int margin[2]{m_slice_margin.x, m_slice_margin.y};
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt2("Cell px", cell)) {
            m_slice_cell = {std::max(1, cell[0]), std::max(1, cell[1])};
        }
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt2("Spacing px", spacing)) {
            m_slice_spacing = {std::max(0, spacing[0]), std::max(0, spacing[1])};
        }
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::InputInt2("Margin px", margin)) {
            m_slice_margin = {std::max(0, margin[0]), std::max(0, margin[1])};
        }
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::SliderFloat("Atlas zoom", &m_atlas_zoom, 0.25f, 4.0f, "%.2fx");

    const float available_width = std::max(120.0f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);
    const float fit = std::min(available_width / static_cast<float>(texture_width),
                               330.0f / static_cast<float>(texture_height));
    const float scale = std::clamp(fit * m_atlas_zoom, 0.03f, 8.0f);
    const ImVec2 image_size(texture_width * scale, texture_height * scale);

    ImGui::BeginChild(
        "LevelComposerAtlasCanvas", ImVec2(0.0f, std::min(360.0f, image_size.y + 18.0f)), true,
        ImGuiWindowFlags_HorizontalScrollbar
    );
    const ImVec2 image_min = ImGui::GetCursorScreenPos();
    const ImVec2 image_max(image_min.x + image_size.x, image_min.y + image_size.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddImage(
        reinterpret_cast<ImTextureID>(texture.get_sdl_texture()), image_min, image_max
    );

    if (m_slice_to_grid) {
        const int stride_x = std::max(1, m_slice_cell.x + m_slice_spacing.x);
        const int stride_y = std::max(1, m_slice_cell.y + m_slice_spacing.y);
        const ImU32 grid_color = IM_COL32(180, 190, 210, 85);
        for (int x = std::max(0, m_slice_margin.x); x <= texture_width; x += stride_x) {
            draw_list->AddLine(
                ImVec2(image_min.x + x * scale, image_min.y),
                ImVec2(image_min.x + x * scale, image_max.y), grid_color
            );
        }
        for (int y = std::max(0, m_slice_margin.y); y <= texture_height; y += stride_y) {
            draw_list->AddLine(
                ImVec2(image_min.x, image_min.y + y * scale),
                ImVec2(image_max.x, image_min.y + y * scale), grid_color
            );
        }
    }

    ImGui::SetCursorScreenPos(image_min);
    ImGui::InvisibleButton(
        "LevelComposerAtlasHitbox", image_size,
        ImGuiButtonFlags_MouseButtonLeft
    );
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const glm::vec2 local(
        std::clamp((mouse.x - image_min.x) / scale, 0.0f, static_cast<float>(texture_width)),
        std::clamp((mouse.y - image_min.y) / scale, 0.0f, static_cast<float>(texture_height))
    );
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_slice_dragging = true;
        m_slice_drag_start = local;
    }
    if (m_slice_dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_slice_dragging = false;
    }
    if (m_slice_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const glm::ivec4 previous_region = entry.sprite_region.rect_px;
        if (m_slice_to_grid) {
            const int start_x = clamped_cell_origin(
                m_slice_drag_start.x, m_slice_margin.x, m_slice_cell.x,
                m_slice_spacing.x, texture_width
            );
            const int start_y = clamped_cell_origin(
                m_slice_drag_start.y, m_slice_margin.y, m_slice_cell.y,
                m_slice_spacing.y, texture_height
            );
            const int end_x = clamped_cell_origin(
                local.x, m_slice_margin.x, m_slice_cell.x,
                m_slice_spacing.x, texture_width
            );
            const int end_y = clamped_cell_origin(
                local.y, m_slice_margin.y, m_slice_cell.y,
                m_slice_spacing.y, texture_height
            );
            const int minimum_x = std::min(start_x, end_x);
            const int minimum_y = std::min(start_y, end_y);
            const int maximum_x = std::max(start_x, end_x) + m_slice_cell.x;
            const int maximum_y = std::max(start_y, end_y) + m_slice_cell.y;
            entry.sprite_region.rect_px = {
                minimum_x, minimum_y,
                maximum_x - minimum_x, maximum_y - minimum_y
            };
        } else {
            const SDL_FRect selection = normalized_rect(m_slice_drag_start, local);
            entry.sprite_region.rect_px = {
                static_cast<int>(std::floor(selection.x)),
                static_cast<int>(std::floor(selection.y)),
                std::max(1, static_cast<int>(std::ceil(selection.w))),
                std::max(1, static_cast<int>(std::ceil(selection.h)))
            };
        }
        clamp_region(entry.sprite_region.rect_px, texture_width, texture_height);
        if (!same_rect(previous_region, entry.sprite_region.rect_px)) {
            if (m_auto_size_from_region) {
                entry.sprite_region.world_size = {
                    static_cast<float>(entry.sprite_region.rect_px.z),
                    static_cast<float>(entry.sprite_region.rect_px.w)
                };
            }
            m_palette_dirty = true;
        }
    }
    if (m_slice_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_slice_dragging = false;
    }

    const glm::ivec4& selected = entry.sprite_region.rect_px;
    const ImVec2 selected_min(
        image_min.x + selected.x * scale,
        image_min.y + selected.y * scale
    );
    const ImVec2 selected_max(
        selected_min.x + selected.z * scale,
        selected_min.y + selected.w * scale
    );
    draw_list->AddRectFilled(selected_min, selected_max, IM_COL32(80, 170, 255, 42));
    draw_list->AddRect(selected_min, selected_max, IM_COL32(80, 190, 255, 255), 0.0f, 0, 2.0f);
    ImGui::EndChild();

    int region[4]{selected.x, selected.y, selected.z, selected.w};
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputInt4("Region X/Y/W/H", region)) {
        entry.sprite_region.rect_px = {region[0], region[1], region[2], region[3]};
        clamp_region(entry.sprite_region.rect_px, texture_width, texture_height);
        if (m_auto_size_from_region) {
            entry.sprite_region.world_size = {
                static_cast<float>(entry.sprite_region.rect_px.z),
                static_cast<float>(entry.sprite_region.rect_px.w)
            };
        }
        m_palette_dirty = true;
    }
}

void LevelComposerPanel::render_sprite_region_editor(LevelPaletteEntry& entry) {
    Texture* texture = resolve_texture(entry);
    if (!texture || !texture->get_sdl_texture()) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f), "Texture is missing or unavailable.");
        return;
    }

    ImGui::Checkbox("Auto size from region", &m_auto_size_from_region);
    render_atlas_canvas(entry, *texture);
    float world_size[2]{entry.sprite_region.world_size.x, entry.sprite_region.world_size.y};
    if (ImGui::DragFloat2("World size", world_size, 1.0f, 1.0f, 8192.0f, "%.0f")) {
        entry.sprite_region.world_size = {
            std::max(1.0f, world_size[0]), std::max(1.0f, world_size[1])
        };
        m_palette_dirty = true;
    }
    float pivot[2]{entry.sprite_region.pivot.x, entry.sprite_region.pivot.y};
    if (ImGui::SliderFloat2("Pivot", pivot, 0.0f, 1.0f, "%.2f")) {
        entry.sprite_region.pivot = {pivot[0], pivot[1]};
        m_palette_dirty = true;
    }
    if (ImGui::Button("Center pivot")) {
        entry.sprite_region.pivot = {0.5f, 0.5f};
        m_palette_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Ground pivot")) {
        entry.sprite_region.pivot = {0.5f, 1.0f};
        m_palette_dirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Places the sorting anchor at the bottom center of the sprite.");
    }
    if (ImGui::Button("Size = selected pixels")) {
        entry.sprite_region.world_size = {
            static_cast<float>(entry.sprite_region.rect_px.z),
            static_cast<float>(entry.sprite_region.rect_px.w)
        };
        m_palette_dirty = true;
    }
}

void LevelComposerPanel::render_entry_editor(LevelPaletteEntry& entry) {
    EditorUi::section_heading("STAMP SETTINGS", entry_kind_label(entry.kind));
    bool text_changed = false;
    text_changed |= ImGui::InputText("Name", m_entry_label_buf.data(), m_entry_label_buf.size());
    text_changed |= ImGui::InputText("ID", m_entry_id_buf.data(), m_entry_id_buf.size());
    if (text_changed) apply_entry_buffers(entry);

    const auto category = std::find_if(
        m_palette.categories.begin(), m_palette.categories.end(),
        [&entry](const LevelPaletteCategory& candidate) {
            return candidate.id == entry.category;
        }
    );
    const char* category_preview = category == m_palette.categories.end()
        ? entry.category.c_str() : category->label.c_str();
    if (ImGui::BeginCombo("Category", category_preview)) {
        for (const LevelPaletteCategory& candidate : m_palette.categories) {
            const bool selected = candidate.id == entry.category;
            if (ImGui::Selectable(candidate.label.c_str(), selected)) {
                entry.category = candidate.id;
                std::snprintf(
                    m_entry_category_buf.data(), m_entry_category_buf.size(), "%s",
                    entry.category.c_str()
                );
                m_palette_dirty = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    float snap[2]{entry.placement.snap.x, entry.placement.snap.y};
    if (ImGui::DragFloat2("Snap X/Y", snap, 1.0f, 0.0f, 4096.0f, "%.0f")) {
        entry.placement.snap = {std::max(0.0f, snap[0]), std::max(0.0f, snap[1])};
        m_palette_dirty = true;
    }
    if (ImGui::DragFloat("Stroke spacing", &entry.placement.spacing, 1.0f, 1.0f, 8192.0f, "%.0f")) {
        entry.placement.spacing = std::max(1.0f, entry.placement.spacing);
        m_palette_dirty = true;
    }
    if (ImGui::DragFloat("Rotation", &entry.placement.rotation_degrees, 1.0f, -360.0f, 360.0f, "%.0f deg")) {
        m_palette_dirty = true;
    }

    if (entry.kind == LevelPaletteEntryKind::SpriteRegion) {
        const char* preset_names[]{
            "Custom", "Ground", "Decal", "Back wall", "Actor / Prop", "Front wall"
        };
        int preset = layer_preset_for(entry);
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("Layer preset", &preset, preset_names, 6) && preset > 0) {
            constexpr std::int16_t sorting_layers[]{-10, -5, 0, 1, 3};
            entry.placement.sorting_layer = sorting_layers[preset - 1];
            m_palette_dirty = true;
        }
        if (entry.placement.sorting_layer == 0 || entry.placement.sorting_layer == 3) {
            ImGui::TextDisabled(
                entry.placement.sorting_layer == 0
                    ? "Back wall: below actors on the Actor / Prop layer."
                    : "Front wall: covers actors walking behind it."
            );
        }

        int sorting = entry.placement.sorting_layer;
        if (ImGui::InputInt("Sorting layer", &sorting)) {
            entry.placement.sorting_layer = static_cast<std::int16_t>(
                std::clamp(sorting, -32768, 32767)
            );
            m_palette_dirty = true;
        }
    }

    if (ImGui::DragFloat(
            "Depth Z", &entry.placement.depth_z, 0.125f,
            -1000.0f, 1000.0f, "%.3f"
        )) {
        m_palette_dirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Depth is sorted before Y. For Y-sort, use the same Z as the actors."
        );
    }
    ImGui::TextDisabled("Y-sort requires the same Z as the actors.");

    if (entry.kind == LevelPaletteEntryKind::SpriteRegion) {
        int order = entry.placement.order_in_layer;
        if (ImGui::InputInt("Order in layer", &order)) {
            entry.placement.order_in_layer = static_cast<std::int16_t>(
                std::clamp(order, -32768, 32767)
            );
            m_palette_dirty = true;
        }
        int render_layer = entry.placement.render_layer;
        if (ImGui::InputInt("Render layer", &render_layer)) {
            entry.placement.render_layer = static_cast<RenderLayer>(
                std::clamp(render_layer, 0, 31)
            );
            m_palette_dirty = true;
        }
        if (ImGui::Checkbox("Flip X", &entry.placement.flip_x)) m_palette_dirty = true;
        ImGui::SameLine();
        if (ImGui::Checkbox("Flip Y", &entry.placement.flip_y)) m_palette_dirty = true;
        render_sprite_region_editor(entry);
    } else {
        const auto& reference = entry.prefab.prefab;
        ImGui::TextWrapped("Prefab: %s", reference.path.c_str());
        ImGui::TextWrapped(
            "Sorting layers, sprite pivots, and colliders are configured inside the prefab."
        );

        const auto& asset_database = AssetDatabase::get();
        const AssetRecord* prefab_record = reference.handle.is_valid()
            ? asset_database.find(reference.handle)
            : asset_database.find_by_path(reference.path);
        const bool prefab_ready = prefab_record &&
            prefab_record->type == AssetType::Prefab &&
            prefab_record->status == AssetStatus::Ready;
        std::string prefab_path;
        if (prefab_ready) {
            prefab_path = (
                asset_database.project_root() / prefab_record->source_path
            ).lexically_normal().string();
        }
        ImGui::BeginDisabled(!prefab_ready || !m_on_open_prefab);
        if (ImGui::Button("Open Prefab", ImVec2(-1.0f, 0.0f)) && m_on_open_prefab) {
            m_on_open_prefab(prefab_path);
        }
        ImGui::EndDisabled();
        if (!prefab_ready) {
            ImGui::TextDisabled("The prefab asset is not available for editing.");
        }
    }

    render_parent_target();
    ImGui::Separator();
    const bool can_stamp = is_entry_asset_ready(entry);
    if (!can_stamp) m_placement_active = false;
    ImGui::BeginDisabled(!can_stamp);
    if (m_placement_active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.82f, 0.30f, 0.22f, 1.0f));
        if (ImGui::Button("Stop stamping", ImVec2(-1.0f, 30.0f))) m_placement_active = false;
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.62f, 0.38f, 1.0f));
        if (ImGui::Button("Stamp in viewport", ImVec2(-1.0f, 30.0f))) m_placement_active = true;
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    if (!can_stamp && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Repair or replace the unavailable asset before stamping.");
    }
    ImGui::TextDisabled(
        entry.kind == LevelPaletteEntryKind::SpriteRegion
            ? "Drag to repeat  |  Q/E rotate  |  X/Y flip  |  Esc stop/cancel"
            : "Drag to repeat  |  Q/E rotate  |  Esc stop/cancel"
    );
}

void LevelComposerPanel::on_gui() {
    if (!is_open) return;
    ImGui::SetNextWindowSize(ImVec2(430.0f, 720.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::End();
        return;
    }

    render_palette_toolbar();
    ImGui::Spacing();
    render_entry_list();
    ImGui::Spacing();
    if (m_selected_entry && *m_selected_entry < m_palette.entries.size()) {
        render_entry_editor(m_palette.entries[*m_selected_entry]);
    } else {
        EditorUi::empty_state(
            EditorIcon::Sprite,
            "No stamp selected",
            "Drop an atlas texture or prefab, then select it here"
        );
    }
    render_pending_load_modal();
    ImGui::End();
}

} // namespace Engine
