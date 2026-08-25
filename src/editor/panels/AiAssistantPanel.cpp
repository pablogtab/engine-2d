#include "AiAssistantPanel.hpp"
#include "EditorAiAttachmentView.hpp"
#include "../../core/AssetDatabase.hpp"
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
constexpr std::uint32_t EDITOR_AI_TOOL_CONTRACT_VERSION = 9;
constexpr std::size_t AUTO_COMPACT_INPUT_TOKEN_THRESHOLD = 24000;
std::string project_root();

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
    const float text_height = ImGui::CalcTextSize(
        message.content.c_str(),
        nullptr,
        false,
        text_width
    ).y;
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
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + text_width);
    ImGui::TextUnformatted(message.content.c_str());
    ImGui::PopTextWrapPos();
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
        {"filesystem", "full_access"},
        {"max_actions_per_proposal", EditorAiAuthoringRegistry::MAX_ACTIONS_PER_PROPOSAL},
        {"tools", m_authoring_registry.tool_schemas()}
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
        .messages = m_messages
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
    m_authoring_feedback = "AI proposal dismissed without changing the scene.";
    m_last_receipt_summary = "The previous editor proposal was dismissed; no changes were applied.";
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
                         previews = proposal.generated_texture_previews](std::string message) {
        ENGINE_LOG_WARN("[AI AUDIT] request={} proposal=rejected reason={}", request_id, message);
        remove_cached_texture_previews(previews);
        std::lock_guard lock(m_request_mutex);
        m_pending_proposal.reset();
        m_generated_texture_preview.reset();
        m_loaded_texture_preview_id.clear();
        m_authoring_feedback = "Proposal rejected: " + message;
        m_last_receipt_summary = "The previous editor proposal failed and was rolled back: " +
            message + ". No scene changes were kept.";
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
    const nlohmann::json receipt = build_application_receipt(
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
    m_pending_proposal.reset();
    m_generated_texture_preview.reset();
    m_loaded_texture_preview_id.clear();
    m_last_application_receipt = receipt;
    m_last_receipt_summary = summary;
    m_authoring_feedback = description + " applied. " + summary + " One Undo restores the previous state.";
    m_messages.push_back({"editor", "Applied successfully. " + summary});
    m_scroll_chat_to_bottom = true;
    persist_current_conversation_locked(m_model.data());
    ENGINE_LOG_INFO(
        "[AI AUDIT] request={} proposal=applied actions={} revision={} description={}",
        proposal.request_id,
        proposal.actions.size(),
        proposal.command_history->get_current_revision(),
        description
    );
    ENGINE_LOG_INFO("[AI AUDIT] receipt={}", receipt.dump());
    m_last_applied_session = AppliedAiSession{
        .request_id = proposal.request_id,
        .target_scene = scene,
        .command_history = proposal.command_history,
        .after_revision = proposal.command_history->get_current_revision(),
        .description = description
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

void AiAssistantPanel::start_request() {
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

    std::string prompt(m_prompt.data());
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

    bool image_generation_requested = false;
    {
        std::lock_guard lock(m_request_mutex);
        if (m_pending_proposal) {
            m_request_error = "Apply or dismiss the pending AI proposal before sending another request.";
            return;
        }
        image_generation_requested = using_codex && !m_imagegen_skill_path.empty() &&
            (m_image_generation_requested || prompt_requests_generated_image(prompt) ||
             (m_conversation_has_generated_image && prompt_refines_generated_image(prompt)));
        m_image_generation_requested = false;
    }

    const std::shared_ptr<Scene> active_scene = m_scene_manager
        ? m_scene_manager->get_active_scene_shared_ptr() : nullptr;
    const std::shared_ptr<EditorCommandHistory> command_history = m_command_history;
    const std::shared_ptr<EditorEntityStateStore> entity_state_store = m_entity_state_store;
    const std::shared_ptr<EditorSelection> selection = m_selection;
    const bool authoring_available = active_scene && command_history && entity_state_store && selection &&
        m_scene_manager && m_scene_manager->is_editing();
    const bool viewport_available = active_scene && !m_viewport_panel.expired() && m_scene_manager &&
        m_scene_manager->is_editing();
    const std::uint64_t baseline_revision = command_history
        ? command_history->get_current_revision() : 0;
    const AuthoringMode authoring_mode = m_authoring_mode;
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
    if (authoring_available) {
        for (const auto& schema : m_authoring_registry.tool_schemas()) {
            request.tools.push_back(schema);
        }
    }
    request.instructions = build_editor_ai_request_instructions({
        image_generation_requested,
        authoring_available,
        authoring_mode == AuthoringMode::ConfirmBeforeApply,
        viewport_available,
        !request.images.empty(),
        previous_receipt,
        attached_contexts,
        request_context->value("project_overview", nlohmann::json::object())
    });
    request.project_root = project_root();
    request.codex_executable = m_codex_executable;
    request.persist_thread = using_codex;
    request.max_tool_rounds = 12;
    request.max_tool_failures = 3;
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
        m_messages.push_back({"user", prompt});
        m_streamed_response.clear();
        m_reasoning_summary.clear();
        m_request_error.clear();
        m_active_tool.clear();
        m_live_activity = "Connecting to Codex";
        m_activity_entries.clear();
        m_activity_entries.push_back({"Request sent", model, 0});
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
        "[AI AUDIT] request={} phase=start backend={} model={} effort={} mode={} scene={} "
        "baseline_revision={} history_messages={} history_chars={} context_chars={} tools={} "
        "tool_schema_chars={} instructions_chars={} prompt_chars={} images={} thread={} persistent={} "
        "compact_before_turn={} compact_source={} previous_input_tokens={}",
        request_id,
        m_backend == ProviderBackend::CodexSubscription ? "codex_subscription" : "openai_api",
        model,
        reasoning_effort,
        authoring_mode == AuthoringMode::AutonomousSafe ? "autonomous_safe" : "confirm_before_apply",
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
    request.execute_tool = [
        this,
        request_context,
        proposal_actions,
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
            return audited(execute_live_editor_tool(
                name, arguments, active_scene, baseline_revision
            ));
        }
        return audited(m_tool_registry.execute_snapshot(name, arguments, *request_context));
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
        authoring_mode,
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
                const auto add_activity = [this, elapsed](
                    std::string label,
                    std::string detail
                ) {
                    constexpr std::size_t MAX_ACTIVITY_ENTRIES = 80;
                    if (m_activity_entries.size() >= MAX_ACTIVITY_ENTRIES) {
                        m_activity_entries.erase(m_activity_entries.begin());
                    }
                    m_activity_entries.push_back({
                        std::move(label),
                        std::move(detail),
                        static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed))
                    });
                };
                if (event.type == EditorAiProviderEventType::Started) {
                    m_live_activity = "Codex started";
                    add_activity("Codex started", "Provider connected");
                } else if (event.type == EditorAiProviderEventType::TextDelta) {
                    m_streamed_response += event.text;
                    m_live_activity = "Writing response";
                    m_scroll_chat_to_bottom = true;
                } else if (event.type == EditorAiProviderEventType::ReasoningSummaryDelta) {
                    m_reasoning_summary += event.text;
                    m_live_activity = "Reasoning";
                    m_scroll_chat_to_bottom = true;
                } else if (event.type == EditorAiProviderEventType::ItemStarted) {
                    const std::string label = activity_item_label(event.item_type);
                    m_live_activity = label;
                    add_activity(label, "Started");
                    m_scroll_chat_to_bottom = true;
                } else if (event.type == EditorAiProviderEventType::ItemCompleted) {
                    const std::string label = activity_item_label(event.item_type);
                    add_activity(
                        label,
                        event.item_status.empty() ? "Completed" : event.item_status
                    );
                    m_live_activity = "Codex is working";
                } else if (event.type == EditorAiProviderEventType::ToolStarted) {
                    m_active_tool = event.tool_name;
                    m_live_activity = "Using " + event.tool_name;
                    add_activity("Tool", event.tool_name + " started");
                } else if (event.type == EditorAiProviderEventType::ToolFinished) {
                    m_active_tool.clear();
                    m_live_activity = "Codex is working";
                    add_activity("Tool", event.tool_name + " " + event.text);
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
        const char* status = result.status == EditorAiProviderStatus::Completed
            ? "completed"
            : (result.status == EditorAiProviderStatus::Cancelled ? "cancelled" : "failed");
        m_live_activity = result.status == EditorAiProviderStatus::Completed
            ? "Completed"
            : (result.status == EditorAiProviderStatus::Cancelled ? "Cancelled" : "Failed");
        if (result.context_compacted) {
            m_ui_feedback = "Codex context was compacted before this message.";
        }
        m_activity_entries.push_back({
            m_live_activity,
            result.error.empty() ? "Turn finished" : result.error,
            m_last_request_duration_ms
        });
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
            m_messages.push_back({"assistant", m_streamed_response});
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
                    .auto_apply = authoring_mode == AuthoringMode::AutonomousSafe
                };
                m_authoring_feedback = m_pending_proposal->auto_apply
                    ? "Proposal validated by the tool boundary; applying on the editor thread..."
                    : "Proposal ready for review. The scene has not changed yet.";
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
        } else {
            remove_cached_texture_previews(request_context->at("generated_texture_previews"));
            m_request_error = result.error;
            if (!proposal_actions->empty()) {
                m_authoring_feedback = "Failed proposal discarded without changing the scene.";
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

    bool auto_apply_proposal = false;
    {
        std::lock_guard lock(m_request_mutex);
        auto_apply_proposal = !m_request_running && m_pending_proposal &&
            m_pending_proposal->auto_apply;
    }
    if (auto_apply_proposal) apply_pending_proposal();

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
        ImGui::Separator();
        if (ImGui::Button("Reset defaults")) {
            m_backend = ProviderBackend::CodexSubscription;
            m_reasoning_effort = ReasoningEffort::Low;
            m_authoring_mode = AuthoringMode::ConfirmBeforeApply;
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
        const std::string phase = live_activity.empty() ? "Activity" : live_activity;
        char header[160];
        std::snprintf(
            header,
            sizeof(header),
            "%s  %.1fs##AIActivity",
            phase.c_str(),
            static_cast<double>(activity_elapsed_ms) / 1000.0
        );
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.17f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.21f, 0.28f, 1.0f));
        const bool open = ImGui::CollapsingHeader(
            header,
            ImGuiTreeNodeFlags_DefaultOpen
        );
        ImGui::PopStyleColor(2);
        if (!open) return;
        ImGui::Indent(8.0f);
        if (!reasoning_summary.empty()) {
            ImGui::TextColored(
                ImVec4(0.58f, 0.62f, 0.74f, 1.0f),
                "Reasoning summary"
            );
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextUnformatted(reasoning_summary.c_str());
            ImGui::PopTextWrapPos();
            if (!activity_entries.empty()) ImGui::Spacing();
        }
        for (const AiActivityEntry& entry : activity_entries) {
            ImGui::TextDisabled("%5.1fs", static_cast<double>(entry.elapsed_ms) / 1000.0);
            ImGui::SameLine();
            ImGui::TextWrapped(
                "%s%s%s",
                entry.label.c_str(),
                entry.detail.empty() ? "" : " - ",
                entry.detail.c_str()
            );
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
    ImGui::BeginDisabled(running || pending_proposal.has_value());
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
        if (m_authoring_mode == AuthoringMode::AutonomousSafe) {
            ImGui::TextColored(
                ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline),
                "Autonomous safe"
            );
        } else {
            ImGui::TextDisabled("Confirm changes");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Change authoring approval mode");
        if (ImGui::IsItemClicked() && !running && !pending_proposal.has_value()) {
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
        ImGui::TextColored(ImVec4(0.95f, 0.36f, 0.32f, 1.0f), "%s", request_error.c_str());
    }
    if (!running && total_tokens > 0) {
        ImGui::TextDisabled(
            "Last request: %.2fs | %zu tools",
            static_cast<double>(request_duration_ms) / 1000.0,
            tool_calls
        );
        ImGui::TextDisabled(
            "Usage: %zu input | %zu output | %zu total tokens",
            input_tokens,
            output_tokens,
            total_tokens
        );
        if (using_codex && !codex_thread_id.empty() &&
            (compact_before_next_request || input_tokens >= AUTO_COMPACT_INPUT_TOKEN_THRESHOLD)) {
            ImGui::TextColored(
                ImVec4(0.76f, 0.68f, 0.36f, 1.0f),
                "Context compaction scheduled for the next message"
            );
        }
    }
    ImGui::TextDisabled("Audit logs: Console + engine.log");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Request IDs and session metadata only; no prompts or credentials");
    }
    if (!ui_feedback.empty()) {
        ImGui::TextColored(ImVec4(0.48f, 0.66f, 0.95f, 1.0f), "%s", ui_feedback.c_str());
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
        for (const EditorAiAuthoringAction& action : pending_proposal->actions) {
            ImGui::BulletText("%s", action.summary.c_str());
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
            authoring_feedback.starts_with("Failed");
        ImGui::TextColored(
            failed ? ImVec4(0.95f, 0.36f, 0.32f, 1.0f)
                   : ImVec4(0.35f, 0.78f, 0.52f, 1.0f),
            "%s",
            authoring_feedback.c_str()
        );
    }
    if (!application_receipt.empty() && ImGui::CollapsingHeader("Last application receipt")) {
        const std::string receipt_text = application_receipt.dump(2);
        if (ImGui::Button("Copy receipt JSON")) {
            copy_to_clipboard(receipt_text, "Application receipt copied.");
        }
        ImGui::BeginChild("AIApplicationReceipt", ImVec2(0.0f, 150.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(receipt_text.c_str());
        ImGui::EndChild();
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
        ImGui::BeginChild("AIContextPreview", ImVec2(0.0f, 180.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(context.c_str());
        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace Engine
