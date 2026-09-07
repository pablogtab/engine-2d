#include "TextureComposerPanel.hpp"

#include "../EditorSelection.hpp"
#include "../inspector/AssetDropResolver.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../ecs/TransformSystem.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../renderer/SpriteSortKey.hpp"
#include "../../renderer/Texture.hpp"
#include "../../scene/Scene.hpp"
#include "../../scene/SceneManager.hpp"
#include "stb/stb_image.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace Engine {
namespace {

bool is_path_inside(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

glm::vec2 rotate_point(glm::vec2 point, float degrees) {
    const float radians = glm::radians(degrees);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return {point.x * cosine - point.y * sine, point.x * sine + point.y * cosine};
}

std::string entity_name(EntityManager& entities, EntityID entity) {
    return entities.has_component<TagComponent>(entity)
        ? entities.get_component<TagComponent>(entity).tag
        : "Entity " + std::to_string(entity);
}

} // namespace

TextureComposerPanel::TextureComposerPanel(
    SDL_Renderer* renderer,
    SceneManager* scene_manager,
    std::shared_ptr<EditorSelection> selection
) : m_renderer(renderer), m_scene_manager(scene_manager), m_selection(std::move(selection)) {
    const std::string default_output = "Assets/Composites/Composite.png";
    std::memcpy(m_output_path.data(), default_output.c_str(), default_output.size() + 1);
    is_open = false;
}

void TextureComposerPanel::on_context_changed(const EditorPanelContext& context) {
    m_selection = context.selection;
}

void TextureComposerPanel::on_render_resources_invalidated() {
    m_preview.reset();
}

bool TextureComposerPanel::import_viewport_selection() {
    m_error.clear();
    m_status.clear();
    if (!m_scene_manager || !m_scene_manager->is_editing() || !m_selection || m_selection->count() < 2) {
        m_error = "Select two or more sprite entities in Edit mode";
        return false;
    }
    Scene* scene = m_scene_manager->get_active_scene();
    if (!scene) { m_error = "There is no active scene"; return false; }
    EntityManager& entities = scene->get_entity_manager();
    TransformSystem::update(entities);

    struct Captured {
        TextureCompositionLayer layer;
        glm::vec2 world_anchor{};
    };
    std::vector<Captured> captured;
    m_result.warnings.clear();
    glm::vec2 minimum(std::numeric_limits<float>::max());
    glm::vec2 maximum(std::numeric_limits<float>::lowest());
    std::size_t skipped = 0;
    bool unsupported_effects = false;

    for (EntityID entity : m_selection->get_selected()) {
        if (!entities.is_alive(entity) || !entities.has_component<TransformComponent>(entity) ||
            !entities.has_component<SpriteComponent>(entity)) {
            ++skipped;
            continue;
        }
        const auto& transform = entities.get_component<TransformComponent>(entity);
        const auto& sprite = entities.get_component<SpriteComponent>(entity);
        if (!transform.world_visible || !sprite.visible || !sprite.texture_handle.is_valid()) {
            ++skipped;
            continue;
        }
        const std::string source = AssetDatabase::get().resolve(sprite.texture_handle);
        if (source.empty()) { ++skipped; continue; }

        Captured item;
        auto& layer = item.layer;
        layer.name = entity_name(entities, entity);
        layer.source_path = source;
        layer.source_rect = sprite.src_rect;
        layer.use_source_rect = sprite.use_src_rect;
        layer.width = std::abs(sprite.size.x * transform.world_scale.x * m_pixels_per_world_unit);
        layer.height = std::abs(sprite.size.y * transform.world_scale.y * m_pixels_per_world_unit);
        layer.pivot_x = transform.world_pivot.x;
        layer.pivot_y = transform.world_pivot.y;
        layer.rotation_degrees = transform.world_rotation;
        layer.flip_x = sprite.flip_x != (transform.world_scale.x < 0.0f);
        layer.flip_y = sprite.flip_y != (transform.world_scale.y < 0.0f);
        layer.tint = sprite.color;
        layer.sort_key = SpriteSortKey::world(
            sprite.sorting_layer, transform.world_position.z,
            transform.world_position.y, sprite.order_in_layer
        );
        item.world_anchor = glm::vec2(transform.world_position) * m_pixels_per_world_unit;

        const glm::vec2 corners[4]{
            {-layer.pivot_x * layer.width, -layer.pivot_y * layer.height},
            {(1.0f - layer.pivot_x) * layer.width, -layer.pivot_y * layer.height},
            {(1.0f - layer.pivot_x) * layer.width, (1.0f - layer.pivot_y) * layer.height},
            {-layer.pivot_x * layer.width, (1.0f - layer.pivot_y) * layer.height}
        };
        for (const glm::vec2 corner : corners) {
            const glm::vec2 world = item.world_anchor + rotate_point(corner, layer.rotation_degrees);
            minimum = glm::min(minimum, world);
            maximum = glm::max(maximum, world);
        }
        unsupported_effects = unsupported_effects || sprite.material || sprite.material_handle.is_valid() ||
            transform.world_skew != glm::vec2(0.0f);
        captured.push_back(std::move(item));
    }

    if (captured.empty()) {
        m_error = "The selection contains no visible sprites with imported textures";
        return false;
    }
    std::stable_sort(captured.begin(), captured.end(), [](const Captured& lhs, const Captured& rhs) {
        return lhs.layer.sort_key < rhs.layer.sort_key;
    });
    m_settings.layers.clear();
    m_settings.width = std::max(1, static_cast<int>(std::ceil(maximum.x - minimum.x)) + m_padding * 2);
    m_settings.height = std::max(1, static_cast<int>(std::ceil(maximum.y - minimum.y)) + m_padding * 2);
    for (auto& item : captured) {
        item.layer.anchor_x = item.world_anchor.x - minimum.x + m_padding;
        item.layer.anchor_y = item.world_anchor.y - minimum.y + m_padding;
        item.layer.sort_key = m_settings.layers.size();
        m_settings.layers.push_back(std::move(item.layer));
    }
    if (unsupported_effects) {
        m_result.warnings = {"Materials, lighting and skew are not baked; preview uses sprite texture and tint"};
    }
    rebuild_preview();
    if (skipped > 0) {
        m_status += " Skipped " + std::to_string(skipped) + " non-sprite or unresolved entities.";
    }
    is_open = true;
    return m_result.success;
}

void TextureComposerPanel::rebuild_preview() {
    const auto retained_warnings = m_result.warnings;
    m_result = TextureCompositionService::compose(m_settings);
    m_result.warnings.insert(m_result.warnings.end(), retained_warnings.begin(), retained_warnings.end());
    m_preview.reset();
    if (!m_result.success) { m_error = m_result.error; return; }
    m_error.clear();
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(m_result.width) * m_result.height);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::size_t offset = index * 4;
        pixels[index] = (static_cast<std::uint32_t>(m_result.rgba[offset]) << 24) |
            (static_cast<std::uint32_t>(m_result.rgba[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(m_result.rgba[offset + 2]) << 8) |
            m_result.rgba[offset + 3];
    }
    if (m_renderer) {
        m_preview = std::make_unique<Texture>(m_renderer, m_result.width, m_result.height, pixels.data());
        if (m_preview->get_sdl_texture()) {
            SDL_SetTextureScaleMode(m_preview->get_sdl_texture(), SDL_SCALEMODE_NEAREST);
        }
    }
    m_status = "Preview rebuilt from " + std::to_string(m_settings.layers.size()) + " layers.";
}

std::filesystem::path TextureComposerPanel::resolved_output_path(std::string& error) const {
    error.clear();
    const auto root = AssetDatabase::get().project_root();
    const std::filesystem::path entered(m_output_path.data());
    const auto lexical = entered.is_absolute() ? entered.lexically_normal() : (root / entered).lexically_normal();
    std::error_code path_error;
    const auto resolved = std::filesystem::weakly_canonical(lexical, path_error);
    const auto assets = std::filesystem::weakly_canonical(root / "Assets", path_error);
    if (path_error || !is_path_inside(resolved, assets)) {
        error = "Output must be located below the project's Assets directory";
        return {};
    }
    if (resolved.extension() != ".png") { error = "Output must use the .png extension"; return {}; }
    for (const auto& layer : m_settings.layers) {
        if (std::filesystem::weakly_canonical(layer.source_path, path_error) == resolved && !path_error) {
            error = "Output cannot replace one of its source textures";
            return {};
        }
        path_error.clear();
    }
    return resolved;
}

std::filesystem::path TextureComposerPanel::recipe_path_for(const std::filesystem::path& output) const {
    auto recipe = output;
    recipe.replace_extension(".texturecompose.json");
    return recipe;
}

void TextureComposerPanel::save_recipe(const std::filesystem::path& output) {
    nlohmann::json layers = nlohmann::json::array();
    const auto& database = AssetDatabase::get();
    for (const auto& layer : m_settings.layers) {
        const AssetRecord* record = database.find_by_path(layer.source_path);
        layers.push_back({
            {"name", layer.name}, {"source", record ? record->source_path : layer.source_path.generic_string()},
            {"texture_handle", record ? record->handle.to_string() : std::string{}},
            {"source_rect", {layer.source_rect.x, layer.source_rect.y, layer.source_rect.w, layer.source_rect.h}},
            {"use_source_rect", layer.use_source_rect}, {"anchor", {layer.anchor_x, layer.anchor_y}},
            {"size", {layer.width, layer.height}}, {"pivot", {layer.pivot_x, layer.pivot_y}},
            {"rotation", layer.rotation_degrees}, {"flip_x", layer.flip_x}, {"flip_y", layer.flip_y},
            {"tint", {layer.tint.r, layer.tint.g, layer.tint.b, layer.tint.a}},
            {"sort_key", layer.sort_key}, {"visible", layer.visible}
        });
    }
    const nlohmann::json recipe{{"version", 1}, {"canvas", {m_settings.width, m_settings.height}}, {"layers", layers}};
    std::ofstream stream(recipe_path_for(output));
    if (!stream || !(stream << recipe.dump(2))) {
        m_error = "PNG saved, but the editable composition recipe could not be saved";
        return;
    }
    stream.close();
    if (!AssetDatabase::get().import_asset(recipe_path_for(output))) {
        m_error = "PNG saved, but the editable composition recipe could not be imported";
    }
}

void TextureComposerPanel::export_png() {
    rebuild_preview();
    if (!m_result.success) return;
    const auto output = resolved_output_path(m_error);
    if (output.empty()) return;
    if (!TextureCompositionService::save_png_atomic(output, m_result, m_error)) return;
    save_recipe(output);
    if (!m_error.empty()) return;
    const auto imported = AssetDatabase::get().import_asset(output);
    if (!imported) { m_error = "PNG was saved but AssetDatabase could not import it"; return; }
    m_status = "Saved and imported " + std::filesystem::relative(
        output, AssetDatabase::get().project_root()).generic_string();
}

void TextureComposerPanel::load_recipe() {
    const auto output = resolved_output_path(m_error);
    if (output.empty()) return;
    std::ifstream stream(recipe_path_for(output));
    nlohmann::json recipe;
    if (!stream || !(stream >> recipe)) { m_error = "Could not read the composition recipe"; return; }
    try {
        TextureCompositionSettings loaded;
        loaded.width = recipe.at("canvas").at(0).get<int>();
        loaded.height = recipe.at("canvas").at(1).get<int>();
        for (const auto& value : recipe.at("layers")) {
            TextureCompositionLayer layer;
            layer.name = value.value("name", std::string{"Layer"});
            const auto handle = AssetHandle::from_string(value.value("texture_handle", std::string{}));
            const AssetRecord* record = handle ? AssetDatabase::get().find(*handle) : nullptr;
            layer.source_path = record
                ? std::filesystem::path(AssetDatabase::get().resolve(record->handle))
                : (AssetDatabase::get().project_root() / value.at("source").get<std::string>());
            const auto rect = value.at("source_rect");
            layer.source_rect = {rect.at(0), rect.at(1), rect.at(2), rect.at(3)};
            layer.use_source_rect = value.value("use_source_rect", false);
            const auto anchor = value.at("anchor"); layer.anchor_x = anchor.at(0); layer.anchor_y = anchor.at(1);
            const auto size = value.at("size"); layer.width = size.at(0); layer.height = size.at(1);
            const auto pivot = value.at("pivot"); layer.pivot_x = pivot.at(0); layer.pivot_y = pivot.at(1);
            layer.rotation_degrees = value.value("rotation", 0.0f);
            layer.flip_x = value.value("flip_x", false); layer.flip_y = value.value("flip_y", false);
            const auto tint = value.at("tint");
            layer.tint = {tint.at(0), tint.at(1), tint.at(2), tint.at(3)};
            layer.sort_key = value.value("sort_key", std::uint64_t{0});
            layer.visible = value.value("visible", true);
            loaded.layers.push_back(std::move(layer));
        }
        m_settings = std::move(loaded);
        m_result.warnings.clear();
        rebuild_preview();
        m_status = "Loaded editable composition recipe.";
    } catch (const std::exception&) {
        m_error = "Composition recipe is invalid";
    }
}

void TextureComposerPanel::add_dropped_texture(const std::string& path) {
    const std::array accepted{AssetType::Texture};
    const auto drop = resolve_asset_drop(AssetDatabase::get(), path, accepted);
    if (!drop.accepted()) { m_error = asset_drop_status_message(drop.status); return; }
    TextureCompositionLayer layer;
    layer.name = std::filesystem::path(drop.record->source_path).stem().string();
    layer.source_path = AssetDatabase::get().resolve(drop.record->handle);
    layer.anchor_x = m_settings.width * 0.5f;
    layer.anchor_y = m_settings.height * 0.5f;
    int width = 0;
    int height = 0;
    int channels = 0;
    if (stbi_info(layer.source_path.string().c_str(), &width, &height, &channels)) {
        layer.width = static_cast<float>(width);
        layer.height = static_cast<float>(height);
    }
    layer.sort_key = m_settings.layers.size();
    m_settings.layers.push_back(std::move(layer));
    rebuild_preview();
}

void TextureComposerPanel::on_gui() {
    if (!is_open) return;
    ImGui::SetNextWindowSize(ImVec2(980.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(m_name.c_str(), &is_open)) { ImGui::End(); return; }
    ImGui::BeginChild("TextureComposerControls", ImVec2(420.0f, 0.0f), true);
    ImGui::TextUnformatted("COMPOSITION");
    ImGui::TextDisabled("Select sprites in the viewport, then import their visible geometry.");
    if (ImGui::Button("Import Viewport Selection", ImVec2(-1.0f, 0.0f))) import_viewport_selection();
    ImGui::SetNextItemWidth(120.0f); ImGui::InputFloat("Pixels / world unit", &m_pixels_per_world_unit, 0.25f, 1.0f, "%.2f");
    m_pixels_per_world_unit = std::clamp(m_pixels_per_world_unit, 0.01f, 32.0f);
    ImGui::SetNextItemWidth(120.0f); ImGui::InputInt("Capture padding", &m_padding); m_padding = std::clamp(m_padding, 0, 512);
    ImGui::InputInt("Canvas width", &m_settings.width); m_settings.width = std::clamp(m_settings.width, 1, 8192);
    ImGui::InputInt("Canvas height", &m_settings.height); m_settings.height = std::clamp(m_settings.height, 1, 8192);

    ImGui::SeparatorText("Layers");
    ImGui::TextDisabled("Drop texture assets here to add manual layers.");
    ImGui::Button("Drop textures here", ImVec2(-1.0f, 34.0f));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetPayload)) {
            const char* begin = static_cast<const char*>(payload->Data);
            const char* end = std::find(begin, begin + payload->DataSize, '\0');
            add_dropped_texture(std::string(begin, end));
        }
        ImGui::EndDragDropTarget();
    }
    int remove = -1;
    bool reordered = false;
    for (int index = 0; index < static_cast<int>(m_settings.layers.size()); ++index) {
        auto& layer = m_settings.layers[index];
        ImGui::PushID(index);
        if (ImGui::TreeNodeEx(layer.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Visible", &layer.visible); ImGui::SameLine();
            if (ImGui::SmallButton("Up") && index + 1 < static_cast<int>(m_settings.layers.size())) {
                std::swap(m_settings.layers[index], m_settings.layers[index + 1]);
                reordered = true;
            } ImGui::SameLine();
            if (ImGui::SmallButton("Down") && index > 0) {
                std::swap(m_settings.layers[index], m_settings.layers[index - 1]);
                reordered = true;
            }
            ImGui::SameLine(); if (ImGui::SmallButton("Remove")) remove = index;
            float anchor[2]{layer.anchor_x, layer.anchor_y};
            if (ImGui::DragFloat2("Position", anchor, 1.0f)) { layer.anchor_x = anchor[0]; layer.anchor_y = anchor[1]; }
            float size[2]{layer.width, layer.height};
            if (ImGui::DragFloat2("Size", size, 1.0f, 1.0f, 8192.0f)) { layer.width = size[0]; layer.height = size[1]; }
            ImGui::DragFloat("Rotation", &layer.rotation_degrees, 1.0f);
            ImGui::Checkbox("Flip X", &layer.flip_x); ImGui::SameLine(); ImGui::Checkbox("Flip Y", &layer.flip_y);
            int opacity = layer.tint.a;
            if (ImGui::SliderInt("Opacity", &opacity, 0, 255)) layer.tint.a = static_cast<std::uint8_t>(opacity);
            if (layer.use_source_rect) {
                float rect[4]{layer.source_rect.x, layer.source_rect.y, layer.source_rect.w, layer.source_rect.h};
                if (ImGui::DragFloat4("Source rect", rect, 1.0f)) layer.source_rect = {rect[0], rect[1], rect[2], rect[3]};
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove >= 0) m_settings.layers.erase(m_settings.layers.begin() + remove);
    if (reordered || remove >= 0) {
        for (std::size_t index = 0; index < m_settings.layers.size(); ++index) {
            m_settings.layers[index].sort_key = index;
        }
    }
    if (ImGui::Button("Rebuild Preview", ImVec2(-1.0f, 0.0f))) rebuild_preview();
    ImGui::SeparatorText("Export");
    ImGui::InputText("##output", m_output_path.data(), m_output_path.size());
    if (ImGui::Button("Export PNG + Recipe", ImVec2(-1.0f, 0.0f))) export_png();
    if (ImGui::Button("Load Recipe", ImVec2(-1.0f, 0.0f))) load_recipe();
    ImGui::TextWrapped("Back and Front occlusion pieces must be exported as separate composites because one PNG has one sorting layer.");
    if (!m_status.empty()) ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s", m_status.c_str());
    if (!m_error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "%s", m_error.c_str());
    for (const auto& warning : m_result.warnings) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "%s", warning.c_str());
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("TextureComposerPreview", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::SliderFloat("Zoom", &m_preview_zoom, 0.1f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
    if (m_preview && m_preview->get_sdl_texture()) {
        ImGui::Image(
            reinterpret_cast<ImTextureID>(m_preview->get_sdl_texture()),
            ImVec2(m_result.width * m_preview_zoom, m_result.height * m_preview_zoom)
        );
    } else {
        ImGui::TextDisabled("Import viewport sprites or drop textures to build a preview.");
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace Engine
