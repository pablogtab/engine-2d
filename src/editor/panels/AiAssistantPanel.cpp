#include "AiAssistantPanel.hpp"
#include "EditorAiAttachmentView.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../ai/EditorAiActivityFormatter.hpp"
#include "../ai/EditorAiRequestPolicy.hpp"
#include "../ai/ProjectCommandRegistry.hpp"

#include "../../scene/ComponentRegistry.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/SceneValidator.hpp"
#include "../../core/BinarySerializer.hpp"
#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"
#include "../../renderer/Texture.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "ViewportPanel.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include "../tools/EditorIcons.hpp"

#include <imgui.h>
#include <SDL3/SDL_clipboard.h>

#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <numeric>
#include <unordered_map>

namespace Engine {
namespace {

std::atomic_uint64_t NEXT_AI_REQUEST_ID{1};
// App Server binds dynamic tools on thread/start. Bump this whenever the editor
// tool schemas or their identity semantics change so saved threads are replaced.
constexpr std::uint32_t EDITOR_AI_TOOL_CONTRACT_VERSION = 18;
// Input usage includes the editor contract, schemas, and cached thread context. Compacting at
// 24k caused a fresh authoring thread to compact again on nearly every turn. Keep ample headroom
// while avoiding repeated 10-20 second compaction passes during normal editor work.
constexpr std::size_t AUTO_COMPACT_INPUT_TOKEN_THRESHOLD = 96000;
std::string project_root();

nlohmann::json begin_plan_implementation_schema() {
    return {
        {"type", "function"},
        {"name", "begin_plan_implementation"},
        {"description",
            "Signal that the user explicitly authorized implementation of the current plan. "
            "This does not mutate the project; the editor switches to Build after the Plan turn."},
        {"parameters", {
            {"type", "object"},
            {"properties", {{"reason", {
                {"type", "string"},
                {"description", "Brief evidence from the current user message authorizing build."}
            }}}},
            {"required", nlohmann::json::array({"reason"})},
            {"additionalProperties", false}
        }},
        {"strict", true}
    };
}

const char* diagnostic_code_name(SceneDiagnosticCode code) {
    switch (code) {
        case SceneDiagnosticCode::MissingPrimaryCamera: return "missing_primary_camera";
        case SceneDiagnosticCode::DuplicatePrimaryCamera: return "duplicate_primary_camera";
        case SceneDiagnosticCode::MissingHierarchyParent: return "missing_hierarchy_parent";
        case SceneDiagnosticCode::MissingHierarchyChild: return "missing_hierarchy_child";
        case SceneDiagnosticCode::HierarchyLinkMismatch: return "hierarchy_link_mismatch";
        case SceneDiagnosticCode::HierarchyCycle: return "hierarchy_cycle";
        case SceneDiagnosticCode::MissingTextureAsset: return "missing_texture_asset";
        case SceneDiagnosticCode::MissingMaterialAsset: return "missing_material_asset";
        case SceneDiagnosticCode::MissingRenderEffectAsset: return "missing_render_effect_asset";
        case SceneDiagnosticCode::MissingScriptAsset: return "missing_script_asset";
        case SceneDiagnosticCode::MissingPrefabAsset: return "missing_prefab_asset";
        case SceneDiagnosticCode::MissingTilemapTileset: return "missing_tilemap_tileset";
        case SceneDiagnosticCode::InvalidColliderShape: return "invalid_collider_shape";
        case SceneDiagnosticCode::InvalidTilemapDimensions: return "invalid_tilemap_dimensions";
        case SceneDiagnosticCode::InvalidLight2D: return "invalid_light_2d";
        case SceneDiagnosticCode::InvalidShadowCaster2D: return "invalid_shadow_caster_2d";
        case SceneDiagnosticCode::MissingShadowCasterCollider: return "missing_shadow_caster_collider";
        case SceneDiagnosticCode::MissingUIImageTexture: return "missing_ui_image_texture";
        case SceneDiagnosticCode::MissingUIFont: return "missing_ui_font";
        case SceneDiagnosticCode::InvalidCanvas: return "invalid_canvas";
        case SceneDiagnosticCode::InvalidUILayout: return "invalid_ui_layout";
        case SceneDiagnosticCode::InvalidUIText: return "invalid_ui_text";
        case SceneDiagnosticCode::MissingUILayout: return "missing_ui_layout";
        case SceneDiagnosticCode::MissingCanvasAncestor: return "missing_canvas_ancestor";
    }
    return "unknown";
}

nlohmann::json viewport_state_output(const ViewportAiState& state) {
    return {
        {"center", {state.center.x, state.center.y}},
        {"zoom", state.zoom},
        {"visible_world", {
            {"min", {state.world_min.x, state.world_min.y}},
            {"max", {state.world_max.x, state.world_max.y}}
        }},
        {"render_size", {state.render_width, state.render_height}},
        {"render_generation", state.render_generation}
    };
}

bool prompt_requests_generated_image(const std::string& prompt) {
    std::string lower;
    lower.reserve(prompt.size());
    std::transform(prompt.begin(), prompt.end(), std::back_inserter(lower), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    const bool generation_verb = lower.find("gener") != std::string::npos ||
        lower.find("crea") != std::string::npos || lower.find("make ") != std::string::npos ||
        lower.find("draw ") != std::string::npos || lower.find("design ") != std::string::npos;
    const bool image_noun = lower.find("textura") != std::string::npos ||
        lower.find("texture") != std::string::npos || lower.find("imagen") != std::string::npos ||
        lower.find("image") != std::string::npos || lower.find("png") != std::string::npos ||
        lower.find("sprite") != std::string::npos || lower.find("icon") != std::string::npos;
    return generation_verb && image_noun;
}

bool prompt_refines_generated_image(const std::string& prompt) {
    std::string lower;
    lower.reserve(prompt.size());
    std::transform(prompt.begin(), prompt.end(), std::back_inserter(lower), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return lower.find("hacela") != std::string::npos ||
        lower.find("cambiala") != std::string::npos || lower.find("modif") != std::string::npos ||
        lower.find("ajust") != std::string::npos || lower.find("variante") != std::string::npos ||
        lower.find("variant") != std::string::npos || lower.find("make it") != std::string::npos ||
        lower.find("change it") != std::string::npos;
}

std::optional<nlohmann::json> cache_generated_png(
    std::uint64_t request_id,
    const EditorAiProviderEvent& event,
    std::size_t ordinal,
    std::string& error
) {
    constexpr std::size_t MAX_GENERATED_PNG_BYTES = 32 * 1024 * 1024;
    constexpr std::uint8_t PNG_SIGNATURE[] = {137, 80, 78, 71, 13, 10, 26, 10};
    const std::filesystem::path source_path(event.artifact_path);
    std::error_code source_error;
    const std::uintmax_t source_size = std::filesystem::file_size(source_path, source_error);
    if (source_error || source_path.extension() != ".png" ||
        source_size < std::size(PNG_SIGNATURE) || source_size > MAX_GENERATED_PNG_BYTES) {
        error = "Codex ImageGen returned an invalid PNG path or size";
        return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes = FileSystem::read_bytes(event.artifact_path);
    if (bytes.size() < std::size(PNG_SIGNATURE) || bytes.size() > MAX_GENERATED_PNG_BYTES ||
        !std::equal(std::begin(PNG_SIGNATURE), std::end(PNG_SIGNATURE), bytes.begin())) {
        error = "Codex ImageGen did not return a valid PNG";
        return std::nullopt;
    }
    const std::string preview_id = "texture_" + std::to_string(request_id) + "_" +
        std::to_string(ordinal);
    const std::filesystem::path preview_path = std::filesystem::path(project_root()) /
        "Library" / "EditorAi" / "TexturePreviews" / (preview_id + ".png");
    if (!FileSystem::write_bytes(preview_path.string(), bytes)) {
        error = "Could not cache the generated PNG for review";
        return std::nullopt;
    }
    return nlohmann::json{
        {"preview_id", preview_id},
        {"source_path", event.artifact_path},
        {"preview_path", preview_path.string()},
        {"byte_count", bytes.size()},
        {"transparent_background", event.transparent_background}
    };
}

std::vector<EditorAiChatMessage> recent_history(
    const std::vector<EditorAiChatMessage>& messages
) {
    constexpr std::size_t MAX_MESSAGES = 6;
    constexpr std::size_t MAX_TOTAL_CHARACTERS = 12'000;
    constexpr std::size_t MAX_MESSAGE_CHARACTERS = 4'000;
    std::vector<EditorAiChatMessage> reversed;
    std::size_t total_characters = 0;
    for (auto message = messages.rbegin();
         message != messages.rend() && reversed.size() < MAX_MESSAGES; ++message) {
        EditorAiChatMessage copy = *message;
        if (copy.content.size() > MAX_MESSAGE_CHARACTERS) {
            copy.content.resize(MAX_MESSAGE_CHARACTERS);
            copy.content += "\n[truncated by editor context budget]";
        }
        if (!reversed.empty() && total_characters + copy.content.size() > MAX_TOTAL_CHARACTERS) break;
        total_characters += copy.content.size();
        reversed.push_back(std::move(copy));
    }
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

std::vector<EditorAiComponentProbe> build_component_probes() {
    std::vector<EditorAiComponentProbe> probes;
    for (const ComponentDescriptor& descriptor : default_component_registry().descriptors()) {
        probes.push_back({descriptor.serialized_key, descriptor.has, descriptor.serialize});
    }
    return probes;
}

std::string engine_source_root() {
#if defined(ENGINE_SOURCE_DIR)
    return ENGINE_SOURCE_DIR;
#else
    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    return error ? "." : current.string();
#endif
}

std::string project_root() {
    return AssetDatabase::get().project_root().string();
}

std::string abbreviated_id(const std::string& id) {
    constexpr std::size_t VISIBLE_CHARACTERS = 8;
    if (id.size() <= VISIBLE_CHARACTERS * 2 + 3) return id;
    return id.substr(0, VISIBLE_CHARACTERS) + "..." +
        id.substr(id.size() - VISIBLE_CHARACTERS);
}

std::string codex_resume_command(const std::string& thread_id) {
    return "codex resume --include-non-interactive " + thread_id;
}

bool has_visible_text(const char* text) {
    if (!text) return false;
    for (const unsigned char character : std::string(text)) {
        if (!std::isspace(character)) return true;
    }
    return false;
}

std::string conversation_transcript(const std::vector<EditorAiChatMessage>& messages) {
    std::string transcript;
    for (const EditorAiChatMessage& message : messages) {
        if (!transcript.empty()) transcript += "\n\n";
        transcript += message.role == "user" ? "You\n"
            : (message.role == "editor" ? "Editor\n" : "Codex\n");
        transcript += message.content;
    }
    return transcript;
}

std::unordered_map<EntityID, nlohmann::json> entity_map(const nlohmann::json& index) {
    std::unordered_map<EntityID, nlohmann::json> result;
    if (!index.is_array()) return result;
    for (const nlohmann::json& entity : index) {
        const EntityID id = entity.value("entity_id", EntityID{0});
        if (id != 0) result.emplace(id, entity);
    }
    return result;
}

nlohmann::json receipt_entity(const nlohmann::json& entity) {
    return {
        {"entity_id", entity.value("entity_id", EntityID{0})},
        {"name", entity.value("name", "")}
    };
}

std::unordered_map<std::string, nlohmann::json> asset_map(const nlohmann::json& index) {
    std::unordered_map<std::string, nlohmann::json> result;
    if (!index.is_array()) return result;
    for (const nlohmann::json& asset : index) {
        const std::string handle = asset.value("asset_handle", "");
        if (!handle.empty()) result.emplace(handle, asset);
    }
    return result;
}

nlohmann::json receipt_asset(const nlohmann::json& asset) {
    return {
        {"asset_handle", asset.value("asset_handle", "")},
        {"path", asset.value("path", "")},
        {"type", asset.value("type", "")},
        {"status", asset.value("status", "")}
    };
}

void remove_cached_texture_previews(const nlohmann::json& previews) {
    if (!previews.is_object()) return;
    const std::filesystem::path allowed_directory = (
        std::filesystem::path(project_root()) / "Library" / "EditorAi" / "TexturePreviews"
    ).lexically_normal();
    for (const auto& [id, preview] : previews.items()) {
        static_cast<void>(id);
        if (!preview.is_object()) continue;
        const std::filesystem::path path =
            std::filesystem::path(preview.value("preview_path", "")).lexically_normal();
        if (path.empty() || path.parent_path() != allowed_directory) continue;
        std::error_code error;
        std::filesystem::remove(path, error);
    }
}

nlohmann::json build_application_receipt(
    std::uint64_t request_id,
    std::uint64_t revision_before,
    std::uint64_t revision_after,
    const std::vector<EditorAiAuthoringAction>& actions,
    const nlohmann::json& before_index,
    const nlohmann::json& after_index,
    const nlohmann::json& before_asset_index,
    const nlohmann::json& after_asset_index
) {
    const auto before = entity_map(before_index);
    const auto after = entity_map(after_index);
    nlohmann::json added = nlohmann::json::array();
    nlohmann::json removed = nlohmann::json::array();
    nlohmann::json updated = nlohmann::json::array();
    constexpr std::size_t MAX_RECEIPT_ENTITIES = 100;
    std::size_t added_count = 0;
    std::size_t removed_count = 0;
    std::size_t updated_count = 0;
    const auto before_assets = asset_map(before_asset_index);
    const auto after_assets = asset_map(after_asset_index);
    nlohmann::json added_assets = nlohmann::json::array();
    nlohmann::json removed_assets = nlohmann::json::array();
    nlohmann::json updated_assets = nlohmann::json::array();
    std::size_t added_asset_count = 0;
    std::size_t removed_asset_count = 0;
    std::size_t updated_asset_count = 0;

    for (const auto& [id, entity] : after) {
        const auto previous = before.find(id);
        if (previous == before.end()) {
            ++added_count;
            if (added.size() < MAX_RECEIPT_ENTITIES) added.push_back(receipt_entity(entity));
            continue;
        }
        nlohmann::json fields = nlohmann::json::array();
        for (const char* field : {"name", "parent_entity_id", "child_entity_ids", "transform", "components", "component_data"}) {
            if (previous->second.value(field, nlohmann::json{}) != entity.value(field, nlohmann::json{})) {
                fields.push_back(field);
            }
        }
        if (!fields.empty() && updated.size() < MAX_RECEIPT_ENTITIES) {
            ++updated_count;
            nlohmann::json item = receipt_entity(entity);
            item["fields"] = std::move(fields);
            updated.push_back(std::move(item));
        } else if (!fields.empty()) ++updated_count;
    }
    for (const auto& [id, entity] : before) {
        if (!after.contains(id) && removed.size() < MAX_RECEIPT_ENTITIES) {
            ++removed_count;
            removed.push_back(receipt_entity(entity));
        } else if (!after.contains(id)) ++removed_count;
    }
    for (const auto& [handle, asset] : after_assets) {
        const auto previous = before_assets.find(handle);
        if (previous == before_assets.end()) {
            ++added_asset_count;
            if (added_assets.size() < MAX_RECEIPT_ENTITIES) {
                added_assets.push_back(receipt_asset(asset));
            }
        } else if (previous->second != asset) {
            ++updated_asset_count;
            if (updated_assets.size() < MAX_RECEIPT_ENTITIES) {
                updated_assets.push_back(receipt_asset(asset));
            }
        }
    }
    for (const auto& [handle, asset] : before_assets) {
        if (after_assets.contains(handle)) continue;
        ++removed_asset_count;
        if (removed_assets.size() < MAX_RECEIPT_ENTITIES) {
            removed_assets.push_back(receipt_asset(asset));
        }
    }

    nlohmann::json action_summaries = nlohmann::json::array();
    for (const EditorAiAuthoringAction& action : actions) action_summaries.push_back(action.summary);
    return {
        {"schema_version", 1},
        {"request_id", request_id},
        {"status", "applied"},
        {"verified", revision_after == revision_before + 1 &&
            (added_count + removed_count + updated_count + added_asset_count +
             removed_asset_count + updated_asset_count) > 0},
        {"revision_before", revision_before},
        {"revision_after", revision_after},
        {"action_count", actions.size()},
        {"actions", std::move(action_summaries)},
        {"changes", {
            {"added", std::move(added)},
            {"removed", std::move(removed)},
            {"updated", std::move(updated)},
            {"added_count", added_count},
            {"removed_count", removed_count},
            {"updated_count", updated_count},
            {"truncated", added_count > MAX_RECEIPT_ENTITIES ||
                removed_count > MAX_RECEIPT_ENTITIES || updated_count > MAX_RECEIPT_ENTITIES},
            {"before_entity_count", before.size()},
            {"after_entity_count", after.size()}
        }},
        {"asset_changes", {
            {"added", std::move(added_assets)},
            {"removed", std::move(removed_assets)},
            {"updated", std::move(updated_assets)},
            {"added_count", added_asset_count},
            {"removed_count", removed_asset_count},
            {"updated_count", updated_asset_count},
            {"before_asset_count", before_assets.size()},
            {"after_asset_count", after_assets.size()}
        }}
    };
}

std::string receipt_summary(const nlohmann::json& receipt) {
    const auto& changes = receipt.at("changes");
    const auto& assets = receipt.value("asset_changes", nlohmann::json::object());
    std::string summary = std::string(receipt.value("verified", false)
        ? "Verified editor receipt: "
        : "Editor receipt (no serialized change observed): ") +
        std::to_string(changes.value("added_count", 0U)) + " added, " +
        std::to_string(changes.value("removed_count", 0U)) + " removed, " +
        std::to_string(changes.value("updated_count", 0U)) + " updated; revision " +
        std::to_string(receipt.value("revision_before", 0ULL)) + " -> " +
        std::to_string(receipt.value("revision_after", 0ULL)) + ". Assets: " +
        std::to_string(assets.value("added_count", 0U)) + " added, " +
        std::to_string(assets.value("removed_count", 0U)) + " removed, " +
        std::to_string(assets.value("updated_count", 0U)) + " updated.";
    if (assets.value("added_count", 0U) == 1U && assets.contains("added") &&
        assets.at("added").is_array() && !assets.at("added").empty()) {
        const auto& asset = assets.at("added").front();
        summary += " New asset: " + asset.value("path", "") + " (handle " +
            asset.value("asset_handle", "") + ").";
    }
    return summary;
}

void draw_codex_badge() {
    constexpr ImVec2 PADDING(9.0f, 5.0f);
    const ImVec2 text_size = ImGui::CalcTextSize("Codex");
    const ImVec2 size(text_size.x + PADDING.x * 2.0f, text_size.y + PADDING.y * 2.0f);
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImVec2 maximum(minimum.x + size.x, minimum.y + size.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        minimum,
        maximum,
        ImGui::GetColorU32(ImGuiCol_FrameBgHovered),
        7.0f
    );
    draw_list->AddText(
        ImVec2(minimum.x + PADDING.x, minimum.y + PADDING.y),
        IM_COL32(245, 245, 242, 255),
        "Codex"
    );
    ImGui::Dummy(size);
}

bool round_icon_button(
    const char* id,
    EditorIcon icon,
    const char* tooltip,
    bool enabled,
    bool emphasized
) {
    constexpr float DIAMETER = 32.0f;
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton("##button", ImVec2(DIAMETER, DIAMETER));
    ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const ImVec2 center(
        (minimum.x + maximum.x) * 0.5f,
        (minimum.y + maximum.y) * 0.5f
    );
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 background = !enabled
        ? IM_COL32(57, 62, 80, 145)
        : (emphasized
            ? (hovered ? IM_COL32(245, 245, 242, 255) : IM_COL32(218, 218, 213, 255))
            : (hovered ? IM_COL32(91, 82, 196, 170) : IM_COL32(72, 66, 145, 120)));
    const ImU32 foreground = !enabled
        ? IM_COL32(145, 145, 140, 150)
        : (emphasized ? IM_COL32(48, 49, 47, 255) : IM_COL32(225, 226, 222, 255));
    draw_list->AddCircleFilled(center, DIAMETER * 0.5f, background, 28);
    draw_editor_icon(draw_list, icon, center, 14.0f, foreground);
    if (hovered && tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return enabled && pressed;
}

std::string compact_model_name(const std::string& model) {
    if (model == "gpt-5.6-sol") return "5.6 Sol";
    if (model == "gpt-5.6-terra") return "5.6 Terra";
    if (model == "gpt-5.6-luna") return "5.6 Luna";
    return model;
}

std::string activity_item_label(const std::string& item_type) {
    if (item_type == "reasoning") return "Reasoning";
    if (item_type == "agentMessage") return "Writing response";
    if (item_type == "imageGeneration") return "Generating image";
    if (item_type == "dynamicToolCall") return "Using editor tool";
    if (item_type == "webSearch") return "Searching the web";
    if (item_type == "commandExecution") return "Running command";
    if (item_type == "fileChange") return "Preparing file changes";
    if (item_type == "contextCompaction") return "Compacting context";
    return item_type.empty() ? "Codex activity" : item_type;
}

std::string friendly_request_error(const std::string& error) {
    if (error.find("did not send its execution request") != std::string::npos) {
        return "Codex stopped before the announced editor tool could run. The incomplete proposal "
            "including actions queued by earlier tool calls was discarded. No scene changes from "
            "this turn were applied, so you can retry safely.";
    }
    if (error.find("produced no events") != std::string::npos) {
        return "Codex stopped producing events, so the editor interrupted the turn. Incomplete "
            "proposal changes were discarded and the request can be retried.";
    }
    if (error.find("already has an active writer") != std::string::npos) {
        return "Codex could not start the verification because the previous turn was still "
            "closing. Applied editor changes were preserved.";
    }
    return error;
}

std::string wrap_selectable_text(const std::string& text, float width) {
    if (text.empty() || width <= 1.0f) return text;
    std::string wrapped;
    wrapped.reserve(text.size() + text.size() / 24);
    std::string line;
    std::size_t cursor = 0;
    const auto flush_line = [&]() {
        wrapped += line;
        line.clear();
    };
    while (cursor < text.size()) {
        if (text[cursor] == '\n') {
            flush_line();
            wrapped.push_back('\n');
            ++cursor;
            continue;
        }
        const std::size_t word_end = text.find_first_of(" \n", cursor);
        const std::size_t end = word_end == std::string::npos ? text.size() : word_end;
        std::string word = text.substr(cursor, end - cursor);
        if (end < text.size() && text[end] == ' ') word.push_back(' ');
        if (!line.empty() && ImGui::CalcTextSize((line + word).c_str()).x > width) {
            flush_line();
            wrapped.push_back('\n');
        }
        while (!word.empty() && ImGui::CalcTextSize(word.c_str()).x > width) {
            std::size_t split = 1;
            while (split < word.size() &&
                ImGui::CalcTextSize(word.substr(0, split + 1).c_str()).x <= width) {
                ++split;
            }
            line += word.substr(0, split);
            flush_line();
            wrapped.push_back('\n');
            word.erase(0, split);
        }
        line += word;
        cursor = end + (end < text.size() && text[end] == ' ' ? 1 : 0);
    }
    flush_line();
    return wrapped;
}

std::string ellipsize_text_to_width(const std::string& text, float maximum_width) {
    if (text.empty() || ImGui::CalcTextSize(text.c_str()).x <= maximum_width) return text;
    constexpr const char* ELLIPSIS = "...";
    const float ellipsis_width = ImGui::CalcTextSize(ELLIPSIS).x;
    if (maximum_width <= ellipsis_width) return ELLIPSIS;

    std::size_t low = 0;
    std::size_t high = text.size();
    while (low < high) {
        const std::size_t middle = low + (high - low + 1) / 2;
        const std::string candidate = text.substr(0, middle) + ELLIPSIS;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maximum_width) low = middle;
        else high = middle - 1;
    }
    while (low > 0 && low < text.size() &&
        (static_cast<unsigned char>(text[low]) & 0xC0U) == 0x80U) {
        --low;
    }
    return text.substr(0, low) + ELLIPSIS;
}

float selectable_text_height(const std::string& text, float maximum_height = 0.0f) {
    const std::size_t lines = 1 + static_cast<std::size_t>(std::count(
        text.begin(), text.end(), '\n'
    ));
    const float height = static_cast<float>(lines) * ImGui::GetTextLineHeight() + 4.0f;
    return maximum_height > 0.0f ? std::min(height, maximum_height) : height;
}

void draw_selectable_text(
    const char* id,
    const std::string& text,
    float width,
    float maximum_height = 0.0f
) {
    std::string wrapped = wrap_selectable_text(text, std::max(1.0f, width));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 1.0f));
    ImGui::InputTextMultiline(
        id,
        wrapped.data(),
        wrapped.size() + 1,
        ImVec2(width, selectable_text_height(wrapped, maximum_height)),
        ImGuiInputTextFlags_ReadOnly
    );
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

bool draw_chat_message(
    const EditorAiChatMessage& message,
    int index,
    float available_width
) {
    bool copy_requested = false;
    const bool is_user = message.role == "user";
    const bool is_editor = message.role == "editor";
    const float bubble_width = std::max(
        140.0f,
        available_width * (is_user ? 0.82f : 0.94f)
    );
    if (is_user) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available_width - bubble_width);
    }

    const ImVec4 background = is_user
        ? ImVec4(0.17f, 0.18f, 0.27f, 1.0f)
        : (is_editor
            ? ImVec4(0.19f, 0.18f, 0.13f, 1.0f)
            : ImVec4(0.12f, 0.13f, 0.17f, 1.0f));
    const ImVec4 label_color = is_user
        ? ImVec4(0.72f, 0.69f, 1.0f, 1.0f)
        : (is_editor
            ? ImVec4(0.88f, 0.70f, 0.28f, 1.0f)
            : ImVec4(0.40f, 0.82f, 0.62f, 1.0f));
    const float text_width = std::max(1.0f, bubble_width - 20.0f);
    const std::string wrapped_content = wrap_selectable_text(message.content, text_width);
    const float text_height = selectable_text_height(wrapped_content);
    const float bubble_height = ImGui::GetTextLineHeightWithSpacing() + text_height + 20.0f;

    ImGui::PushID(index);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::BeginChild(
        "Message",
        ImVec2(bubble_width, bubble_height),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::TextColored(
        label_color,
        "%s",
        is_user ? "You" : (is_editor ? "Editor" : "Codex")
    );
    draw_selectable_text("##content", message.content, text_width);
    ImGui::EndChild();
    if (!is_user) {
        if (editor_icon_button("copy", EditorIcon::Copy, "Copy response")) {
            copy_requested = true;
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::Spacing();
    return copy_requested;
}

} // namespace

AiAssistantPanel::AiAssistantPanel(
    SceneManager* scene_manager,
    std::shared_ptr<EditorSelection> selection,
    ComponentSerializationContext serialization_context
) : m_scene_manager(scene_manager),
    m_selection(std::move(selection)),
    m_serialization_context(serialization_context),
    m_tool_registry(build_component_probes(), engine_source_root()),
    m_authoring_registry(&default_component_registry()),
    m_attachments(serialization_context.renderer, project_root()),
    m_codex_provider(std::make_unique<CodexAppServerProvider>(
        create_default_codex_app_server_transport()
    )),
    m_openai_provider(std::make_unique<OpenAiResponsesProvider>(
        create_default_editor_ai_http_transport()
    )),
    m_conversation_store(std::make_unique<EditorAiConversationStore>(
        (std::filesystem::path(project_root()) / "Library/EditorAi/conversations.json").string()
    )),
    m_codex_executable(find_default_codex_executable()),
    m_imagegen_skill_path(find_default_codex_skill_path("imagegen")) {
    is_open = false;
    std::snprintf(m_model.data(), m_model.size(), "%s", "gpt-5.6-sol");
    std::string load_error;
    if (!m_conversation_store->load(load_error)) {
        ENGINE_LOG_WARN("[AI AUDIT] conversation_store=load_failed error={}", load_error);
    } else if (const auto* conversation = m_conversation_store->active_conversation()) {
        m_messages = conversation->messages;
        if (conversation->tool_contract_version == EDITOR_AI_TOOL_CONTRACT_VERSION) {
            m_codex_thread_id = conversation->thread_id;
            m_codex_session_id = conversation->session_id;
        } else {
            m_superseded_thread_id = conversation->thread_id;
            m_ui_feedback = "Editor tools changed; the next message will upgrade this conversation.";
        }
        if (!conversation->model.empty()) {
            std::snprintf(m_model.data(), m_model.size(), "%s", conversation->model.c_str());
        }
        m_collaboration_mode = conversation->collaboration_mode;
        m_plan_revision = conversation->plan_revision;
        m_plan_text = conversation->plan_text;
        m_plan_explanation = conversation->plan_explanation;
        m_plan = conversation->plan;
        m_scroll_chat_to_bottom = true;
        ENGINE_LOG_INFO(
            "[AI AUDIT] conversation_store=restored thread={} messages={} contract={} current_contract={} compatible={}",
            conversation->thread_id,
            conversation->messages.size(),
            conversation->tool_contract_version,
            EDITOR_AI_TOOL_CONTRACT_VERSION,
            conversation->tool_contract_version == EDITOR_AI_TOOL_CONTRACT_VERSION
        );
    }
}

AiAssistantPanel::~AiAssistantPanel() {
    cancel_request();
    cancel_live_editor_tools("The editor is shutting down");
    if (m_request_thread.joinable()) m_request_thread.join();
}

nlohmann::json AiAssistantPanel::viewport_state_json() const {
    const std::shared_ptr<ViewportPanel> viewport_panel = m_viewport_panel.lock();
    if (!viewport_panel) {
        return {{"available", false}, {"error", "The Scene Viewport panel is unavailable"}};
    }
    ViewportAiState state;
    std::string error;
    if (!viewport_panel->get_ai_view_state(state, error)) {
        return {{"available", false}, {"error", std::move(error)}};
    }
    nlohmann::json output = viewport_state_output(state);
    output["available"] = true;
    output["navigation"] = "ephemeral";
    return output;
}

EditorAiToolResult AiAssistantPanel::execute_live_editor_tool(
    const std::string& name,
    const nlohmann::json& arguments,
    const std::shared_ptr<Scene>& expected_scene,
    std::uint64_t expected_revision
) {
    EditorAiViewportToolCommand command;
    std::string error;
    if (!m_viewport_tools.parse(name, arguments, command, error)) {
        return {false, {}, std::move(error)};
    }
    auto request = std::make_shared<LiveEditorToolRequest>();
    request->name = name;
    request->arguments = arguments;
    request->command = std::move(command);
    request->expected_scene = expected_scene;
    request->expected_revision = expected_revision;
    request->queued_at = std::chrono::steady_clock::now();
    std::future<EditorAiToolResult> completion = request->completion.get_future();
    {
        std::lock_guard lock(m_live_tool_mutex);
        m_live_tool_requests.push_back(request);
    }

    constexpr auto TOOL_TIMEOUT = std::chrono::seconds(8);
    if (completion.wait_for(TOOL_TIMEOUT) != std::future_status::ready) {
        LiveEditorToolState expected = LiveEditorToolState::Queued;
        if (request->state.compare_exchange_strong(expected, LiveEditorToolState::Abandoned)) {
            return {false, {}, "The editor viewport did not respond within 8 seconds"};
        }
        if (expected == LiveEditorToolState::Executing) {
            completion.wait();
            return completion.get();
        }
        return {false, {}, "The editor viewport request was abandoned"};
    }
    return completion.get();
}

EditorAiToolResult AiAssistantPanel::execute_live_editor_tool_on_main_thread(
    const LiveEditorToolRequest& request
) {
    const std::shared_ptr<Scene> expected_scene = request.expected_scene.lock();
    const std::shared_ptr<Scene> active_scene = m_scene_manager
        ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    if (!expected_scene || expected_scene != active_scene) {
        return {false, {}, "The active scene changed while Codex was inspecting the viewport"};
    }
    if (m_command_history && m_command_history->get_current_revision() != request.expected_revision) {
        return {false, {}, "The scene revision changed while Codex was inspecting the viewport"};
    }

    const EditorAiViewportToolCommand& command = request.command;
    if (command.type == EditorAiViewportToolType::GetDiagnostics) {
        const std::vector<SceneDiagnostic> diagnostics = SceneValidator::validate(
            active_scene->get_entity_manager(), m_serialization_context.asset_database
        );
        std::size_t errors = 0;
        std::size_t warnings = 0;
        nlohmann::json items = nlohmann::json::array();
        for (const SceneDiagnostic& diagnostic : diagnostics) {
            diagnostic.severity == SceneDiagnosticSeverity::Error ? ++errors : ++warnings;
            if (items.size() >= command.limit) continue;
            nlohmann::json item{
                {"severity", diagnostic.severity == SceneDiagnosticSeverity::Error
                    ? "error" : "warning"},
                {"code", diagnostic_code_name(diagnostic.code)},
                {"message", diagnostic.message}
            };
            if (diagnostic.entity != 0) item["entity_id"] = diagnostic.entity;
            if (diagnostic.related_entity != 0) {
                item["related_entity_id"] = diagnostic.related_entity;
            }
            if (diagnostic.asset_handle.is_valid()) {
                item["asset_handle"] = diagnostic.asset_handle.to_string();
            }
            if (!diagnostic.asset_path.empty()) item["asset_path"] = diagnostic.asset_path;
            items.push_back(std::move(item));
        }
        return {
            true,
            {
                {"scene", active_scene->get_name()},
                {"passed", errors == 0},
                {"error_count", errors},
                {"warning_count", warnings},
                {"total_count", diagnostics.size()},
                {"returned_count", items.size()},
                {"truncated", items.size() < diagnostics.size()},
                {"diagnostics", std::move(items)}
            },
            {}
        };
    }

    const std::shared_ptr<ViewportPanel> viewport_panel = m_viewport_panel.lock();
    if (!viewport_panel) {
        return {false, {}, "The Scene Viewport panel is unavailable"};
    }

    std::string error;
    bool changed = false;
    switch (command.type) {
        case EditorAiViewportToolType::GetState:
        case EditorAiViewportToolType::Capture:
        case EditorAiViewportToolType::GetDiagnostics:
            break;
        case EditorAiViewportToolType::Pan:
            changed = viewport_panel->ai_pan({command.x, command.y}, error);
            break;
        case EditorAiViewportToolType::Zoom:
            changed = viewport_panel->ai_zoom(command.factor, error);
            break;
        case EditorAiViewportToolType::FocusEntities:
            changed = viewport_panel->ai_focus_entities(
                command.entity_ids, command.padding, error
            );
            break;
        case EditorAiViewportToolType::FrameAll:
            changed = viewport_panel->ai_frame_all(command.padding, error);
            break;
        case EditorAiViewportToolType::Reset:
            changed = viewport_panel->ai_reset(error);
            break;
    }
    if (!error.empty()) return {false, {}, std::move(error)};

    ViewportAiState state;
    if (!viewport_panel->get_ai_view_state(state, error)) {
        return {false, {}, std::move(error)};
    }

    if (command.type != EditorAiViewportToolType::Capture) {
        nlohmann::json output = viewport_state_output(state);
        output["navigation_changed"] = changed;
        output["scene_changed"] = false;
        return {true, std::move(output), {}};
    }

    const std::filesystem::path capture_path = std::filesystem::path(project_root()) /
        "Library" / "EditorAi" / "ViewportCaptures" /
        ("capture_" + std::to_string(++m_viewport_capture_sequence) + ".png");
    std::size_t byte_count = 0;
    if (!viewport_panel->capture_viewport_png(capture_path.string(), byte_count, error)) {
        return {false, {}, std::move(error)};
    }
    constexpr std::size_t MAX_CAPTURE_BYTES = 12 * 1024 * 1024;
    const std::vector<std::uint8_t> bytes = FileSystem::read_bytes(capture_path.string());
    static_cast<void>(FileSystem::remove(capture_path.string()));
    constexpr std::uint8_t PNG_SIGNATURE[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < std::size(PNG_SIGNATURE) || bytes.size() > MAX_CAPTURE_BYTES ||
        !std::equal(std::begin(PNG_SIGNATURE), std::end(PNG_SIGNATURE), bytes.begin())) {
        return {false, {}, "The viewport capture was not a valid bounded PNG"};
    }

    nlohmann::json output = viewport_state_output(state);
    output["scene"] = active_scene->get_name();
    output["format"] = "png";
    output["byte_count"] = byte_count;
    output["visual_content_attached"] = true;
    return {
        true,
        std::move(output),
        {},
        "data:image/png;base64," + Base64::encode(bytes)
    };
}

void AiAssistantPanel::process_live_editor_tools() {
    std::deque<std::shared_ptr<LiveEditorToolRequest>> requests;
    {
        std::lock_guard lock(m_live_tool_mutex);
        requests.swap(m_live_tool_requests);
    }
    for (const auto& request : requests) {
        if (!request) continue;
        LiveEditorToolState expected = LiveEditorToolState::Queued;
        if (!request->state.compare_exchange_strong(expected, LiveEditorToolState::Executing)) {
            continue;
        }
        const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - request->queued_at
        ).count();
        ENGINE_LOG_INFO(
            "[AI AUDIT] phase=live_tool_dispatch name={} queue_wait_ms={}",
            request->name,
            wait_ms
        );
        const auto execute_started = std::chrono::steady_clock::now();
        EditorAiToolResult result = execute_live_editor_tool_on_main_thread(*request);
        const std::uint64_t render_generation = result.output.is_object()
            ? result.output.value("render_generation", std::uint64_t{0}) : 0;
        const std::size_t capture_bytes = result.output.is_object()
            ? result.output.value("byte_count", std::size_t{0}) : 0;
        ENGINE_LOG_INFO(
            "[AI AUDIT] phase=live_tool_complete name={} success={} execute_ms={} "
            "render_generation={} capture_bytes={} visual_attached={} error={}",
            request->name,
            result.success,
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - execute_started
            ).count(),
            render_generation,
            capture_bytes,
            !result.image_url.empty(),
            result.success ? "none" : result.error
        );
        request->state.store(LiveEditorToolState::Completed);
        request->completion.set_value(std::move(result));
    }
}

void AiAssistantPanel::cancel_live_editor_tools(const std::string& reason) {
    std::deque<std::shared_ptr<LiveEditorToolRequest>> requests;
    {
        std::lock_guard lock(m_live_tool_mutex);
        requests.swap(m_live_tool_requests);
    }
    for (const auto& request : requests) {
        if (!request) continue;
        LiveEditorToolState expected = LiveEditorToolState::Queued;
        if (request->state.compare_exchange_strong(expected, LiveEditorToolState::Abandoned)) {
            request->completion.set_value({false, {}, reason});
        }
    }
}

std::string AiAssistantPanel::current_context_json() const {
    const Scene* active_scene = m_scene_manager ? m_scene_manager->get_active_scene() : nullptr;
    nlohmann::json context = m_tool_registry.build_context_snapshot(
        active_scene, m_selection.get(), m_serialization_context
    );
    context["authoring"] = {
        {"mode", m_authoring_mode == AuthoringMode::AutonomousSafe
            ? "autonomous_safe" : "confirm_before_apply"},
        {"collaboration", m_collaboration_mode == EditorAiCollaborationMode::Plan
            ? "plan" : "build"},
        {"filesystem", m_collaboration_mode == EditorAiCollaborationMode::Plan
            ? "read_only" : "full_access"},
        {"max_actions_per_proposal", EditorAiAuthoringRegistry::MAX_ACTIONS_PER_PROPOSAL},
        {"tools", m_authoring_registry.tool_schemas()}
    };
    context["plan"] = {
        {"revision", m_plan_revision},
        {"available", !m_plan_text.empty()}
    };
    context["viewport"] = viewport_state_json();
    context["viewport"]["tools"] = m_viewport_tools.tool_schemas();
    return context.dump(2);
}

void AiAssistantPanel::prune_attached_contexts() {
    m_attachments.prune_entities(
        m_scene_manager ? m_scene_manager->get_active_scene_shared_ptr() : nullptr
    );
}

void AiAssistantPanel::attach_current_selection() {
    const std::shared_ptr<Scene> active_scene = m_scene_manager
        ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    const EditorAiAttachmentResult result = m_attachments.attach_selection(
        active_scene, m_selection.get()
    );
    m_focus_prompt_requested = true;
    std::lock_guard lock(m_request_mutex);
    m_ui_feedback = result.message;
}

void AiAssistantPanel::attach_asset_paths(
    const std::vector<std::filesystem::path>& paths
) {
    const EditorAiAttachmentResult result = m_attachments.attach_assets(
        paths, m_serialization_context.asset_database
    );
    m_focus_prompt_requested = true;
    std::lock_guard lock(m_request_mutex);
    m_ui_feedback = result.message;
}

void AiAssistantPanel::attach_clipboard_screenshot() {
    constexpr const char* MIME_TYPES[] = {"image/png", "image/jpeg"};
    std::optional<EditorAiAttachmentResult> rejection;
    for (const char* mime_type : MIME_TYPES) {
        if (!SDL_HasClipboardData(mime_type)) continue;
        std::size_t size = 0;
        void* data = SDL_GetClipboardData(mime_type, &size);
        if (!data) continue;
        const EditorAiAttachmentResult result = m_attachments.attach_screenshot_bytes(
            static_cast<const std::uint8_t*>(data), size, mime_type, "Clipboard screenshot"
        );
        SDL_free(data);
        if (result.success) {
            m_focus_prompt_requested = true;
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = result.message;
            return;
        }
        rejection = result;
    }
    std::lock_guard lock(m_request_mutex);
    m_ui_feedback = rejection
        ? rejection->message
        : "The clipboard does not contain a supported PNG or JPEG screenshot.";
}

void AiAssistantPanel::attach_viewport_screenshot() {
    const std::shared_ptr<ViewportPanel> viewport = m_viewport_panel.lock();
    if (!viewport) {
        std::lock_guard lock(m_request_mutex);
        m_ui_feedback = "The Scene Viewport is unavailable.";
        return;
    }
    const std::filesystem::path temporary = std::filesystem::path(project_root()) /
        "Library" / "EditorAi" / "ScreenshotAttachments" /
        ("viewport_source_" + std::to_string(NEXT_AI_REQUEST_ID.fetch_add(1)) + ".png");
    std::size_t byte_count = 0;
    std::string error;
    if (!viewport->capture_viewport_png(temporary.string(), byte_count, error)) {
        std::lock_guard lock(m_request_mutex);
        m_ui_feedback = error.empty() ? "Could not capture the Scene Viewport." : error;
        return;
    }
    const std::vector<std::uint8_t> bytes = FileSystem::read_bytes(temporary.string());
    static_cast<void>(FileSystem::remove(temporary.string()));
    const EditorAiAttachmentResult result = m_attachments.attach_screenshot_bytes(
        bytes.data(), bytes.size(), "image/png", "Scene Viewport"
    );
    m_focus_prompt_requested = true;
    std::lock_guard lock(m_request_mutex);
    m_ui_feedback = result.message;
}

void AiAssistantPanel::cancel_request() {
    m_cancel_requested.store(true);
    static_cast<void>(ProjectCommandRegistry::cancel_active());
    cancel_live_editor_tools("The viewport request was cancelled");
    ENGINE_LOG_INFO("[AI AUDIT] ui_action=cancel_request");
}

void AiAssistantPanel::start_new_conversation() {
    std::lock_guard lock(m_request_mutex);
    if (m_request_running || m_pending_proposal) return;
    m_attachments.clear_all();
    m_pending_asset_context_paths.clear();
    m_iterative_authoring.reset();
    m_pending_user_input.reset();
    m_iteration_continuation_requested = false;
    m_iteration_verification_pending = false;
    m_attach_assets_requested = false;
    m_messages.clear();
    m_streamed_response.clear();
    m_reasoning_summary.clear();
    m_request_error.clear();
    m_active_tool.clear();
    m_live_activity = "Idle";
    m_activity_entries.clear();
    m_request_started_at = {};
    m_authoring_feedback.clear();
    m_ui_feedback = "New conversation ready.";
    m_codex_thread_id.clear();
    m_codex_session_id.clear();
    m_superseded_thread_id.clear();
    m_conversation_has_generated_image = false;
    m_compact_before_next_request = false;
    m_collaboration_mode = EditorAiCollaborationMode::Default;
    m_plan.clear();
    m_plan_text.clear();
    m_plan_explanation.clear();
    m_plan_revision = 0;
    m_approved_plan_text.clear();
    m_plan_build_requested = false;
    m_last_input_tokens = 0;
    m_last_output_tokens = 0;
    m_last_total_tokens = 0;
    m_last_tool_calls = 0;
    m_last_request_duration_ms = 0;
    m_scroll_chat_to_bottom = false;
    m_focus_prompt_requested = true;
    m_prompt.fill('\0');
    if (m_conversation_store) {
        static_cast<void>(m_conversation_store->set_active_thread(""));
        std::string save_error;
        if (!m_conversation_store->save(save_error)) {
            m_request_error = save_error;
        }
    }
    ENGINE_LOG_INFO("[AI AUDIT] ui_action=new_conversation");
}

void AiAssistantPanel::restore_conversation(const std::string& thread_id) {
    std::lock_guard lock(m_request_mutex);
    if (m_request_running || m_pending_proposal || !m_conversation_store) return;
    const EditorAiConversationRecord* conversation = m_conversation_store->find(thread_id);
    if (!conversation) {
        m_request_error = "The selected conversation is no longer available.";
        return;
    }
    m_attachments.clear_all();
    m_pending_asset_context_paths.clear();
    m_iterative_authoring.reset();
    m_pending_user_input.reset();
    m_iteration_continuation_requested = false;
    m_iteration_verification_pending = false;
    m_attach_assets_requested = false;
    if (conversation->tool_contract_version == EDITOR_AI_TOOL_CONTRACT_VERSION) {
        m_codex_thread_id = conversation->thread_id;
        m_codex_session_id = conversation->session_id;
        m_superseded_thread_id.clear();
    } else {
        m_codex_thread_id.clear();
        m_codex_session_id.clear();
        m_superseded_thread_id = conversation->thread_id;
    }
    m_messages = conversation->messages;
    m_collaboration_mode = conversation->collaboration_mode;
    m_plan_revision = conversation->plan_revision;
    m_plan_text = conversation->plan_text;
    m_plan_explanation = conversation->plan_explanation;
    m_plan = conversation->plan;
    m_approved_plan_text.clear();
    m_plan_build_requested = false;
    m_conversation_has_generated_image = false;
    m_compact_before_next_request = false;
    m_streamed_response.clear();
    m_reasoning_summary.clear();
    m_request_error.clear();
    m_active_tool.clear();
    m_live_activity = "Idle";
    m_activity_entries.clear();
    m_request_started_at = {};
    m_authoring_feedback.clear();
    m_ui_feedback = conversation->tool_contract_version == EDITOR_AI_TOOL_CONTRACT_VERSION
        ? "Conversation restored."
        : "Conversation restored; the next message will upgrade its editor tools.";
    m_last_input_tokens = conversation->last_input_tokens;
    m_last_output_tokens = conversation->last_output_tokens;
    m_last_total_tokens = conversation->last_total_tokens;
    m_last_tool_calls = 0;
    m_last_request_duration_ms = 0;
    m_scroll_chat_to_bottom = true;
    m_focus_prompt_requested = true;
    m_prompt.fill('\0');
    if (!conversation->model.empty()) {
        std::snprintf(m_model.data(), m_model.size(), "%s", conversation->model.c_str());
    }
    static_cast<void>(m_conversation_store->set_active_thread(thread_id));
    std::string save_error;
    if (!m_conversation_store->save(save_error)) m_request_error = save_error;
    ENGINE_LOG_INFO(
        "[AI AUDIT] conversation_store=resumed thread={} messages={}",
        conversation->thread_id,
        conversation->messages.size()
    );
}

void AiAssistantPanel::rename_conversation(
    const std::string& thread_id,
    const std::string& title
) {
    std::lock_guard lock(m_request_mutex);
    if (!m_conversation_store || m_request_running || m_pending_proposal) return;
    if (!m_conversation_store->rename(thread_id, title)) {
        m_request_error = "Conversation could not be renamed. Enter a non-empty title.";
        return;
    }
    std::string save_error;
    if (!m_conversation_store->save(save_error)) {
        m_request_error = save_error;
        return;
    }
    m_request_error.clear();
    m_ui_feedback = "Conversation renamed.";
    ENGINE_LOG_INFO("[AI AUDIT] conversation_store=renamed thread={}", thread_id);
}

void AiAssistantPanel::delete_conversation(const std::string& thread_id) {
    std::lock_guard lock(m_request_mutex);
    if (!m_conversation_store || m_request_running || m_pending_proposal) return;
    if (!m_conversation_store->erase(thread_id)) {
        m_request_error = "The selected conversation is no longer available.";
        return;
    }
    if (m_codex_thread_id == thread_id || m_superseded_thread_id == thread_id) {
        m_attachments.clear_all();
        m_pending_asset_context_paths.clear();
        m_attach_assets_requested = false;
        m_messages.clear();
        m_streamed_response.clear();
        m_reasoning_summary.clear();
        m_codex_thread_id.clear();
        m_codex_session_id.clear();
        m_superseded_thread_id.clear();
        m_conversation_has_generated_image = false;
        m_compact_before_next_request = false;
        m_collaboration_mode = EditorAiCollaborationMode::Default;
        m_plan.clear();
        m_plan_text.clear();
        m_plan_explanation.clear();
        m_plan_revision = 0;
        m_approved_plan_text.clear();
        m_plan_build_requested = false;
        m_live_activity = "Idle";
        m_activity_entries.clear();
        m_request_started_at = {};
        m_last_input_tokens = 0;
        m_last_output_tokens = 0;
        m_last_total_tokens = 0;
        m_last_tool_calls = 0;
        m_last_request_duration_ms = 0;
        m_prompt.fill('\0');
        m_focus_prompt_requested = true;
    }
    std::string save_error;
    if (!m_conversation_store->save(save_error)) {
        m_request_error = save_error;
        return;
    }
    m_request_error.clear();
    m_ui_feedback = "Conversation deleted.";
    ENGINE_LOG_INFO("[AI AUDIT] conversation_store=deleted thread={}", thread_id);
}

void AiAssistantPanel::copy_to_clipboard(
    const std::string& contents,
    const std::string& feedback
) {
    if (contents.empty()) return;
    ImGui::SetClipboardText(contents.c_str());
    std::lock_guard lock(m_request_mutex);
    m_ui_feedback = feedback;
    ENGINE_LOG_INFO("[AI AUDIT] ui_action=clipboard_copy result={}", feedback);
}

void AiAssistantPanel::toggle_authoring_mode() {
    m_authoring_mode = m_authoring_mode == AuthoringMode::AutonomousSafe
        ? AuthoringMode::ConfirmBeforeApply
        : AuthoringMode::AutonomousSafe;
    std::lock_guard lock(m_request_mutex);
    m_ui_feedback = m_authoring_mode == AuthoringMode::AutonomousSafe
        ? "Autonomous safe mode enabled. Validated proposals will apply automatically."
        : "Confirmation enabled. Proposals will wait for your approval.";
    ENGINE_LOG_INFO(
        "[AI AUDIT] ui_action=authoring_mode mode={}",
        m_authoring_mode == AuthoringMode::AutonomousSafe
            ? "autonomous_safe" : "confirm_before_apply"
    );
}

void AiAssistantPanel::set_collaboration_mode(EditorAiCollaborationMode mode) {
    std::lock_guard lock(m_request_mutex);
    if (m_request_running || m_pending_proposal || m_pending_user_input) return;
    if (mode == EditorAiCollaborationMode::Plan &&
        m_backend != ProviderBackend::CodexSubscription) {
        m_ui_feedback = "Plan mode requires the Codex subscription backend.";
        return;
    }
    m_collaboration_mode = mode;
    m_ui_feedback = mode == EditorAiCollaborationMode::Plan
        ? "Plan mode enabled. Codex can inspect and refine a plan without changing the project."
        : "Build mode enabled.";
    ENGINE_LOG_INFO(
        "[AI AUDIT] ui_action=collaboration_mode mode={}",
        mode == EditorAiCollaborationMode::Plan ? "plan" : "default"
    );
}

std::optional<EditorAiUserInputAnswers> AiAssistantPanel::request_user_input(
    const EditorAiUserInputRequest& request
) {
    auto pending = std::make_shared<PendingUserInput>(request);
    auto completion = pending->completion.get_future();
    {
        std::lock_guard lock(m_request_mutex);
        if (m_pending_user_input) return std::nullopt;
        m_pending_user_input = pending;
        m_live_activity = "Waiting for your answer";
        m_scroll_chat_to_bottom = true;
    }
    const auto started = std::chrono::steady_clock::now();
    while (completion.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
        if (m_cancel_requested.load()) {
            bool expected = false;
            if (pending->resolved.compare_exchange_strong(expected, true)) {
                pending->completion.set_value(std::nullopt);
            }
        } else if (request.auto_resolution_ms &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started
            ).count() >= static_cast<std::int64_t>(*request.auto_resolution_ms)) {
            bool expected = false;
            if (pending->resolved.compare_exchange_strong(expected, true)) {
                pending->completion.set_value(EditorAiUserInputAnswers{});
            }
        }
    }
    auto answer = completion.get();
    {
        std::lock_guard lock(m_request_mutex);
        if (m_pending_user_input == pending) m_pending_user_input.reset();
        if (m_request_running) m_live_activity = "Codex is working";
    }
    return answer;
}

void AiAssistantPanel::resolve_user_input(bool submit) {
    std::shared_ptr<PendingUserInput> pending;
    {
        std::lock_guard lock(m_request_mutex);
        pending = m_pending_user_input;
    }
    if (!pending) return;
    if (!submit) m_cancel_requested.store(true);
    std::optional<EditorAiUserInputAnswers> result;
    if (submit) {
        EditorAiUserInputAnswers answers;
        for (std::size_t index = 0; index < pending->request.questions.size(); ++index) {
            std::string value = pending->custom_selected[index]
                ? std::string(pending->custom_answers[index].data())
                : pending->answers[index];
            if (!has_visible_text(value.c_str())) {
                std::lock_guard lock(m_request_mutex);
                m_ui_feedback = "Answer every Codex question before continuing.";
                return;
            }
            answers.push_back({pending->request.questions[index].id, {std::move(value)}});
        }
        result = std::move(answers);
    }
    bool expected = false;
    if (pending->resolved.compare_exchange_strong(expected, true)) {
        pending->completion.set_value(std::move(result));
    }
}

void AiAssistantPanel::start_approved_plan_build() {
    std::string plan;
    {
        std::lock_guard lock(m_request_mutex);
        if (!m_plan_build_requested || m_request_running || m_pending_proposal ||
            m_pending_user_input) return;
        m_plan_build_requested = false;
        plan = !m_plan_text.empty() ? m_plan_text : m_approved_plan_text;
        if (plan.empty()) {
            m_request_error = "Codex requested Build before producing an authoritative plan.";
            return;
        }
        m_approved_plan_text = plan;
        m_collaboration_mode = EditorAiCollaborationMode::Default;
        m_messages.push_back({"editor", "Plan revision " + std::to_string(m_plan_revision) +
            " approved; Build started automatically."});
        m_ui_feedback = "Plan approved. Codex switched to Build automatically.";
        m_scroll_chat_to_bottom = true;
    }
    start_request(
        "Implement the approved plan below completely. Treat it as the authoritative scope, "
        "use the validated editor authoring tools for scene changes, verify the result, and do "
        "not silently expand the plan.\n\n" + plan,
        false,
        true
    );
}

void AiAssistantPanel::persist_current_conversation_locked(const std::string& model) {
    if (!m_conversation_store || m_codex_thread_id.empty()) return;
    const auto updated_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    m_conversation_store->record(EditorAiConversationRecord{
        .thread_id = m_codex_thread_id,
        .session_id = m_codex_session_id,
        .title = make_editor_ai_conversation_title(m_messages),
        .model = model,
        .tool_contract_version = EDITOR_AI_TOOL_CONTRACT_VERSION,
        .last_input_tokens = m_last_input_tokens,
        .last_output_tokens = m_last_output_tokens,
        .last_total_tokens = m_last_total_tokens,
        .updated_at_unix_ms = updated_at,
        .messages = m_messages,
        .collaboration_mode = m_collaboration_mode,
        .plan_revision = m_plan_revision,
        .plan_text = m_plan_text,
        .plan_explanation = m_plan_explanation,
        .plan = m_plan
    });
    if (!m_superseded_thread_id.empty() && m_superseded_thread_id != m_codex_thread_id) {
        const std::string previous_thread = m_superseded_thread_id;
        static_cast<void>(m_conversation_store->erase(previous_thread));
        m_superseded_thread_id.clear();
        ENGINE_LOG_INFO(
            "[AI AUDIT] conversation_store=contract_upgraded old_thread={} new_thread={} contract={}",
            previous_thread,
            m_codex_thread_id,
            EDITOR_AI_TOOL_CONTRACT_VERSION
        );
    }
    std::string save_error;
    if (!m_conversation_store->save(save_error)) {
        m_request_error = save_error;
        ENGINE_LOG_WARN(
            "[AI AUDIT] conversation_store=save_failed thread={} error={}",
            m_codex_thread_id,
            save_error
        );
    }
}

void AiAssistantPanel::dismiss_pending_proposal() {
    std::lock_guard lock(m_request_mutex);
    const bool dismisses_iteration = m_pending_proposal &&
        m_pending_proposal->iterative_continuation && m_iterative_authoring;
    if (m_pending_proposal) {
        ENGINE_LOG_INFO(
            "[AI AUDIT] request={} proposal=dismissed actions={} scene={} baseline_revision={}",
            m_pending_proposal->request_id,
            m_pending_proposal->actions.size(),
            m_pending_proposal->scene_name,
            m_pending_proposal->baseline_revision
        );
    }
    if (m_pending_proposal) {
        remove_cached_texture_previews(m_pending_proposal->generated_texture_previews);
    }
    m_pending_proposal.reset();
    m_generated_texture_preview.reset();
    m_loaded_texture_preview_id.clear();
    if (dismisses_iteration) {
        m_iterative_authoring->session.finish_cancelled(
            "The correction batch was dismissed; earlier applied batches were preserved."
        );
        m_iteration_continuation_requested = false;
        m_iteration_verification_pending = false;
        m_authoring_feedback = m_iterative_authoring->session.outcome();
        m_last_receipt_summary = m_authoring_feedback;
    } else {
        m_authoring_feedback = "AI proposal dismissed without changing the scene.";
        m_last_receipt_summary =
            "The previous editor proposal was dismissed; no changes were applied.";
    }
    m_messages.push_back({"editor", m_last_receipt_summary});
    m_scroll_chat_to_bottom = true;
    persist_current_conversation_locked(m_model.data());
}

void AiAssistantPanel::apply_pending_proposal() {
    PendingProposal proposal;
    {
        std::lock_guard lock(m_request_mutex);
        if (!m_pending_proposal || m_request_running) return;
        proposal = *m_pending_proposal;
    }

    const auto reject = [this, request_id = proposal.request_id,
                         iterative_continuation = proposal.iterative_continuation,
                         previews = proposal.generated_texture_previews](std::string message) {
        ENGINE_LOG_WARN("[AI AUDIT] request={} proposal=rejected reason={}", request_id, message);
        remove_cached_texture_previews(previews);
        std::lock_guard lock(m_request_mutex);
        m_pending_proposal.reset();
        m_generated_texture_preview.reset();
        m_loaded_texture_preview_id.clear();
        if (iterative_continuation && m_iterative_authoring) {
            m_iterative_authoring->session.finish_failed(
                "A correction batch was rejected: " + message +
                ". Earlier applied batches were preserved."
            );
            m_iteration_continuation_requested = false;
            m_authoring_feedback = m_iterative_authoring->session.outcome();
            m_last_receipt_summary = m_authoring_feedback;
        } else {
            m_authoring_feedback = "Proposal rejected: " + message;
            m_last_receipt_summary = "The previous editor proposal failed and was rolled back: " +
                message + ". No scene changes were kept.";
        }
        m_messages.push_back({"editor", m_last_receipt_summary});
        m_scroll_chat_to_bottom = true;
        persist_current_conversation_locked(m_model.data());
    };

    const std::shared_ptr<Scene> scene = proposal.target_scene.lock();
    const std::shared_ptr<Scene> active_scene = m_scene_manager
        ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    if (!scene || scene != active_scene) {
        reject("the active scene changed after the proposal was generated");
        return;
    }
    if (!m_scene_manager->is_editing()) {
        reject("authoring is available only in Edit mode");
        return;
    }
    if (!proposal.command_history || proposal.command_history != m_command_history ||
        !proposal.entity_state_store || proposal.entity_state_store != m_entity_state_store ||
        proposal.selection != m_selection) {
        reject("the editor session changed after the proposal was generated");
        return;
    }
    if (proposal.iterative_continuation) {
        bool session_valid = false;
        {
            std::lock_guard lock(m_request_mutex);
            session_valid = m_iterative_authoring &&
                m_iterative_authoring->session.active();
        }
        if (!session_valid) {
            reject("the iterative authoring session is no longer valid");
            return;
        }
    }
    std::string error;
    std::string description;
    if (!m_authoring_registry.execute_proposal(
        proposal.actions,
        EditorAiAuthoringContext{
            .entities = &scene->get_entity_manager(),
            .selection = proposal.selection,
            .entity_state_store = proposal.entity_state_store,
            .component_registry = &default_component_registry(),
            .serialization = m_serialization_context
        },
        *proposal.command_history,
        proposal.baseline_revision,
        description,
        error
    )) {
        reject(error.empty() ? "the proposal could not be applied" : error);
        return;
    }

    const nlohmann::json after_snapshot = m_tool_registry.build_context_snapshot(
        scene.get(), proposal.selection.get(), m_serialization_context
    );
    nlohmann::json receipt = build_application_receipt(
        proposal.request_id,
        proposal.baseline_revision,
        proposal.command_history->get_current_revision(),
        proposal.actions,
        proposal.before_entity_index,
        after_snapshot.value("entity_index", nlohmann::json::array()),
        proposal.before_asset_index,
        after_snapshot.value("asset_index", nlohmann::json::array())
    );
    const std::string summary = receipt_summary(receipt);
    remove_cached_texture_previews(proposal.generated_texture_previews);

    std::lock_guard lock(m_request_mutex);
    if (!proposal.iterative_continuation) {
        m_iteration_verification_pending = false;
        IterativeAuthoringBinding binding;
        binding.session.begin(
            proposal.request_id,
            proposal.original_prompt,
            proposal.session_revision_before,
            std::chrono::minutes{m_authoring_work_budget_minutes}
        );
        binding.target_scene = scene;
        binding.command_history = proposal.command_history;
        binding.entity_state_store = proposal.entity_state_store;
        binding.selection = proposal.selection;
        m_iterative_authoring = std::move(binding);
    }
    std::string session_error;
    if (!m_iterative_authoring ||
        !m_iterative_authoring->session.record_applied_batch(
            proposal.actions.size(), session_error
        )) {
        m_pending_proposal.reset();
        m_authoring_feedback = "Applied batch could not join its iterative session: " + session_error;
        m_request_error = m_authoring_feedback;
        return;
    }
    const std::string session_description =
        m_iterative_authoring->session.history_description();
    if (!proposal.command_history->squash_commands_after_revision(
        m_iterative_authoring->session.revision_before(), session_description
    )) {
        m_iterative_authoring->session.finish_failed(
            "Applied batches could not be consolidated into one Undo transaction."
        );
        m_authoring_feedback = m_iterative_authoring->session.outcome();
        m_request_error = m_authoring_feedback;
        m_pending_proposal.reset();
        return;
    }
    const bool should_continue = m_iterative_verification_enabled &&
        m_iterative_authoring->session.can_continue();
    if (!should_continue && m_iterative_authoring->session.active()) {
        m_iterative_authoring->session.finish_validated_atomically();
    }
    m_iteration_continuation_requested = should_continue;
    receipt["session_status"] = should_continue ? "inspecting" : "validated_atomically";
    receipt["session_batches"] = m_iterative_authoring->session.applied_batches();
    receipt["session_actions"] = m_iterative_authoring->session.total_actions();
    receipt["session_work_time_ms"] = m_iterative_authoring->session.work_time().count();
    receipt["session_work_budget_ms"] = m_iterative_authoring->session.work_budget().count();
    m_pending_proposal.reset();
    m_generated_texture_preview.reset();
    m_loaded_texture_preview_id.clear();
    m_last_application_receipt = receipt;
    m_last_receipt_summary = summary;
    m_authoring_feedback = should_continue
        ? session_description + " applied. Inspecting the current result..."
        : session_description + " applied and atomically data-validated. Visual iteration was not run.";
    m_messages.push_back({"editor", "Applied successfully. " + summary});
    m_scroll_chat_to_bottom = true;
    persist_current_conversation_locked(m_model.data());
    ENGINE_LOG_INFO(
        "[AI AUDIT] request={} proposal=applied actions={} revision={} description={}",
        proposal.request_id,
        proposal.actions.size(),
        proposal.command_history->get_current_revision(),
        session_description
    );
    ENGINE_LOG_INFO("[AI AUDIT] receipt={}", receipt.dump());
    m_last_applied_session = AppliedAiSession{
        .request_id = m_iterative_authoring->session.root_request_id(),
        .target_scene = scene,
        .command_history = proposal.command_history,
        .after_revision = proposal.command_history->get_current_revision(),
        .description = session_description
    };
}

void AiAssistantPanel::undo_last_ai_session() {
    AppliedAiSession session;
    {
        std::lock_guard lock(m_request_mutex);
        if (!m_last_applied_session) return;
        session = *m_last_applied_session;
    }
    const auto scene = session.target_scene.lock();
    const auto history = session.command_history.lock();
    const auto active_scene = m_scene_manager ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    if (!scene || scene != active_scene || !history || history != m_command_history ||
        history->get_current_revision() != session.after_revision ||
        history->get_undo_description() != session.description) {
        std::lock_guard lock(m_request_mutex);
        m_authoring_feedback = "AI session is no longer the latest scene edit; use the regular Undo history.";
        m_last_applied_session.reset();
        m_iterative_authoring.reset();
        m_iteration_continuation_requested = false;
        m_iteration_verification_pending = false;
        return;
    }
    history->undo();
    std::lock_guard lock(m_request_mutex);
    if (history->get_current_revision() != session.after_revision) {
        ENGINE_LOG_INFO(
            "[AI AUDIT] request={} proposal=undone description={}",
            session.request_id,
            session.description
        );
        m_authoring_feedback = session.description + " undone.";
        m_last_receipt_summary = "The previous AI editor session was undone; resolve the current scene again.";
        if (!m_last_application_receipt.empty()) {
            m_last_application_receipt["status"] = "undone";
            m_last_application_receipt["undo_revision"] = history->get_current_revision();
        }
        m_last_applied_session.reset();
    } else {
        m_authoring_feedback = "The AI session could not be undone; scene state was preserved.";
    }
}

void AiAssistantPanel::continue_iterative_authoring() {
    {
        std::lock_guard lock(m_request_mutex);
        if (!m_iteration_continuation_requested || m_request_running || m_pending_proposal ||
            !m_iterative_authoring || !m_iterative_authoring->session.can_continue()) {
            return;
        }
        m_iteration_continuation_requested = false;
    }
    start_request({}, true);
}

void AiAssistantPanel::finalize_iterative_verification() {
    std::lock_guard lock(m_request_mutex);
    if (!m_iteration_verification_pending || m_request_running ||
        !m_iterative_authoring || !m_iterative_authoring->session.active()) {
        return;
    }
    m_iteration_verification_pending = false;

    const auto scene = m_iterative_authoring->target_scene.lock();
    const auto history = m_iterative_authoring->command_history.lock();
    const auto active_scene = m_scene_manager
        ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    const bool current_state_matches = scene && scene == active_scene && history &&
        history == m_command_history &&
        history->get_current_revision() == m_iteration_verification_revision;
    if (m_iteration_verification_inspected && current_state_matches) {
        if (m_iteration_verification_time_budget_reached) {
            m_iterative_authoring->session.finish_time_budget();
            m_authoring_feedback =
                "Iterative authoring completed its final inspection after using "
                "the configured AI work time.";
        } else {
            m_iterative_authoring->session.finish_verified();
            m_authoring_feedback =
                "Iterative authoring verified after " +
                std::to_string(m_iterative_authoring->session.applied_batches()) +
                " applied batch(es).";
        }
        if (!m_last_application_receipt.empty()) {
            m_last_application_receipt["session_status"] =
                m_iteration_verification_time_budget_reached
                    ? "time_budget_reached" : "verified";
            m_last_application_receipt["session_batches"] =
                m_iterative_authoring->session.applied_batches();
            m_last_application_receipt["session_actions"] =
                m_iterative_authoring->session.total_actions();
            m_last_application_receipt["session_work_time_ms"] =
                m_iterative_authoring->session.work_time().count();
        }
    } else {
        const std::string reason = !m_iteration_verification_inspected
            ? "Codex ended the verification pass without inspecting the applied scene."
            : "The scene changed while Codex was verifying the applied result.";
        m_iterative_authoring->session.finish_failed(reason);
        m_authoring_feedback = reason;
        m_request_error = reason;
    }
    m_last_receipt_summary = m_authoring_feedback;
    m_messages.push_back({"editor", m_authoring_feedback});
    m_scroll_chat_to_bottom = true;
    persist_current_conversation_locked(m_model.data());
}

void AiAssistantPanel::start_request(
    std::string internal_prompt,
    bool iterative_continuation,
    bool approved_plan_build
) {
    const bool using_codex = m_backend == ProviderBackend::CodexSubscription;
    IEditorAiProvider* provider = using_codex
        ? static_cast<IEditorAiProvider*>(m_codex_provider.get())
        : static_cast<IEditorAiProvider*>(m_openai_provider.get());
    if (!provider || !provider->is_available()) return;
    const char* api_key_value = std::getenv("OPENAI_API_KEY");
    if (m_backend == ProviderBackend::OpenAiApi && (!api_key_value || api_key_value[0] == '\0')) {
        std::lock_guard lock(m_request_mutex);
        m_request_error = "Set OPENAI_API_KEY before launching the editor.";
        return;
    }
    if (m_backend == ProviderBackend::CodexSubscription && m_codex_executable.empty()) {
        std::lock_guard lock(m_request_mutex);
        m_request_error = "Codex CLI was not found. Install Codex or set CODEX_EXECUTABLE.";
        return;
    }

    std::string prompt = !internal_prompt.empty()
        ? std::move(internal_prompt)
        : std::string(m_prompt.data());
    std::string original_prompt = prompt;
    std::size_t applied_batches = 0;
    std::uint64_t session_work_time_ms = 0;
    std::uint64_t session_work_budget_ms = 0;
    bool final_verification_only = false;
    std::uint64_t session_revision_before = 0;
    if (iterative_continuation) {
        std::lock_guard lock(m_request_mutex);
        if (!m_iterative_authoring || !m_iterative_authoring->session.can_continue()) {
            m_iteration_continuation_requested = false;
            return;
        }
        prompt = m_iterative_authoring->session.continuation_prompt();
        original_prompt = m_iterative_authoring->session.original_prompt();
        applied_batches = m_iterative_authoring->session.applied_batches();
        session_work_time_ms = static_cast<std::uint64_t>(
            m_iterative_authoring->session.work_time().count()
        );
        session_work_budget_ms = static_cast<std::uint64_t>(
            m_iterative_authoring->session.work_budget().count()
        );
        final_verification_only =
            m_iterative_authoring->session.final_verification_only();
        session_revision_before = m_iterative_authoring->session.revision_before();
    }
    const std::string model(m_model.data());
    if (!has_visible_text(prompt.c_str()) && m_attachments.has_screenshots()) {
        prompt = "Inspect the attached screenshot(s).";
    }
    if (!has_visible_text(prompt.c_str()) || !has_visible_text(model.c_str())) return;
    const std::uint64_t request_id = NEXT_AI_REQUEST_ID.fetch_add(1);
    const auto request_started = std::chrono::steady_clock::now();
    const std::string reasoning_effort = m_reasoning_effort == ReasoningEffort::High
        ? "high"
        : (m_reasoning_effort == ReasoningEffort::Medium ? "medium" : "low");

    const bool planning = !iterative_continuation && !approved_plan_build &&
        m_collaboration_mode == EditorAiCollaborationMode::Plan;
    bool image_generation_requested = false;
    {
        std::lock_guard lock(m_request_mutex);
        if (m_pending_proposal) {
            m_request_error = "Apply or dismiss the pending AI proposal before sending another request.";
            return;
        }
        image_generation_requested = !planning && !iterative_continuation && using_codex &&
            !m_imagegen_skill_path.empty() &&
            (m_image_generation_requested || prompt_requests_generated_image(prompt) ||
             (m_conversation_has_generated_image && prompt_refines_generated_image(prompt)));
        m_image_generation_requested = false;
    }

    const std::shared_ptr<Scene> active_scene = m_scene_manager
        ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    const std::shared_ptr<EditorCommandHistory> command_history = m_command_history;
    const std::shared_ptr<EditorEntityStateStore> entity_state_store = m_entity_state_store;
    const std::shared_ptr<EditorSelection> selection = m_selection;
    const bool editor_authoring_available = active_scene && command_history &&
        entity_state_store && selection &&
        m_scene_manager && m_scene_manager->is_editing();
    const bool authoring_available = editor_authoring_available && !planning &&
        !final_verification_only;
    const bool viewport_available = active_scene && !m_viewport_panel.expired() && m_scene_manager &&
        m_scene_manager->is_editing();
    const std::uint64_t baseline_revision = command_history
        ? command_history->get_current_revision() : 0;
    const AuthoringMode authoring_mode = m_authoring_mode;
    if (iterative_continuation) {
        std::lock_guard lock(m_request_mutex);
        const auto session_scene = m_iterative_authoring
            ? m_iterative_authoring->target_scene.lock() : nullptr;
        const auto session_history = m_iterative_authoring
            ? m_iterative_authoring->command_history.lock() : nullptr;
        const auto session_state_store = m_iterative_authoring
            ? m_iterative_authoring->entity_state_store.lock() : nullptr;
        const auto session_selection = m_iterative_authoring
            ? m_iterative_authoring->selection.lock() : nullptr;
        const bool history_is_latest = m_last_applied_session &&
            m_last_applied_session->command_history.lock() == command_history &&
            m_last_applied_session->after_revision == command_history->get_current_revision() &&
            m_last_applied_session->description == command_history->get_undo_description();
        if (!editor_authoring_available || session_scene != active_scene ||
            session_history != command_history || session_state_store != entity_state_store ||
            session_selection != selection || !history_is_latest) {
            m_iteration_continuation_requested = false;
            m_iterative_authoring->session.finish_failed(
                "The editor session changed before iterative verification."
            );
            m_authoring_feedback = m_iterative_authoring->session.outcome();
            return;
        }
    } else if (command_history) {
        session_revision_before = command_history->get_current_revision();
    }
    auto proposal_actions = std::make_shared<std::vector<EditorAiAuthoringAction>>();
    auto request_context = std::make_shared<nlohmann::json>(
        m_tool_registry.build_context_snapshot(active_scene.get(), selection.get(), m_serialization_context)
    );
    nlohmann::json attached_contexts = m_attachments.build_context(
        *request_context, m_serialization_context.asset_database
    );
    (*request_context)["attached_contexts"] = attached_contexts;
    (*request_context)["generated_texture_previews"] = nlohmann::json::object();
    (*request_context)["viewport"] = viewport_state_json();
    const nlohmann::json before_entity_index = request_context->value(
        "entity_index", nlohmann::json::array()
    );
    const nlohmann::json before_asset_index = request_context->value(
        "asset_index", nlohmann::json::array()
    );
    std::string previous_receipt;
    {
        std::lock_guard lock(m_request_mutex);
        previous_receipt = m_last_receipt_summary;
    }

    EditorAiProviderRequest request;
    request.api_key = api_key_value ? api_key_value : "";
    request.model = model;
    request.prompt = prompt;
    request.reasoning_effort = reasoning_effort;
    std::vector<std::filesystem::path> screenshot_cleanup_paths;
    request.images = m_attachments.build_images(using_codex, screenshot_cleanup_paths);
    if (image_generation_requested) {
        request.skills.push_back({"imagegen", m_imagegen_skill_path});
    }
    request.tools = m_tool_registry.tool_schemas();
    if (viewport_available) {
        for (const auto& schema : m_viewport_tools.tool_schemas()) {
            request.tools.push_back(schema);
        }
    }
    // App Server binds dynamic tools when the thread is created. Keep authoring schemas
    // available on a read-only Plan turn so the same thread can transition to Build.
    if (editor_authoring_available) {
        for (const auto& schema : m_authoring_registry.tool_schemas()) {
            request.tools.push_back(schema);
        }
    }
    if (using_codex) request.tools.push_back(begin_plan_implementation_schema());
    request.instructions = build_editor_ai_request_instructions({
        .image_generation_requested = image_generation_requested,
        .authoring_available = authoring_available,
        .confirm_before_apply = authoring_mode == AuthoringMode::ConfirmBeforeApply,
        .viewport_available = viewport_available,
        .has_images = !request.images.empty(),
        .iterative_continuation = iterative_continuation,
        .final_verification_only = final_verification_only,
        .planning = planning,
        .has_existing_plan = !m_plan_text.empty(),
        .applied_batches = applied_batches,
        .work_time_ms = session_work_time_ms,
        .work_budget_ms = session_work_budget_ms,
        .previous_receipt = previous_receipt,
        .attached_contexts = attached_contexts,
        .project_overview = request_context->value(
            "project_overview", nlohmann::json::object()
        ),
        .existing_plan = m_plan_text
    });
    request.project_root = project_root();
    request.codex_executable = m_codex_executable;
    request.persist_thread = using_codex;
    request.max_tool_rounds = 12;
    request.max_tool_failures = 3;
    request.collaboration_mode = planning
        ? EditorAiCollaborationMode::Plan : EditorAiCollaborationMode::Default;
    if (using_codex) {
        request.request_user_input = [this](const EditorAiUserInputRequest& input) {
            return request_user_input(input);
        };
    }
    std::string compaction_source = "none";
    std::size_t previous_input_tokens = 0;
    {
        std::lock_guard lock(m_request_mutex);
        request.provider_thread_id = using_codex ? m_codex_thread_id : "";
        request.history = request.provider_thread_id.empty()
            ? recent_history(m_messages)
            : std::vector<EditorAiChatMessage>{};
        previous_input_tokens = m_last_input_tokens;
        const bool automatic_compaction = previous_input_tokens >=
            AUTO_COMPACT_INPUT_TOKEN_THRESHOLD;
        request.compact_thread_before_turn = using_codex &&
            !request.provider_thread_id.empty() &&
            (m_compact_before_next_request || automatic_compaction);
        if (request.compact_thread_before_turn) {
            compaction_source = m_compact_before_next_request ? "manual" : "automatic";
            m_compact_before_next_request = false;
        }
        if (!iterative_continuation && !approved_plan_build) {
            m_messages.push_back({"user", prompt});
        }
        m_streamed_response.clear();
        m_reasoning_summary.clear();
        m_request_error.clear();
        m_active_tool.clear();
        m_live_activity = "Connecting to Codex";
        m_activity_entries.clear();
        m_request_started_at = request_started;
        m_authoring_feedback.clear();
        m_ui_feedback.clear();
        m_last_tool_calls = 0;
        m_last_request_duration_ms = 0;
        m_request_running = true;
        m_scroll_chat_to_bottom = true;
    }
    const std::size_t history_characters = std::accumulate(
        request.history.begin(),
        request.history.end(),
        std::size_t{0},
        [](std::size_t total, const EditorAiChatMessage& message) {
            return total + message.content.size();
        }
    );
    ENGINE_LOG_INFO(
        "[AI AUDIT] request={} phase=start backend={} model={} effort={} mode={} collaboration={} scene={} "
        "baseline_revision={} history_messages={} history_chars={} context_chars={} tools={} "
        "tool_schema_chars={} instructions_chars={} prompt_chars={} images={} thread={} persistent={} "
        "compact_before_turn={} compact_source={} previous_input_tokens={}",
        request_id,
        m_backend == ProviderBackend::CodexSubscription ? "codex_subscription" : "openai_api",
        model,
        reasoning_effort,
        authoring_mode == AuthoringMode::AutonomousSafe ? "autonomous_safe" : "confirm_before_apply",
        planning ? "plan" : "default",
        active_scene ? active_scene->get_name() : "none",
        baseline_revision,
        request.history.size(),
        history_characters,
        request_context->dump().size(),
        request.tools.size(),
        request.tools.dump().size(),
        request.instructions.size(),
        prompt.size(),
        request.images.size(),
        request.provider_thread_id.empty() ? "new" : request.provider_thread_id,
        request.persist_thread,
        request.compact_thread_before_turn,
        compaction_source,
        previous_input_tokens
    );
    auto inspection_tool_count = std::make_shared<std::size_t>(0);
    auto build_transition_requested = std::make_shared<bool>(false);
    request.execute_tool = [
        this,
        request_context,
        proposal_actions,
        inspection_tool_count,
        build_transition_requested,
        planning,
        authoring_available,
        viewport_available,
        active_scene,
        baseline_revision,
        viewport_capture_count = std::make_shared<std::size_t>(0),
        request_id,
        request_started
    ](
        const std::string& name,
        const nlohmann::json& arguments
    ) -> EditorAiToolResult {
        const auto tool_started = std::chrono::steady_clock::now();
        const auto audited = [&](EditorAiToolResult result) {
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - tool_started
            ).count();
            ENGINE_LOG_INFO(
                "[AI AUDIT] request={} phase=tool name={} success={} elapsed_ms={} duration_ms={} "
                "queued_actions={} error={}",
                request_id,
                name,
                result.success,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - request_started
                ).count(),
                duration,
                proposal_actions->size(),
                result.success ? "none" : result.error
            );
            return result;
        };
        if (name == "begin_plan_implementation") {
            if (!planning) {
                return audited({false, {}, "Build transition is available only in Plan mode"});
            }
            const std::string reason = arguments.value("reason", "");
            if (!has_visible_text(reason.c_str())) {
                return audited({false, {}, "Build transition requires explicit authorization evidence"});
            }
            *build_transition_requested = true;
            return audited({true, {{"accepted", true}, {"transition", "plan_to_build"}}, {}});
        }
        if (planning && name == "run_project_command") {
            return audited({false, {}, "Project commands are unavailable in read-only Plan mode"});
        }
        if (m_authoring_registry.is_authoring_tool(name)) {
            if (!authoring_available) {
                return audited({false, {}, "Authoring is unavailable for this request"});
            }
            return audited(m_authoring_registry.queue_action(
                name, arguments, *proposal_actions, request_context.get()
            ));
        }
        if (m_viewport_tools.is_tool(name)) {
            if (!viewport_available) {
                return audited({false, {}, "Viewport inspection is unavailable for this request"});
            }
            if (name == "capture_viewport" && ++*viewport_capture_count > 4) {
                return audited({false, {}, "Codex exceeded the four-capture viewport limit"});
            }
            EditorAiToolResult result = execute_live_editor_tool(
                name, arguments, active_scene, baseline_revision
            );
            if (result.success) ++*inspection_tool_count;
            return audited(std::move(result));
        }
        EditorAiToolResult result = m_tool_registry.execute_snapshot(
            name, arguments, *request_context
        );
        if (result.success) ++*inspection_tool_count;
        return audited(std::move(result));
    };
    m_prompt.fill('\0');
    m_attachments.clear_contexts();
    m_attachments.clear_screenshots(false);
    if (m_request_thread.joinable()) m_request_thread.join();
    m_cancel_requested.store(false);

