#include "TilemapPalettePanel.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EntityCommands.hpp"
#include "../inspector/AssetDropTarget.hpp"
#include "../ui/EditorUi.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/AssetManager.hpp"
#include "../../core/Log.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Scene.hpp"
#include <imgui.h>
#include <random>
#include <algorithm>

namespace Engine {

TilemapPalettePanel::TilemapPalettePanel(
    SDL_Renderer* renderer,
    SceneManager* scene_manager,
    std::shared_ptr<EditorSelection> selection
) : m_renderer(renderer), m_scene_manager(scene_manager), m_selection(selection) {
}

void TilemapPalettePanel::on_context_changed(const EditorPanelContext& context) {
    m_selection = context.selection;
    m_command_history = context.command_history;
    m_bound_tilemap_entity = 0;
}

EntityID TilemapPalettePanel::get_selected_tilemap_entity() const {
    if (!m_scene_manager || !m_selection || m_selection->empty()) return 0;
    Scene* active_scene = m_scene_manager->get_active_scene();
    if (!active_scene) return 0;

    EntityManager& em = active_scene->get_entity_manager();
    EntityID selected_tilemap = 0;
    for (EntityID id : m_selection->get_selected()) {
        if (em.is_alive(id) && em.has_component<TilemapComponent>(id)) {
            if (selected_tilemap == 0 || id < selected_tilemap) selected_tilemap = id;
        }
    }
    return selected_tilemap;
}

bool TilemapPalettePanel::has_selected_tilemap() const {
    return get_selected_tilemap_entity() != 0;
}

void TilemapPalettePanel::sync_from_selected_tilemap(bool force) {
    const EntityID entity = get_selected_tilemap_entity();
    if (entity == 0 || !m_scene_manager) {
        m_bound_tilemap_entity = 0;
        return;
    }

    EntityManager& em = m_scene_manager->get_active_scene()->get_entity_manager();
    const auto& tilemap = em.get_component<TilemapComponent>(entity);
    if (!force && entity == m_bound_tilemap_entity &&
        (m_tileset_input_active || m_pixel_size_input_active)) {
        return;
    }
    if (!force && entity == m_bound_tilemap_entity &&
        m_tileset_path == tilemap.tileset_path &&
        m_tile_pixel_size == tilemap.tile_pixel_size) {
        return;
    }

    m_bound_tilemap_entity = entity;
    m_tileset_path = tilemap.tileset_path;
    m_tile_pixel_size = std::clamp(tilemap.tile_pixel_size, 1, 4096);
    std::snprintf(
        m_tileset_input_buf,
        sizeof(m_tileset_input_buf),
        "%s",
        m_tileset_path.c_str()
    );
}

bool TilemapPalettePanel::apply_selected_tileset_settings(
    const std::string& path,
    int tile_pixel_size
) {
    const EntityID entity = get_selected_tilemap_entity();
    if (entity == 0 || !m_scene_manager) return false;

    EntityManager& em = m_scene_manager->get_active_scene()->get_entity_manager();
    if (!em.is_alive(entity) || !em.has_component<TilemapComponent>(entity)) return false;

    const TilemapComponent before = em.get_component<TilemapComponent>(entity);
    TilemapComponent after = before;
    after.tileset_path = path;
    after.tile_pixel_size = std::clamp(tile_pixel_size, 1, 4096);
    if (after.tileset_path == before.tileset_path &&
        after.tile_pixel_size == before.tile_pixel_size) {
        sync_from_selected_tilemap(true);
        return false;
    }

    const auto reload_tileset = [renderer = m_renderer](EntityManager& command_em, EntityID id) {
        if (!command_em.has_component<TilemapComponent>(id)) return;
        auto& command_tilemap = command_em.get_component<TilemapComponent>(id);
        command_tilemap.tileset_texture = nullptr;
        if (renderer && !command_tilemap.tileset_path.empty()) {
            command_tilemap.tileset_texture = AssetManager::get().get_texture(
                renderer,
                command_tilemap.tileset_path
            );
        }
    };

    bool applied = false;
    if (m_command_history) {
        applied = m_command_history->execute_command(
            std::make_unique<SetComponentCommand<TilemapComponent>>(
                &em,
                entity,
                before,
                after,
                "Change Tilemap Tileset",
                reload_tileset
            )
        );
    } else {
        em.get_component<TilemapComponent>(entity) = after;
        reload_tileset(em, entity);
        applied = true;
    }

    if (applied) sync_from_selected_tilemap(true);
    return applied;
}

void TilemapPalettePanel::add_to_random_pool(uint16_t tile_id, float weight) {
    for (auto& item : m_random_pool) {
        if (item.tile_id == tile_id) {
            item.weight = weight;
            return;
        }
    }
    m_random_pool.push_back({tile_id, weight});
}

void TilemapPalettePanel::remove_from_random_pool(uint16_t tile_id) {
    m_random_pool.erase(
        std::remove_if(m_random_pool.begin(), m_random_pool.end(), [tile_id](const TileVariant& v) {
            return v.tile_id == tile_id;
        }),
        m_random_pool.end()
    );
}

bool TilemapPalettePanel::is_in_random_pool(uint16_t tile_id) const {
    for (const auto& item : m_random_pool) {
        if (item.tile_id == tile_id) return true;
    }
    return false;
}

uint16_t TilemapPalettePanel::get_random_tile_id() const {
    if (m_random_pool.empty()) {
        return m_active_tile_id;
    }

    float total_weight = 0.0f;
    for (const auto& item : m_random_pool) {
        if (item.weight > 0.0f) total_weight += item.weight;
    }

    if (total_weight <= 0.0f) {
        return m_random_pool.front().tile_id;
    }

    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0f, total_weight);
    float pick = dist(rng);