    m_request_thread = std::thread([
        this,
        provider,
        request = std::move(request),
        proposal_actions,
        active_scene,
        command_history,
        entity_state_store,
        selection,
        baseline_revision,
        before_entity_index,
        before_asset_index,
        request_context,
        inspection_tool_count,
        build_transition_requested,
        authoring_mode,
        iterative_continuation,
        approved_plan_build,
        planning,
        final_verification_only,
        original_prompt,
        session_revision_before,
        using_codex,
        model,
        request_id,
        request_started,
        screenshot_cleanup_paths = std::move(screenshot_cleanup_paths)
    ]() mutable {
        bool first_text_logged = false;
        bool first_reasoning_summary_logged = false;
        auto last_event_at = request_started;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> item_started_at;
        std::unordered_map<std::string, std::size_t> reasoning_summary_characters;
        const EditorAiProviderResult result = provider->run(
            request,
            [
                this,
                request_id,
                request_started,
                request_context,
                &first_text_logged,
                &first_reasoning_summary_logged,
                &last_event_at,
                &item_started_at,
                &reasoning_summary_characters
            ](const EditorAiProviderEvent& event) {
                const auto event_at = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    event_at - request_started
                ).count();
                const auto gap = std::chrono::duration_cast<std::chrono::milliseconds>(
                    event_at - last_event_at
                ).count();
                last_event_at = event_at;
                if (event.type == EditorAiProviderEventType::Started) {
                    ENGINE_LOG_INFO(
                        "[AI AUDIT] request={} phase=provider_started elapsed_ms={} gap_ms={}",
                        request_id,
                        elapsed,
                        gap
                    );
                } else if (event.type == EditorAiProviderEventType::TextDelta && !first_text_logged) {
                    first_text_logged = true;
                    ENGINE_LOG_INFO(
                        "[AI AUDIT] request={} phase=first_text elapsed_ms={} gap_ms={}",
                        request_id,
                        elapsed,
                        gap
                    );
                } else if (event.type == EditorAiProviderEventType::ReasoningSummaryDelta) {
                    reasoning_summary_characters[event.item_id] += event.text.size();
                    if (!first_reasoning_summary_logged && !event.text.empty()) {
                        first_reasoning_summary_logged = true;
                        ENGINE_LOG_INFO(
                            "[AI AUDIT] request={} phase=reasoning_summary_started item={} "
                            "elapsed_ms={} gap_ms={}",
                            request_id,
                            event.item_id.empty() ? "unknown" : event.item_id,
                            elapsed,
                            gap
                        );
                    }
                } else if (event.type == EditorAiProviderEventType::ItemStarted) {
                    if (!event.item_id.empty()) item_started_at[event.item_id] = event_at;
                    ENGINE_LOG_INFO(
                        "[AI AUDIT] request={} phase=item_started type={} item={} elapsed_ms={} gap_ms={}",
                        request_id,
                        event.item_type,
                        event.item_id.empty() ? "unknown" : event.item_id,
                        elapsed,
                        gap
                    );
                } else if (event.type == EditorAiProviderEventType::ItemCompleted) {
                    std::int64_t item_duration = 0;
                    const auto started = item_started_at.find(event.item_id);
                    if (started != item_started_at.end()) {
                        item_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                            event_at - started->second
                        ).count();
                        item_started_at.erase(started);
                    }
                    const auto summary_characters = reasoning_summary_characters.find(event.item_id);
                    const std::size_t summary_size = summary_characters ==
                            reasoning_summary_characters.end()
                        ? 0
                        : summary_characters->second;
                    if (summary_characters != reasoning_summary_characters.end()) {
                        reasoning_summary_characters.erase(summary_characters);
                    }
                    ENGINE_LOG_INFO(
                        "[AI AUDIT] request={} phase=item_completed type={} item={} status={} "
                        "elapsed_ms={} duration_ms={} gap_ms={} summary_chars={}",
                        request_id,
                        event.item_type,
                        event.item_id.empty() ? "unknown" : event.item_id,
                        event.item_status.empty() ? "unknown" : event.item_status,
                        elapsed,
                        item_duration,
                        gap,
                        event.item_type == "reasoning" ? summary_size : 0
                    );
                } else if (event.type == EditorAiProviderEventType::ToolStarted) {
                    ENGINE_LOG_INFO(
                        "[AI AUDIT] request={} phase=tool_started name={} elapsed_ms={} gap_ms={}",
                        request_id,
                        event.tool_name,
                        elapsed,
                        gap
                    );
                } else if (event.type == EditorAiProviderEventType::ImageGenerated) {
                    std::string cache_error;
                    auto preview = cache_generated_png(
                        request_id,
                        event,
                        request_context->at("generated_texture_previews").size(),
                        cache_error
                    );
                    if (preview) {
                        std::string artifact_id = event.artifact_id;
                        if (artifact_id.empty()) {
                            artifact_id = preview->at("preview_id").get<std::string>();
                        }
                        (*request_context)["generated_texture_previews"][artifact_id] = *preview;
                        ENGINE_LOG_INFO(
                            "[AI AUDIT] request={} phase=image_generated artifact={} bytes={} elapsed_ms={}",
                            request_id,
                            artifact_id,
                            preview->value("byte_count", std::size_t{0}),
                            elapsed
                        );
                        std::lock_guard image_lock(m_request_mutex);
                        m_conversation_has_generated_image = true;
                    } else {
                        ENGINE_LOG_WARN(
                            "[AI AUDIT] request={} phase=image_rejected reason={} elapsed_ms={}",
                            request_id,
                            cache_error,
                            elapsed
                        );
                    }
                }
                std::lock_guard lock(m_request_mutex);
                const auto upsert_activity = [this, elapsed](
                    const EditorAiProviderEvent& item_event
                ) {
                    if (item_event.item_type == "agentMessage" ||
                        item_event.item_type == "plan") {
                        return;
                    }
                    constexpr std::size_t MAX_ACTIVITY_ENTRIES = 40;
                    const std::string key = item_event.item_type == "reasoning"
                        ? "reasoning"
                        : (!item_event.item_id.empty()
                            ? item_event.item_id
                            : item_event.item_type + ":" + item_event.tool_name);
                    const EditorAiActivityPresentation presentation =
                        format_editor_ai_activity(item_event);
                    auto entry = std::find_if(
                        m_activity_entries.begin(),
                        m_activity_entries.end(),
                        [&key](const AiActivityEntry& candidate) {
                            return candidate.item_id == key;
                        }
                    );
                    if (entry == m_activity_entries.end()) {
                        if (m_activity_entries.size() >= MAX_ACTIVITY_ENTRIES) {
                            m_activity_entries.erase(m_activity_entries.begin());
                        }
                        m_activity_entries.push_back({
                            presentation.summary,
                            {},
                            static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed)),
                            key,
                            item_event.item_type,
                            item_event.item_status.empty() ? "inProgress" : item_event.item_status
                        });
                        entry = std::prev(m_activity_entries.end());
                    }
                    entry->label = presentation.summary;
                    entry->item_type = item_event.item_type;
                    if (!item_event.item_status.empty()) entry->status = item_event.item_status;
                    std::string detail = presentation.detail;
                    constexpr std::size_t MAX_ACTIVITY_DETAIL = 64 * 1024;
                    if (detail.size() > MAX_ACTIVITY_DETAIL) {
                        detail.resize(MAX_ACTIVITY_DETAIL);
                        detail += "\n\n[Detail truncated at 64 KiB]";
                    }
                    if (!detail.empty()) entry->detail = detail;
                    if (entry->status != "inProgress") {
                        if (item_event.details.contains("durationMs") &&
                            item_event.details.at("durationMs").is_number_integer()) {
                            entry->duration_ms = item_event.details.at("durationMs")
                                .get<std::uint64_t>();
                        } else {
                            const std::uint64_t completed_at = static_cast<std::uint64_t>(
                                std::max<std::int64_t>(0, elapsed)
                            );
                            entry->duration_ms = completed_at >= entry->elapsed_ms
                                ? completed_at - entry->elapsed_ms : 0;
                        }
                    }
                };
                if (event.type == EditorAiProviderEventType::Started) {
                    m_live_activity = "Codex started";
                } else if (event.type == EditorAiProviderEventType::TextDelta) {
                    m_streamed_response += event.text;
                    m_live_activity = "Writing response";
                    m_scroll_chat_to_bottom = true;
                } else if (event.type == EditorAiProviderEventType::ReasoningSummaryDelta) {
                    m_reasoning_summary += event.text;
                    m_live_activity = "Reasoning";
                    upsert_activity(event);
                    m_scroll_chat_to_bottom = true;
                } else if (event.type == EditorAiProviderEventType::ItemStarted) {
                    if (event.item_type == "dynamicToolCall") {
                        const std::string tool = event.details.is_object() &&
                                event.details.contains("tool") &&
                                event.details.at("tool").is_string()
                            ? event.details.at("tool").get<std::string>()
                            : "editor tool";
                        m_live_activity = "Waiting for Codex to call " + tool;
                    } else {
                        m_live_activity = activity_item_label(event.item_type);
                    }
                    upsert_activity(event);
                    m_scroll_chat_to_bottom = true;
                } else if (event.type == EditorAiProviderEventType::ItemCompleted) {
                    upsert_activity(event);
                    m_live_activity = "Codex is working";
                } else if (event.type == EditorAiProviderEventType::ItemOutputDelta) {
                    const auto entry = std::find_if(
                        m_activity_entries.begin(),
                        m_activity_entries.end(),
                        [&event](const AiActivityEntry& candidate) {
                            return candidate.item_id == event.item_id;
                        }
                    );
                    if (entry != m_activity_entries.end()) {
                        constexpr std::size_t MAX_LIVE_OUTPUT = 64 * 1024;
                        if (entry->output.size() < MAX_LIVE_OUTPUT) {
                            entry->output += event.text.substr(
                                0, MAX_LIVE_OUTPUT - entry->output.size()
                            );
                        }
                    }
                } else if (event.type == EditorAiProviderEventType::ToolStarted) {
                    m_active_tool = event.tool_name;
                    m_live_activity = "Executing " + event.tool_name;
                    upsert_activity(event);
                } else if (event.type == EditorAiProviderEventType::ToolFinished) {
                    m_active_tool.clear();
                    m_live_activity = "Codex is working";
                    upsert_activity(event);
                } else if (event.type == EditorAiProviderEventType::PlanDelta) {
                    m_streamed_response += event.text;
                    m_live_activity = "Drafting plan";
                    m_scroll_chat_to_bottom = true;
                } else if (event.type == EditorAiProviderEventType::PlanUpdated) {
                    m_plan = event.plan;
                    m_plan_explanation = event.explanation;
                    m_live_activity = "Updating plan";
                    m_scroll_chat_to_bottom = true;
                } else if (event.type == EditorAiProviderEventType::PlanCompleted) {
                    m_streamed_response = event.text;
                    m_live_activity = "Plan ready";
                    m_scroll_chat_to_bottom = true;
                }
            },
            [this]() { return m_cancel_requested.load(); }
        );

        for (const std::filesystem::path& screenshot : screenshot_cleanup_paths) {
            static_cast<void>(FileSystem::remove(screenshot.string()));
        }

        std::lock_guard lock(m_request_mutex);
        m_request_running = false;
        m_active_tool.clear();
        if (result.total_tokens > 0) {
            m_last_input_tokens = result.input_tokens;
            m_last_output_tokens = result.output_tokens;
            m_last_total_tokens = result.total_tokens;
        }
        if (request.compact_thread_before_turn && !result.context_compacted) {
            m_compact_before_next_request = true;
        }
        m_last_tool_calls = result.tool_call_count;
        if (!result.provider_thread_id.empty()) {
            m_codex_thread_id = result.provider_thread_id;
        }
        if (!result.provider_session_id.empty()) {
            m_codex_session_id = result.provider_session_id;
        }
        m_scroll_chat_to_bottom = true;
        m_last_request_duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - request_started
            ).count()
        );
        if (iterative_continuation && m_iterative_authoring) {
            m_iterative_authoring->session.record_work_time(
                std::chrono::milliseconds{m_last_request_duration_ms}
            );
        }
        const char* status = result.status == EditorAiProviderStatus::Completed
            ? "completed"
            : (result.status == EditorAiProviderStatus::Cancelled ? "cancelled" : "failed");
        m_live_activity = result.status == EditorAiProviderStatus::Completed
            ? "Completed"
            : (result.status == EditorAiProviderStatus::Cancelled ? "Cancelled" : "Failed");
        if (result.status != EditorAiProviderStatus::Completed) {
            for (AiActivityEntry& entry : m_activity_entries) {
                if (entry.status != "inProgress") continue;
                entry.status = result.status == EditorAiProviderStatus::Cancelled
                    ? "cancelled" : "failed";
                const std::uint64_t completed_at = m_last_request_duration_ms;
                entry.duration_ms = completed_at >= entry.elapsed_ms
                    ? completed_at - entry.elapsed_ms : 0;
                if (!result.error.empty()) {
                    if (!entry.detail.empty()) entry.detail += "\n\n";
                    entry.detail += "Turn stopped\n" + result.error;
                }
            }
        }
        if (result.context_compacted) {
            m_ui_feedback = "Codex context was compacted before this message.";
        }
        ENGINE_LOG_INFO(
            "[AI AUDIT] request={} phase=finish status={} duration_ms={} tool_calls={} "
            "proposal_actions={} input_tokens={} output_tokens={} total_tokens={} thread={} "
            "session={} context_compacted={} error={}",
            request_id,
            status,
            m_last_request_duration_ms,
            result.tool_call_count,
            proposal_actions->size(),
            result.input_tokens,
            result.output_tokens,
            result.total_tokens,
            result.provider_thread_id.empty() ? "none" : result.provider_thread_id,
            result.provider_session_id.empty() ? "none" : result.provider_session_id,
            result.context_compacted,
            result.error.empty() ? "none" : result.error
        );
        if (result.status == EditorAiProviderStatus::Completed) {
            if (m_streamed_response.empty()) m_streamed_response = result.text;
            if (!m_streamed_response.empty()) {
                m_messages.push_back({"assistant", m_streamed_response});
            }
            if (planning && !result.plan_text.empty()) {
                if (m_plan_text != result.plan_text) ++m_plan_revision;
                m_plan_text = result.plan_text;
                m_plan_explanation = result.plan_explanation;
                m_plan = result.plan;
            }
            if (planning && *build_transition_requested) {
                m_approved_plan_text = !result.plan_text.empty()
                    ? result.plan_text : m_plan_text;
                m_plan_build_requested = true;
                m_ui_feedback = "Implementation authorization detected; switching to Build...";
            }
            if (!proposal_actions->empty()) {
                m_pending_proposal = PendingProposal{
                    .request_id = request_id,
                    .actions = std::move(*proposal_actions),
                    .target_scene = active_scene,
                    .command_history = command_history,
                    .entity_state_store = entity_state_store,
                    .selection = selection,
                    .baseline_revision = baseline_revision,
                    .scene_name = active_scene ? active_scene->get_name() : "",
                    .before_entity_index = before_entity_index,
                    .before_asset_index = before_asset_index,
                    .generated_texture_previews = request_context->value(
                        "generated_texture_previews", nlohmann::json::object()
                    ),
                    .auto_apply = authoring_mode == AuthoringMode::AutonomousSafe,
                    .iterative_continuation = iterative_continuation,
                    .original_prompt = approved_plan_build && !m_approved_plan_text.empty()
                        ? m_approved_plan_text : original_prompt,
                    .session_revision_before = session_revision_before
                };
                m_authoring_feedback = m_pending_proposal->auto_apply
                    ? "Proposal validated by the tool boundary; applying on the editor thread..."
                    : "Proposal ready for review. The scene has not changed yet.";
            } else if (iterative_continuation && m_iterative_authoring) {
                m_iteration_continuation_requested = false;
                m_iteration_verification_pending = true;
                m_iteration_verification_inspected = *inspection_tool_count > 0;
                m_iteration_verification_time_budget_reached = final_verification_only;
                m_iteration_verification_revision = baseline_revision;
            } else if (!request_context->at("generated_texture_previews").empty()) {
                remove_cached_texture_previews(request_context->at("generated_texture_previews"));
                m_authoring_feedback =
                    "Codex generated an image but did not queue a valid project import.";
            }
        } else if (result.status == EditorAiProviderStatus::Cancelled) {
            remove_cached_texture_previews(request_context->at("generated_texture_previews"));
            m_request_error = "Request cancelled.";
            if (!proposal_actions->empty()) {
                m_authoring_feedback = "Cancelled proposal discarded without changing the scene.";
            }
            if (iterative_continuation && m_iterative_authoring) {
                m_iteration_continuation_requested = false;
                m_iteration_verification_pending = false;
                m_iterative_authoring->session.finish_cancelled(
                    "Iterative verification was cancelled; applied batches were preserved."
                );
                m_authoring_feedback = m_iterative_authoring->session.outcome();
                m_last_receipt_summary = m_authoring_feedback;
                if (!m_last_application_receipt.empty()) {
                    m_last_application_receipt["session_status"] = "cancelled";
                }
            }
        } else {
            remove_cached_texture_previews(request_context->at("generated_texture_previews"));
            m_request_error = result.error;
            if (!proposal_actions->empty()) {
                m_authoring_feedback = "Failed proposal discarded without changing the scene.";
            }
            if (iterative_continuation && m_iterative_authoring) {
                m_iteration_continuation_requested = false;
                m_iteration_verification_pending = false;
                const std::string verification_failure =
                    result.error.find("already has an active writer") != std::string::npos
                    ? "Verification could not start because the previous Codex turn was still "
                        "closing; applied batches were preserved."
                    : "Iterative verification failed; applied batches were preserved.";
                m_iterative_authoring->session.finish_failed(
                    verification_failure
                );
                m_authoring_feedback = m_iterative_authoring->session.outcome();
                m_last_receipt_summary = m_authoring_feedback;
                if (!m_last_application_receipt.empty()) {
                    m_last_application_receipt["session_status"] = "failed";
                }
            }
        }
        if (using_codex && result.status == EditorAiProviderStatus::Completed &&
            !m_codex_thread_id.empty()) {
            persist_current_conversation_locked(model);
        }
    });
}