    float accumulated = 0.0f;
    for (const auto& item : m_random_pool) {
        if (item.weight <= 0.0f) continue;
        accumulated += item.weight;
        if (pick <= accumulated) {
            return item.tile_id;
        }
    }

    return m_random_pool.back().tile_id;
}

void TilemapPalettePanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(380, 560), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        if (!has_selected_tilemap()) {
            EditorUi::empty_state(
                EditorIcon::Grid,
                "No tilemap selected",
                "Select an entity with a Tilemap component"
            );
            ImGui::End();
            return;
        }

        sync_from_selected_tilemap();

        // Mode Toggle Button (Pill design)
        if (m_is_painting_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.60f, 0.35f, 0.90f));
            if (ImGui::Button("PAINT MODE ACTIVE - CLICK TO PAUSE", ImVec2(ImGui::GetContentRegionAvail().x, 26))) {
                m_is_painting_active = false;
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.40f, 0.90f));
            if (ImGui::Button("SELECTION MODE - CLICK TO PAINT", ImVec2(ImGui::GetContentRegionAvail().x, 26))) {
                m_is_painting_active = true;
            }
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // --- 1. Painting Tools Bar ---
        ImGui::TextDisabled("TOOL SELECTION:");

        float btn_w = (ImGui::GetContentRegionAvail().x - 18.0f) / 4.0f;

        bool is_brush = (m_tool_mode == TileToolMode::Brush);
        if (is_brush) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.40f, 0.95f));
        if (ImGui::Button("🖌️ Brush", ImVec2(btn_w, 28))) m_tool_mode = TileToolMode::Brush;
        if (is_brush) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Single Tile Brush: Paint exact active tile");

        ImGui::SameLine();
        bool is_random = (m_tool_mode == TileToolMode::RandomBrush);
        if (is_random) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.50f, 0.15f, 0.95f));
        if (ImGui::Button("🎲 Random", ImVec2(btn_w, 28))) m_tool_mode = TileToolMode::RandomBrush;
        if (is_random) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Random Variation Brush: Mixes tiles organically from the pool using %% weights");

        ImGui::SameLine();
        bool is_box = (m_tool_mode == TileToolMode::BoxFill);
        if (is_box) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.50f, 0.85f, 0.95f));
        if (ImGui::Button("🔲 Fill", ImVec2(btn_w, 28))) m_tool_mode = TileToolMode::BoxFill;
        if (is_box) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Box Fill: Drag & drop rectangular areas (supports random fill)");

        ImGui::SameLine();
        bool is_eraser = (m_tool_mode == TileToolMode::Eraser);
        if (is_eraser) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.95f));
        if (ImGui::Button("🧹 Erase", ImVec2(btn_w, 28))) m_tool_mode = TileToolMode::Eraser;
        if (is_eraser) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Eraser: Remove tiles");

        ImGui::Spacing();
        ImGui::Separator();

        // --- 2. Random Variation Pool Manager (When in Random mode or with active pool) ---
        if (m_tool_mode == TileToolMode::RandomBrush || !m_random_pool.empty()) {
            if (ImGui::CollapsingHeader("🎲 Random Variation Pool", ImGuiTreeNodeFlags_DefaultOpen)) {
                float total_w = 0.0f;
                for (const auto& item : m_random_pool) total_w += item.weight;

                ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f), "Active Variants: %zu tiles (Total Weight: %.0f)", m_random_pool.size(), total_w);
                
                // Render weight sliders for each variant in the pool
                for (size_t i = 0; i < m_random_pool.size(); ++i) {
                    auto& item = m_random_pool[i];
                    ImGui::PushID(static_cast<int>(item.tile_id + 1000));

                    float pct = (total_w > 0.0f) ? (item.weight / total_w * 100.0f) : 0.0f;
                    char label_buf[64];
                    snprintf(label_buf, sizeof(label_buf), "Tile #%d (%.1f%%)", item.tile_id, pct);

                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 65.0f);
                    ImGui::SliderFloat(label_buf, &item.weight, 1.0f, 100.0f, "%.0f");

                    ImGui::SameLine();
                    if (ImGui::Button("✖", ImVec2(24, 20))) {
                        remove_from_random_pool(item.tile_id);
                        ImGui::PopID();
                        break;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove tile from random pool");

                    ImGui::PopID();
                }

                ImGui::Spacing();
                if (ImGui::Button("⚖️ Balance Equal", ImVec2(130, 22))) {
                    for (auto& item : m_random_pool) item.weight = 100.0f;
                }
                ImGui::SameLine();
                if (ImGui::Button("🗑️ Clear Pool", ImVec2(100, 22))) {
                    m_random_pool.clear();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(Shift+Click tile to toggle)");

                ImGui::Separator();
            }
        }

        // --- 3. Tileset Texture & Zoom Controls ---
        ImGui::TextDisabled("TILESET & ZOOM:");
        
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
        const bool path_submitted = ImGui::InputText(
            "##tileset_path",
            m_tileset_input_buf,
            sizeof(m_tileset_input_buf),
            ImGuiInputTextFlags_EnterReturnsTrue
        );
        m_tileset_input_active = ImGui::IsItemActive();
        if (path_submitted) {
            apply_selected_tileset_settings(m_tileset_input_buf, m_tile_pixel_size);
        }

        // Drag and Drop target from AssetBrowserPanel
        if (const auto dropped = accept_asset_drop_target(
                AssetDatabase::get(), AssetType::Texture
            )) {
            if (const AssetRecord* record = AssetDatabase::get().find(*dropped)) {
                apply_selected_tileset_settings(record->source_path, m_tile_pixel_size);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Apply", ImVec2(72, 20))) {
            apply_selected_tileset_settings(m_tileset_input_buf, m_tile_pixel_size);
        }

        // Grid Zoom and Tile Pixel size controls
        ImGui::SetNextItemWidth(132.0f);
        ImGui::SliderFloat("Preview Zoom", &m_palette_cell_size, 32.0f, 72.0f, "%.0f px");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(92.0f);
        if (ImGui::InputInt("Cell px", &m_tile_pixel_size, 1, 16)) {
            m_tile_pixel_size = std::clamp(m_tile_pixel_size, 1, 4096);
        }
        m_pixel_size_input_active = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            apply_selected_tileset_settings(m_tileset_input_buf, m_tile_pixel_size);
        }

        if (m_tool_mode == TileToolMode::BoxFill) {
            ImGui::Checkbox("Use random pool for box fill", &m_random_box_fill);
            if (m_random_box_fill && m_random_pool.empty()) {
                ImGui::TextDisabled("Add variants with Shift+Click before using random fill.");
            }
        }

        ImGui::Separator();

        // --- 4. Interactive Palette Grid ---
        sync_from_selected_tilemap();
        Texture* tileset_tex = AssetManager::get().get_texture(m_renderer, m_tileset_path);
        if (tileset_tex) {
            int tex_w = tileset_tex->get_width();
            int tex_h = tileset_tex->get_height();
            int cols = std::max(1, tex_w / m_tile_pixel_size);
            int rows = std::max(1, tex_h / m_tile_pixel_size);

            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "ACTIVE TILE: #%d  |  Grid: %dx%d (%d tiles)", m_active_tile_id, cols, rows, cols * rows);

            ImGui::BeginChild("PaletteGridScroll", ImVec2(0, 0), true);

            float cell_size = m_palette_cell_size;
            int num_cols = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (cell_size + 8.0f)));

            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    uint16_t tile_id = static_cast<uint16_t>(r * cols + c + 1);

                    ImVec2 uv0(static_cast<float>(c * m_tile_pixel_size) / tex_w, static_cast<float>(r * m_tile_pixel_size) / tex_h);
                    ImVec2 uv1(static_cast<float>((c + 1) * m_tile_pixel_size) / tex_w, static_cast<float>((r + 1) * m_tile_pixel_size) / tex_h);

                    ImGui::PushID(tile_id);
                    
                    bool is_active_single = (m_active_tile_id == tile_id && m_tool_mode != TileToolMode::Eraser);
                    bool in_pool = is_in_random_pool(tile_id);

                    if (is_active_single) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.45f, 0.95f, 0.90f));
                    } else if (in_pool) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.55f, 0.15f, 0.65f));
                    }

                    if (ImGui::ImageButton("##tile_btn", reinterpret_cast<ImTextureID>(tileset_tex->get_sdl_texture()), ImVec2(cell_size, cell_size), uv0, uv1)) {
                        ImGuiIO& io = ImGui::GetIO();
                        if (io.KeyShift) {
                            // Shift+Click: Toggle tile in/out of Random Pool
                            if (in_pool) {
                                remove_from_random_pool(tile_id);
                            } else {
                                add_to_random_pool(tile_id, 50.0f);
                            }
                        } else {
                            m_active_tile_id = tile_id;
                            if (m_tool_mode == TileToolMode::Eraser) {
                                m_tool_mode = TileToolMode::Brush;
                            }
                        }
                    }

                    // Badge / Overlay indicator for Random Pool members
                    if (in_pool) {
                        ImVec2 min_p = ImGui::GetItemRectMin();
                        ImVec2 max_p = ImGui::GetItemRectMax();
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        dl->AddRect(min_p, max_p, IM_COL32(245, 158, 11, 255), 3.0f, 0, 2.0f);
                        dl->AddText(ImVec2(min_p.x + 3, min_p.y + 2), IM_COL32(255, 255, 255, 230), "🎲");
                    }

                    // Context Menu for each tile
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem("🎯 Select as Active Tile")) {
                            m_active_tile_id = tile_id;
                        }
                        if (!in_pool && ImGui::MenuItem("🎲 Add to Random Pool")) {
                            add_to_random_pool(tile_id, 50.0f);
                        }
                        if (in_pool && ImGui::MenuItem("✖ Remove from Random Pool")) {
                            remove_from_random_pool(tile_id);
                        }
                        ImGui::EndPopup();
                    }

                    if (is_active_single || in_pool) {
                        ImGui::PopStyleColor();
                    }

                    if ((c + 1) % num_cols != 0 && c + 1 < cols) {
                        ImGui::SameLine();
                    }

                    ImGui::PopID();
                }
            }

            ImGui::EndChild();
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Could not load tileset texture.");
        }
    }
    ImGui::End();
}

} // namespace Engine