void AiAssistantPanel::on_gui() {
    process_live_editor_tools();
    if (m_attach_assets_requested) {
        m_attach_assets_requested = false;
        std::vector<std::filesystem::path> paths = std::move(m_pending_asset_context_paths);
        m_pending_asset_context_paths.clear();
        attach_asset_paths(paths);
    } else if (m_attach_selection_requested) {
        m_attach_selection_requested = false;
        attach_current_selection();
    } else {
        prune_attached_contexts();
    }
    bool auto_apply_proposal = false;
    {
        std::lock_guard lock(m_request_mutex);
        auto_apply_proposal = !m_request_running && m_pending_proposal &&
            m_pending_proposal->auto_apply;
    }
    if (auto_apply_proposal) apply_pending_proposal();
    finalize_iterative_verification();
    continue_iterative_authoring();
    start_approved_plan_build();
    if (!is_open) return;

    bool window_request_running = false;
    {
        std::lock_guard lock(m_request_mutex);
        window_request_running = m_request_running;
    }
    ImGui::SetNextWindowSize(ImVec2(500.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (m_focus_requested) {
        ImGui::SetNextWindowFocus();
        m_focus_requested = false;
    }
    // A live tool waits for this panel to dispatch it on the editor thread.
    // Keep the panel alive until the request completes or is cancelled.
    if (!ImGui::Begin(m_name.c_str(), window_request_running ? nullptr : &is_open)) {
        ImGui::End();
        return;
    }

    const bool codex_available = m_codex_provider && m_codex_provider->is_available() &&
        !m_codex_executable.empty();
    const bool api_provider_available = m_openai_provider && m_openai_provider->is_available();
    const char* api_key = std::getenv("OPENAI_API_KEY");
    const bool api_key_available = api_key && api_key[0] != '\0';
    const bool using_codex = m_backend == ProviderBackend::CodexSubscription;
    const bool provider_available = using_codex ? codex_available : api_provider_available;
    const bool credentials_available = using_codex || api_key_available;
    bool running = false;
    std::string streamed_response;
    std::string reasoning_summary;
    std::string request_error;
    std::string active_tool;
    std::string live_activity;
    std::vector<AiActivityEntry> activity_entries;
    std::chrono::steady_clock::time_point request_started_at;
    std::vector<EditorAiChatMessage> messages;
    std::size_t input_tokens = 0;
    std::size_t output_tokens = 0;
    std::size_t total_tokens = 0;
    std::size_t tool_calls = 0;
    std::uint64_t request_duration_ms = 0;
    std::optional<PendingProposal> pending_proposal;
    std::optional<AppliedAiSession> last_applied_session;
    std::optional<EditorAiAuthoringSession> iterative_session;
    std::shared_ptr<PendingUserInput> pending_user_input;
    std::vector<EditorAiPlanStep> plan;
    std::string plan_text;
    std::string plan_explanation;
    std::size_t plan_revision = 0;
    EditorAiCollaborationMode collaboration_mode{EditorAiCollaborationMode::Default};
    std::string authoring_feedback;
    std::string ui_feedback;
    nlohmann::json application_receipt;
    std::string codex_thread_id;
    std::string codex_session_id;
    std::vector<EditorAiConversationRecord> conversations;
    bool scroll_chat_to_bottom = false;
    bool compact_before_next_request = false;
    bool open_rename_conversation = false;
    bool open_delete_conversation = false;
    {
        std::lock_guard lock(m_request_mutex);
        running = m_request_running;
        streamed_response = m_streamed_response;
        reasoning_summary = m_reasoning_summary;
        request_error = m_request_error;
        active_tool = m_active_tool;
        live_activity = m_live_activity;
        activity_entries = m_activity_entries;
        request_started_at = m_request_started_at;
        messages = m_messages;
        input_tokens = m_last_input_tokens;
        output_tokens = m_last_output_tokens;
        total_tokens = m_last_total_tokens;
        tool_calls = m_last_tool_calls;
        request_duration_ms = m_last_request_duration_ms;
        pending_proposal = m_pending_proposal;
        last_applied_session = m_last_applied_session;
        if (m_iterative_authoring) iterative_session = m_iterative_authoring->session;
        pending_user_input = m_pending_user_input;
        plan = m_plan;
        plan_text = m_plan_text;
        plan_explanation = m_plan_explanation;
        plan_revision = m_plan_revision;
        collaboration_mode = m_collaboration_mode;
        authoring_feedback = m_authoring_feedback;
        ui_feedback = m_ui_feedback;
        application_receipt = m_last_application_receipt;
        codex_thread_id = m_codex_thread_id;
        codex_session_id = m_codex_session_id;
        if (m_conversation_store) conversations = m_conversation_store->conversations();
        scroll_chat_to_bottom = m_scroll_chat_to_bottom;
        compact_before_next_request = m_compact_before_next_request;
        m_scroll_chat_to_bottom = false;
    }

    std::string conversation_title = "New conversation";
    const auto current_conversation = std::find_if(
        conversations.begin(),
        conversations.end(),
        [&](const EditorAiConversationRecord& conversation) {
            return conversation.thread_id == codex_thread_id;
        }
    );
    if (current_conversation != conversations.end()) {
        conversation_title = current_conversation->title;
    }

    draw_codex_badge();
    if (m_open_session_settings_requested) {
        ImGui::OpenPopup("AISessionSettings");
        m_open_session_settings_requested = false;
    }
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 4.0f));
    constexpr float TOOLBAR_WIDTH = 28.0f * 4.0f + 5.0f * 3.0f;
    const float toolbar_x = ImGui::GetCursorPosX() + std::max(
        0.0f,
        ImGui::GetContentRegionAvail().x - TOOLBAR_WIDTH
    );
    ImGui::SetCursorPosX(toolbar_x);
    if (editor_icon_button(
        "conversation_actions",
        EditorIcon::Ellipsis,
        "Conversation actions",
        ImGui::IsPopupOpen("AIConversationActions"),
        ImVec2(28.0f, 28.0f)
    )) {
        ImGui::OpenPopup("AIConversationActions");
    }
    ImGui::SameLine();
    if (editor_icon_button(
        "conversation_history",
        EditorIcon::History,
        "Conversation history",
        ImGui::IsPopupOpen("AIConversationHistory"),
        ImVec2(28.0f, 28.0f)
    )) {
        ImGui::OpenPopup("AIConversationHistory");
    }
    ImGui::SameLine();
    if (editor_icon_button(
        "session_settings",
        EditorIcon::Settings,
        "Session settings",
        ImGui::IsPopupOpen("AISessionSettings"),
        ImVec2(28.0f, 28.0f)
    )) {
        ImGui::OpenPopup("AISessionSettings");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(running || pending_proposal.has_value());
    if (editor_icon_button(
        "new_conversation",
        EditorIcon::Edit,
        "New conversation",
        codex_thread_id.empty() && messages.empty(),
        ImVec2(28.0f, 28.0f)
    )) {
        start_new_conversation();
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();

    ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 0.0f), ImVec2(520.0f, 460.0f));
    if (ImGui::BeginPopup("AIConversationHistory")) {
        ImGui::TextUnformatted("Conversations");
        ImGui::Separator();
        ImGui::BeginDisabled(running || pending_proposal.has_value());
        if (ImGui::Selectable("New conversation", codex_thread_id.empty())) {
            start_new_conversation();
        }
        if (conversations.empty()) {
            ImGui::TextDisabled("No saved conversations yet.");
        }
        for (const EditorAiConversationRecord& conversation : conversations) {
            const std::string label = conversation.title + "##" + conversation.thread_id;
            if (ImGui::Selectable(
                label.c_str(),
                conversation.thread_id == codex_thread_id
            )) {
                restore_conversation(conversation.thread_id);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\n%s\n%zu messages",
                    conversation.model.c_str(),
                    conversation.thread_id.c_str(),
                    conversation.messages.size()
                );
            }
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("AISessionSettings")) {
        ImGui::TextUnformatted("Session settings");
        ImGui::Separator();
        ImGui::BeginDisabled(running || pending_proposal.has_value());
        const char* backends[] = {"Codex subscription", "OpenAI API key"};
        int backend_index = m_backend == ProviderBackend::CodexSubscription ? 0 : 1;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Backend", &backend_index, backends, 2)) {
            m_backend = backend_index == 0
                ? ProviderBackend::CodexSubscription
                : ProviderBackend::OpenAiApi;
            start_new_conversation();
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Backend changed. A new conversation was started.";
        }
        if (m_backend == ProviderBackend::CodexSubscription) {
            ImGui::TextDisabled(
                "Full Access: Codex can run local commands; editor tools preserve scene Undo/Redo"
            );
            ImGui::TextDisabled(
                "Context: automatic compaction after %zu input tokens",
                AUTO_COMPACT_INPUT_TOKEN_THRESHOLD
            );
        }
        const std::string current_model(m_model.data());
        int model_preset = current_model == "gpt-5.6-luna" ? 0
            : (current_model == "gpt-5.6-terra" ? 1
            : (current_model == "gpt-5.6-sol" ? 2 : 3));
        const char* model_presets[] = {
            "Luna - fastest",
            "Terra - balanced",
            "Sol - highest capability",
            "Custom"
        };
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Model preset", &model_preset, model_presets, 4) && model_preset < 3) {
            const char* selected_model = model_preset == 0
                ? "gpt-5.6-luna" : (model_preset == 1 ? "gpt-5.6-terra" : "gpt-5.6-sol");
            std::snprintf(m_model.data(), m_model.size(), "%s", selected_model);
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Model updated.";
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("Model", m_model.data(), m_model.size())) {
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Model updated.";
        }
        const char* reasoning_efforts[] = {"Low - fast", "Medium", "High"};
        int effort_index = m_reasoning_effort == ReasoningEffort::Low ? 0
            : (m_reasoning_effort == ReasoningEffort::Medium ? 1 : 2);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Reasoning", &effort_index, reasoning_efforts, 3)) {
            m_reasoning_effort = effort_index == 0
                ? ReasoningEffort::Low
                : (effort_index == 1 ? ReasoningEffort::Medium : ReasoningEffort::High);
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Reasoning effort updated.";
        }
        const char* authoring_modes[] = {"Confirm before apply", "Autonomous safe"};
        int authoring_mode_index = m_authoring_mode == AuthoringMode::ConfirmBeforeApply ? 0 : 1;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("Authoring", &authoring_mode_index, authoring_modes, 2)) {
            m_authoring_mode = authoring_mode_index == 0
                ? AuthoringMode::ConfirmBeforeApply
                : AuthoringMode::AutonomousSafe;
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = m_authoring_mode == AuthoringMode::AutonomousSafe
                ? "Autonomous safe mode enabled."
                : "Changes now require confirmation.";
        }
        if (ImGui::Checkbox(
                "Iterate visually after apply",
                &m_iterative_verification_enabled
            )) {
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = m_iterative_verification_enabled
                ? "Codex will inspect applied scene changes and may queue corrections."
                : "Applied changes will use fast atomic editor validation.";
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Off is faster and validates data deterministically. Enable only when a second "
                "visual/spatial inspection is worth another Codex turn."
            );
        }
        ImGui::BeginDisabled(!m_iterative_verification_enabled);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderInt(
            "AI work budget (min)",
            &m_authoring_work_budget_minutes,
            1,
            30,
            "%d min"
        )) {
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Iterative AI work budget updated.";
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Counts provider work only; time waiting for confirmation is excluded"
            );
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::Button("Reset defaults")) {
            m_backend = ProviderBackend::CodexSubscription;
            m_reasoning_effort = ReasoningEffort::Low;
            m_authoring_mode = AuthoringMode::ConfirmBeforeApply;
            m_iterative_verification_enabled = false;
            m_authoring_work_budget_minutes = 10;
            std::snprintf(m_model.data(), m_model.size(), "%s", "gpt-5.6-sol");
            start_new_conversation();
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Session defaults restored.";
            ENGINE_LOG_INFO("[AI AUDIT] ui_action=session_settings_reset");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Done")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("AIConversationActions")) {
        ImGui::BeginDisabled(messages.empty());
        if (ImGui::MenuItem("Copy conversation")) {
            copy_to_clipboard(conversation_transcript(messages), "Conversation copied.");
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::BeginDisabled(codex_thread_id.empty());
        if (ImGui::MenuItem("Copy thread ID")) {
            copy_to_clipboard(codex_thread_id, "Thread ID copied.");
        }
        if (ImGui::MenuItem("Copy resume command")) {
            copy_to_clipboard(
                codex_resume_command(codex_thread_id),
                "Codex resume command copied."
            );
        }
        ImGui::Separator();
        ImGui::BeginDisabled(running || pending_proposal.has_value() || !using_codex);
        if (ImGui::MenuItem(
            "Compact context before next message",
            nullptr,
            compact_before_next_request
        )) {
            m_compact_before_next_request = !compact_before_next_request;
            compact_before_next_request = m_compact_before_next_request;
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = compact_before_next_request
                ? "Codex context will be compacted before the next message."
                : "Manual context compaction cancelled.";
            ENGINE_LOG_INFO(
                "[AI AUDIT] ui_action=context_compaction_scheduled enabled={}",
                compact_before_next_request
            );
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                "Keeps the Codex thread but compresses its remote working context before the next turn"
            );
        }
        ImGui::Separator();
        ImGui::BeginDisabled(running || pending_proposal.has_value());
        if (ImGui::MenuItem("Rename conversation...")) {
            m_conversation_action_thread_id = codex_thread_id;
            std::snprintf(
                m_conversation_title.data(),
                m_conversation_title.size(),
                "%s",
                conversation_title.c_str()
            );
            open_rename_conversation = true;
        }
        if (ImGui::MenuItem("Delete conversation...")) {
            m_conversation_action_thread_id = codex_thread_id;
            open_delete_conversation = true;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (codex_thread_id.empty()) ImGui::TextDisabled("No saved conversation selected.");
        ImGui::Separator();
        ImGui::TextDisabled("History: Library/EditorAi");
        ImGui::TextDisabled("Audit: engine.log");
        ImGui::EndPopup();
    }

    if (open_rename_conversation) ImGui::OpenPopup("Rename AI conversation");
    if (open_delete_conversation) ImGui::OpenPopup("Delete AI conversation");

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
        "Rename AI conversation",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize
    )) {
        ImGui::TextUnformatted("Conversation title");
        ImGui::SetNextItemWidth(-1.0f);
        const bool submit_rename = ImGui::InputText(
            "##conversation_title",
            m_conversation_title.data(),
            m_conversation_title.size(),
            ImGuiInputTextFlags_EnterReturnsTrue
        );
        const bool valid_title = has_visible_text(m_conversation_title.data());
        ImGui::BeginDisabled(!valid_title);
        if (ImGui::Button("Rename") || (submit_rename && valid_title)) {
            rename_conversation(m_conversation_action_thread_id, m_conversation_title.data());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(
        "Delete AI conversation",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize
    )) {
        ImGui::TextWrapped(
            "Delete '%s' from local editor history? This does not delete the remote Codex thread.",
            conversation_title.c_str()
        );
        if (ImGui::Button("Delete")) {
            delete_conversation(m_conversation_action_thread_id);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::TextWrapped("%s", conversation_title.c_str());
    if (provider_available && credentials_available) {
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 0.52f, 1.0f), "Connected");
    } else if (using_codex) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.32f, 1.0f), "Codex CLI not found");
    } else if (!provider_available) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.32f, 1.0f), "HTTP unavailable");
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.32f, 1.0f), "API key missing");
    }
    if (!codex_session_id.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Session %s", abbreviated_id(codex_session_id).c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", codex_session_id.c_str());
        }
    }
    ImGui::Separator();

    ImGui::BeginDisabled(running || pending_proposal.has_value() || pending_user_input);
    if (ImGui::SmallButton(collaboration_mode == EditorAiCollaborationMode::Plan
            ? "Plan [active]" : "Plan")) {
        set_collaboration_mode(EditorAiCollaborationMode::Plan);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(collaboration_mode == EditorAiCollaborationMode::Default
            ? "Build [active]" : "Build")) {
        set_collaboration_mode(EditorAiCollaborationMode::Default);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(
        collaboration_mode == EditorAiCollaborationMode::Plan
            ? "Read-only planning" : "Project changes allowed"
    );

    if (!plan_text.empty() || !plan.empty()) {
        const std::string plan_header = "Plan revision " + std::to_string(plan_revision) +
            "##AIPlan";
        if (ImGui::CollapsingHeader(plan_header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (!plan_explanation.empty()) {
                draw_selectable_text(
                    "##plan-explanation",
                    plan_explanation,
                    std::max(1.0f, ImGui::GetContentRegionAvail().x)
                );
            }
            for (std::size_t index = 0; index < plan.size(); ++index) {
                const EditorAiPlanStep& step = plan[index];
                ImGui::PushID(static_cast<int>(index));
                const ImVec4 color = step.status == "completed"
                    ? ImVec4(0.35f, 0.78f, 0.52f, 1.0f)
                    : (step.status == "inProgress"
                        ? ImVec4(0.48f, 0.66f, 0.95f, 1.0f)
                        : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextColored(color, "%s", step.status == "completed" ? "[x]" : "[ ]");
                ImGui::SameLine();
                draw_selectable_text(
                    "##plan-step",
                    step.step,
                    std::max(1.0f, ImGui::GetContentRegionAvail().x)
                );
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Copy plan")) {
                copy_to_clipboard(plan_text, "Plan copied.");
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(running || pending_proposal.has_value() || pending_user_input);
            if (ImGui::SmallButton("Approve & Build")) {
                std::lock_guard lock(m_request_mutex);
                m_approved_plan_text = m_plan_text;
                m_plan_build_requested = true;
                m_ui_feedback = "Plan approved; switching to Build...";
            }
            ImGui::EndDisabled();
        }
    }

    const float conversation_height = std::clamp(
        ImGui::GetContentRegionAvail().y - 270.0f,
        220.0f,
        760.0f
    );
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::BeginChild("AIConversation", ImVec2(0.0f, conversation_height), true);
    if (messages.empty() && streamed_response.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Start a conversation");
        ImGui::TextWrapped(
            "Ask about the active scene, the selected entity, renderer contracts, or request a "
            "reversible scene change."
        );
    }
    const std::uint64_t activity_elapsed_ms = running &&
            request_started_at != std::chrono::steady_clock::time_point{}
        ? static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - request_started_at
        ).count())
        : request_duration_ms;
    const auto draw_activity = [&]() {
        if (activity_entries.empty() && reasoning_summary.empty()) return;
        const std::string phase = running
            ? (live_activity.empty() ? "Codex is working" : live_activity)
            : "Work details";
        char header[160];
        std::snprintf(
            header,
            sizeof(header),
            "%s  %.1fs  %zu operation%s###AIActivity",
            phase.c_str(),
            static_cast<double>(activity_elapsed_ms) / 1000.0,
            activity_entries.size(),
            activity_entries.size() == 1 ? "" : "s"
        );
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.17f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.21f, 0.28f, 1.0f));
        const bool open = ImGui::CollapsingHeader(
            header,
            running ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None
        );
        ImGui::PopStyleColor(2);
        if (!open) return;
        ImGui::Indent(8.0f);
        for (std::size_t index = 0; index < activity_entries.size(); ++index) {
            const AiActivityEntry& entry = activity_entries[index];
            if (!entry.item_id.empty()) {
                ImGui::PushID(entry.item_id.c_str());
            } else {
                ImGui::PushID(static_cast<int>(index));
            }
            const bool in_progress = entry.status == "inProgress";
            const bool failed = entry.status == "failed" || entry.status == "declined";
            const ImVec4 status_color = failed
                ? ImVec4(0.95f, 0.36f, 0.32f, 1.0f)
                : (in_progress
                    ? ImVec4(0.48f, 0.66f, 0.95f, 1.0f)
                    : ImVec4(0.35f, 0.78f, 0.52f, 1.0f));
            ImGui::TextColored(status_color, "%s", in_progress ? ">" : (failed ? "!" : "+"));
            ImGui::SameLine();
            std::string visible_detail = entry.item_type == "reasoning"
                ? reasoning_summary
                : (!entry.detail.empty() ? entry.detail : entry.output);
            if (entry.status == "inProgress" && !entry.output.empty() &&
                visible_detail != entry.output) {
                visible_detail += "\n\nOutput\n" + entry.output;
            }
            const bool has_detail = !visible_detail.empty();
            char row[192];
            const std::uint64_t displayed_duration_ms = in_progress
                ? (activity_elapsed_ms >= entry.elapsed_ms
                    ? activity_elapsed_ms - entry.elapsed_ms : 0)
                : entry.duration_ms;
            const double time_seconds = static_cast<double>(displayed_duration_ms) / 1000.0;
            char time_label[32];
            std::snprintf(time_label, sizeof(time_label), "%.1fs", time_seconds);
            const float row_text_width = std::max(
                24.0f,
                ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(time_label).x - 42.0f
            );
            const std::string compact_label = ellipsize_text_to_width(
                entry.label,
                row_text_width
            );
            std::snprintf(
                row,
                sizeof(row),
                "%s  %s###activity-row",
                compact_label.c_str(),
                time_label
            );
            const bool detail_open = has_detail
                ? ImGui::TreeNodeEx(row, ImGuiTreeNodeFlags_SpanAvailWidth)
                : (ImGui::TextUnformatted(entry.label.c_str()), false);
            if (detail_open) {
                if (editor_icon_button("copy-detail", EditorIcon::Copy, "Copy operation detail")) {
                    copy_to_clipboard(visible_detail, "Operation detail copied.");
                }
                draw_selectable_text(
                    "##activity-detail",
                    visible_detail,
                    std::max(1.0f, ImGui::GetContentRegionAvail().x),
                    220.0f
                );
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::Unindent(8.0f);
        ImGui::Spacing();
    };
    int message_index = 0;
    bool activity_drawn = false;
    for (std::size_t index = 0; index < messages.size(); ++index) {
        const EditorAiChatMessage& message = messages[index];
        if (!running && !activity_drawn && index + 1 == messages.size() &&
            message.role == "assistant") {
            draw_activity();
            activity_drawn = true;
        }
        if (draw_chat_message(message, message_index++, ImGui::GetContentRegionAvail().x)) {
            copy_to_clipboard(message.content, "Response copied.");
        }
    }
    if (running) {
        draw_activity();
        activity_drawn = true;
    }
    if (running && !streamed_response.empty()) {
        if (draw_chat_message(
            EditorAiChatMessage{"assistant", streamed_response},
            message_index,
            ImGui::GetContentRegionAvail().x
        )) {
            copy_to_clipboard(streamed_response, "Partial response copied.");
        }
    }
    if (running && streamed_response.empty()) {
        ImGui::TextColored(
            ImVec4(0.40f, 0.82f, 0.62f, 1.0f),
            "%s...",
            live_activity.empty() ? "Codex is working" : live_activity.c_str()
        );
    }
    if (pending_user_input) {
        ImGui::Separator();
        ImGui::TextColored(
            ImVec4(0.48f, 0.66f, 0.95f, 1.0f),
            "Codex needs your input"
        );
        for (std::size_t index = 0;
             index < pending_user_input->request.questions.size(); ++index) {
            const EditorAiUserInputQuestion& question =
                pending_user_input->request.questions[index];
            ImGui::PushID(static_cast<int>(index));
            if (!question.header.empty()) ImGui::TextDisabled("%s", question.header.c_str());
            draw_selectable_text(
                "##question",
                question.question,
                std::max(1.0f, ImGui::GetContentRegionAvail().x)
            );
            for (const EditorAiUserInputOption& option : question.options) {
                const bool selected = !pending_user_input->custom_selected[index] &&
                    pending_user_input->answers[index] == option.label;
                if (ImGui::Selectable(option.label.c_str(), selected)) {
                    pending_user_input->answers[index] = option.label;
                    pending_user_input->custom_selected[index] = false;
                }
                if (!option.description.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", option.description.c_str());
                }
            }
            if (question.allow_other || question.options.empty()) {
                if (!question.options.empty() &&
                    ImGui::Selectable("Other...", pending_user_input->custom_selected[index])) {
                    pending_user_input->custom_selected[index] = true;
                }
                if (pending_user_input->custom_selected[index]) {
                    ImGui::InputText(
                        "##custom_answer",
                        pending_user_input->custom_answers[index].data(),
                        pending_user_input->custom_answers[index].size(),
                        question.secret ? ImGuiInputTextFlags_Password : ImGuiInputTextFlags_None
                    );
                }
            }
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (ImGui::Button("Continue")) resolve_user_input(true);
        ImGui::SameLine();
        if (ImGui::Button("Cancel request")) resolve_user_input(false);
    }
    if (!activity_drawn && !activity_entries.empty()) {
        draw_activity();
    }
    if (scroll_chat_to_bottom) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    const bool has_attached_context = m_attachments.has_contexts();
    const bool has_screenshots = m_attachments.has_screenshots();
    const float composer_height = 126.0f + (has_attached_context ? 30.0f : 0.0f) +
        (has_screenshots ? 66.0f : 0.0f);
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
    );
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImGui::GetStyleColorVec4(ImGuiCol_Border)
    );
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13.0f, 11.0f));
    ImGui::BeginChild("AIComposer", ImVec2(0.0f, composer_height), true);

    draw_editor_ai_attachment_view(m_attachments);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    // Keep the native blinking caret inside the multiline widget's clip rect.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
    ImGui::BeginDisabled(running || pending_proposal.has_value());
    if (m_focus_prompt_requested) {
        ImGui::SetKeyboardFocusHere();
        m_focus_prompt_requested = false;
    }
    const bool submitted = ImGui::InputTextMultiline(
        "##ai_prompt",
        m_prompt.data(),
        m_prompt.size(),
        ImVec2(-1.0f, 62.0f),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine
    );
    const bool prompt_hovered = ImGui::IsItemHovered();
    const bool prompt_active = ImGui::IsItemActive();
    const ImGuiIO& composer_io = ImGui::GetIO();
    if (prompt_active && (composer_io.KeySuper || composer_io.KeyCtrl) &&
        ImGui::IsKeyPressed(ImGuiKey_V, false) &&
        (SDL_HasClipboardData("image/png") || SDL_HasClipboardData("image/jpeg"))) {
        attach_clipboard_screenshot();
    }
    if (prompt_hovered && !running && !pending_proposal.has_value()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }
    ImGui::EndDisabled();
    const ImVec2 prompt_min = ImGui::GetItemRectMin();
    const ImVec2 prompt_max = ImGui::GetItemRectMax();
    if (prompt_active) {
        ImGui::GetWindowDrawList()->AddRect(
            ImVec2(prompt_min.x - 4.0f, prompt_min.y - 4.0f),
            ImVec2(prompt_max.x + 4.0f, prompt_max.y + 4.0f),
            ImGui::GetColorU32(ImGuiCol_NavHighlight),
            6.0f,
            0,
            1.5f
        );
    }
    if (m_prompt[0] == '\0' && !prompt_active) {
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(prompt_min.x + 4.0f, prompt_min.y + 2.0f),
            IM_COL32(185, 185, 180, 175),
            "Ask Codex about the scene or engine..."
        );
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    const bool can_send = provider_available && credentials_available && !running &&
        !pending_proposal.has_value() &&
        (has_visible_text(m_prompt.data()) || m_attachments.has_screenshots()) &&
        has_visible_text(m_model.data());
    if (submitted && can_send) start_request();

    if (editor_icon_button(
        "composer_actions",
        EditorIcon::Plus,
        m_image_generation_requested
            ? "Codex ImageGen is enabled for the next message"
            : "Add context or enable image generation",
        ImGui::IsPopupOpen("AIComposerActions") || m_image_generation_requested,
        ImVec2(24.0f, 24.0f)
    )) {
        ImGui::OpenPopup("AIComposerActions");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(running || pending_proposal.has_value() ||
        collaboration_mode == EditorAiCollaborationMode::Plan);
    if (editor_icon_button(
        "authoring_security",
        EditorIcon::Shield,
        m_authoring_mode == AuthoringMode::AutonomousSafe
            ? "Autonomous safe: validated changes apply automatically"
            : "Confirm before apply: review every scene change",
        m_authoring_mode == AuthoringMode::AutonomousSafe,
        ImVec2(24.0f, 24.0f)
    )) {
        toggle_authoring_mode();
    }
    ImGui::EndDisabled();
    if (ImGui::GetContentRegionAvail().x > 330.0f) {
        ImGui::SameLine();
        if (collaboration_mode == EditorAiCollaborationMode::Plan) {
            ImGui::TextColored(
                ImVec4(0.48f, 0.66f, 0.95f, 1.0f),
                "Read-only plan"
            );
        } else if (m_authoring_mode == AuthoringMode::AutonomousSafe) {
            ImGui::TextColored(
                ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline),
                "Autonomous safe"
            );
        } else {
            ImGui::TextDisabled("Confirm changes");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Change authoring approval mode");
        if (ImGui::IsItemClicked() && !running && !pending_proposal.has_value() &&
            collaboration_mode != EditorAiCollaborationMode::Plan) {
            toggle_authoring_mode();
        }
    }

    const std::string effort_label = m_reasoning_effort == ReasoningEffort::Low
        ? "Low" : (m_reasoning_effort == ReasoningEffort::Medium ? "Medium" : "High");
    const std::string model_label = compact_model_name(m_model.data()) + "  " + effort_label;
    const float right_controls_width = ImGui::CalcTextSize(model_label.c_str()).x + 44.0f;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - right_controls_width
    ));
    ImGui::BeginDisabled(running || pending_proposal.has_value());
    ImGui::TextDisabled("%s", model_label.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open model and reasoning settings");
    if (ImGui::IsItemClicked()) m_open_session_settings_requested = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (running) {
        if (round_icon_button("cancel", EditorIcon::Stop, "Cancel request", true, false)) {
            cancel_request();
        }
    } else if (round_icon_button("send", EditorIcon::ArrowUp, "Send", can_send, true)) {
        start_request();
    }

    if (ImGui::BeginPopup("AIComposerActions")) {
        if (ImGui::MenuItem("Paste screenshot", "Cmd+V")) {
            attach_clipboard_screenshot();
        }
        if (ImGui::MenuItem("Attach Scene Viewport screenshot")) {
            attach_viewport_screenshot();
        }
        ImGui::Separator();
        ImGui::BeginDisabled(!using_codex || m_imagegen_skill_path.empty());
        if (ImGui::MenuItem(
            "Generate PNG with Codex",
            nullptr,
            m_image_generation_requested
        )) {
            m_image_generation_requested = !m_image_generation_requested;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                m_imagegen_skill_path.empty()
                    ? "The Codex imagegen skill was not found"
                    : "Uses the Codex subscription; no API key required"
            );
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Attach selection", "Cmd+L")) {
            attach_current_selection();
        }
        if (ImGui::MenuItem("Copy scene context")) {
            copy_to_clipboard(current_context_json(), "Scene context copied.");
        }
        if (ImGui::MenuItem("Copy selection context")) {
            const nlohmann::json context = nlohmann::json::parse(
                current_context_json(), nullptr, false
            );
            const std::string selection_context = !context.is_discarded() &&
                    context.contains("selection")
                ? context.at("selection").dump(2)
                : "{}";
            copy_to_clipboard(selection_context, "Selection context copied.");
        }
        ImGui::BeginDisabled(running || pending_proposal.has_value());
        ImGui::BeginDisabled(m_prompt[0] == '\0');
        if (ImGui::MenuItem("Clear message")) {
            m_prompt.fill('\0');
            m_focus_prompt_requested = true;
            std::lock_guard lock(m_request_mutex);
            m_ui_feedback = "Message cleared.";
        }
        ImGui::EndDisabled();
        if (ImGui::MenuItem("New conversation")) start_new_conversation();
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    if (!active_tool.empty()) {
        ImGui::TextDisabled("Working: %s", active_tool.c_str());
    }
    if (!request_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.36f, 0.32f, 1.0f));
        draw_selectable_text(
            "##request-error",
            friendly_request_error(request_error),
            std::max(1.0f, ImGui::GetContentRegionAvail().x)
        );
        ImGui::PopStyleColor();
        ImGui::BeginDisabled(running || pending_proposal.has_value() || !provider_available ||
            !credentials_available);
        if (ImGui::SmallButton("Retry request")) {
            start_request(
                "Retry the previous request from a fresh scene snapshot. Re-resolve every target "
                "and repeat required proposal actions because the interrupted proposal was discarded."
            );
        }
        ImGui::EndDisabled();
    }
    if (!running && (total_tokens > 0 || !request_error.empty() || !ui_feedback.empty()) &&
        ImGui::CollapsingHeader("Last request details")) {
        char duration[32];
        std::snprintf(
            duration,
            sizeof(duration),
            "%.2f",
            static_cast<double>(request_duration_ms) / 1000.0
        );
        std::string request_details = std::string("Duration: ") + duration +
            " s\nEditor tools: " + std::to_string(tool_calls);
        if (total_tokens > 0) {
            request_details += "\nTokens: " + std::to_string(input_tokens) + " input, " +
                std::to_string(output_tokens) + " output, " + std::to_string(total_tokens) +
                " total";
        }
        if (!request_error.empty()) request_details += "\n\nTechnical error\n" + request_error;
        request_details += "\n\nAudit log: Console and Library/engine.log";
        draw_selectable_text(
            "##last-request-details",
            request_details,
            std::max(1.0f, ImGui::GetContentRegionAvail().x),
            180.0f
        );
        if (using_codex && !codex_thread_id.empty() &&
            (compact_before_next_request || input_tokens >= AUTO_COMPACT_INPUT_TOKEN_THRESHOLD)) {
            ImGui::TextColored(
                ImVec4(0.76f, 0.68f, 0.36f, 1.0f),
                "Context compaction scheduled for the next message"
            );
        }
    }
    if (!ui_feedback.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.66f, 0.95f, 1.0f));
        draw_selectable_text(
            "##ui-feedback",
            ui_feedback,
            std::max(1.0f, ImGui::GetContentRegionAvail().x)
        );
        ImGui::PopStyleColor();
    }

    if (pending_proposal) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Pending AI proposal");
        ImGui::TextDisabled(
            "%zu actions | scene: %s | baseline revision: %llu",
            pending_proposal->actions.size(),
            pending_proposal->scene_name.c_str(),
            static_cast<unsigned long long>(pending_proposal->baseline_revision)
        );
        if (pending_proposal->generated_texture_previews.is_object() &&
            !pending_proposal->generated_texture_previews.empty()) {
            const nlohmann::json& preview =
                pending_proposal->generated_texture_previews.begin().value();
            const std::string preview_id = preview.value("preview_id", "");
            const std::string preview_path = preview.value("preview_path", "");
            if (!preview_id.empty() && preview_id != m_loaded_texture_preview_id) {
                m_generated_texture_preview.reset();
                m_loaded_texture_preview_id = preview_id;
                if (m_serialization_context.renderer && !preview_path.empty()) {
                    m_generated_texture_preview.reset(Texture::load_from_file(
                        m_serialization_context.renderer, preview_path
                    ));
                }
            }
            ImGui::TextDisabled("Generated PNG preview");
            if (m_generated_texture_preview && m_generated_texture_preview->get_sdl_texture()) {
                const float width = static_cast<float>(
                    std::max(1, m_generated_texture_preview->get_width())
                );
                const float height = static_cast<float>(
                    std::max(1, m_generated_texture_preview->get_height())
                );
                const float scale = std::min({1.0f, 280.0f / width, 190.0f / height});
                ImGui::Image(
                    m_generated_texture_preview->get_sdl_texture(),
                    ImVec2(width * scale, height * scale)
                );
                ImGui::TextDisabled(
                    "%d x %d px | %zu bytes",
                    m_generated_texture_preview->get_width(),
                    m_generated_texture_preview->get_height(),
                    preview.value("byte_count", std::size_t{0})
                );
            } else {
                ImGui::TextDisabled("PNG cached and ready to import");
            }
        }
        ImGui::BeginChild("AIPendingProposal", ImVec2(0.0f, 118.0f), true);
        for (std::size_t index = 0; index < pending_proposal->actions.size(); ++index) {
            const EditorAiAuthoringAction& action = pending_proposal->actions[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::Bullet();
            ImGui::SameLine();
            draw_selectable_text(
                "##proposal-action",
                action.summary,
                std::max(1.0f, ImGui::GetContentRegionAvail().x)
            );
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (!pending_proposal->auto_apply) {
            if (ImGui::Button("Apply proposal")) apply_pending_proposal();
            ImGui::SameLine();
            if (ImGui::Button("Dismiss")) dismiss_pending_proposal();
        }
    }

    if (!authoring_feedback.empty()) {
        const bool failed = authoring_feedback.starts_with("Proposal rejected") ||
            authoring_feedback.starts_with("Failed") ||
            authoring_feedback.find("failed") != std::string::npos;
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            failed ? ImVec4(0.95f, 0.36f, 0.32f, 1.0f)
                   : ImVec4(0.35f, 0.78f, 0.52f, 1.0f)
        );
        draw_selectable_text(
            "##authoring-feedback",
            authoring_feedback,
            std::max(1.0f, ImGui::GetContentRegionAvail().x)
        );
        ImGui::PopStyleColor();
    }
    if (iterative_session && iterative_session->status() !=
            EditorAiAuthoringSession::Status::Idle) {
        ImGui::TextDisabled(
            "%s: %s | %zu batches | %zu actions | %.1f/%.1f min AI time",
            iterative_session->status() ==
                    EditorAiAuthoringSession::Status::ValidatedAtomically
                ? "Authoring" : "Iteration",
            iterative_session->status_label().c_str(),
            iterative_session->applied_batches(),
            iterative_session->total_actions(),
            static_cast<double>(iterative_session->work_time().count()) / 60000.0,
            static_cast<double>(iterative_session->work_budget().count()) / 60000.0
        );
        if (!iterative_session->outcome().empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", iterative_session->outcome().c_str());
        }
    }
    if (!application_receipt.empty() && ImGui::CollapsingHeader("Last application receipt")) {
        const std::string receipt_text = application_receipt.dump(2);
        if (ImGui::Button("Copy receipt JSON")) {
            copy_to_clipboard(receipt_text, "Application receipt copied.");
        }
        draw_selectable_text(
            "##application-receipt",
            receipt_text,
            std::max(1.0f, ImGui::GetContentRegionAvail().x),
            150.0f
        );
    }

    bool can_undo_ai_session = false;
    if (last_applied_session) {
        const auto session_scene = last_applied_session->target_scene.lock();
        const auto session_history = last_applied_session->command_history.lock();
        const auto active_scene = m_scene_manager
            ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
        can_undo_ai_session = session_scene && session_scene == active_scene && session_history &&
            session_history == m_command_history &&
            session_history->get_current_revision() == last_applied_session->after_revision &&
            session_history->get_undo_description() == last_applied_session->description;
    }
    if (last_applied_session) {
        ImGui::BeginDisabled(!can_undo_ai_session);
        if (ImGui::Button("Undo last AI session")) undo_last_ai_session();
        ImGui::EndDisabled();
        if (!can_undo_ai_session) {
            ImGui::SameLine();
            ImGui::TextDisabled("Use regular Undo: newer edits exist.");
        }
    }

    if (ImGui::CollapsingHeader("Context preview")) {
        const std::string context = current_context_json();
        if (ImGui::Button("Copy JSON")) {
            copy_to_clipboard(context, "Context JSON copied.");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Live scene metadata; document contents are loaded only on request");
        draw_selectable_text(
            "##context-preview",
            context,
            std::max(1.0f, ImGui::GetContentRegionAvail().x),
            180.0f
        );
    }

    ImGui::End();
}

} // namespace Engine
