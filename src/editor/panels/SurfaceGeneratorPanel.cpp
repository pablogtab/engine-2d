#include "SurfaceGeneratorPanel.hpp"

#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include "../../renderer/Texture.hpp"
#include "../inspector/AssetDropResolver.hpp"
#include "../ui/EditorUi.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <random>

namespace Engine {

namespace {

constexpr int PROFILE_VERSION = 1;

void copy_path(std::array<char, 512>& destination, const std::string& source) {
    std::snprintf(destination.data(), destination.size(), "%s", source.c_str());
}

bool is_path_inside(const std::filesystem::path& child, const std::filesystem::path& parent) {
    const auto relative = child.lexically_normal().lexically_relative(parent.lexically_normal());
    return !relative.empty() && relative != "." && relative.begin()->string() != "..";
}

} // namespace

SurfaceGeneratorPanel::SurfaceGeneratorPanel(SDL_Renderer* renderer)
    : m_renderer(renderer) {
    is_open = false;
    copy_path(m_output_path, "Assets/Textures/Generated/surface.png");
}

SurfaceGeneratorPanel::~SurfaceGeneratorPanel() {
    if (m_control) m_control->cancel.store(true);
    if (m_future.valid()) m_future.wait();
}

void SurfaceGeneratorPanel::on_render_resources_invalidated() {
    m_preview.reset();
}

bool SurfaceGeneratorPanel::build_settings(
    SurfaceGenerationSettings& settings,
    std::string& error
) const {
    error.clear();
    const AssetDatabase& database = AssetDatabase::get();
    settings = {};
    settings.width = m_width;
    settings.height = m_height;
    settings.patch_size = m_patch_size;
    settings.overlap = m_overlap;
    settings.candidate_count = m_candidate_count;
    settings.repetition_radius = m_repetition_radius;
    settings.seed = m_seed;
    settings.luminance_normalization = m_luminance_normalization;

    for (int index = 0; index < m_source_count; ++index) {
        const std::string entered = m_sources[index].path.data();
        if (entered.empty()) continue;
        const AssetRecord* record = database.find_by_path(entered);
        if (!record || record->status != AssetStatus::Ready || record->type != AssetType::Texture) {
            error = "Source " + std::to_string(index + 1) + " is not an imported texture asset";
            return false;
        }
        const std::string resolved = database.resolve(record->handle);
        if (resolved.empty()) {
            error = "Source " + std::to_string(index + 1) + " could not be resolved";
            return false;
        }
        settings.sources.push_back({
            resolved,
            m_sources[index].weight,
            m_sources[index].flip_x,
            m_sources[index].flip_y,
            m_sources[index].rotate_quarter_turns
        });
    }
    if (settings.sources.empty()) {
        error = "Add at least one texture from the Asset Browser";
        return false;
    }
    return true;
}

void SurfaceGeneratorPanel::start_generation() {
    SurfaceGenerationSettings settings;
    if (!build_settings(settings, m_error)) return;
    if (!SurfaceGenerationService::validate_settings(settings, m_error)) return;
    m_generated_settings = settings;
    m_generated_sources = m_sources;
    m_generated_source_count = m_source_count;
    m_preview.reset();
    m_result = {};
    m_status = "Generating surface...";
    m_control = std::make_shared<SurfaceGenerationControl>();
    const auto control = m_control;
    m_running = true;
    m_future = std::async(std::launch::async, [settings = std::move(settings), control]() {
        try {
            return SurfaceGenerationService::generate(settings, control.get());
        } catch (const std::exception& exception) {
            SurfaceGenerationResult result;
            result.error = exception.what();
            return result;
        }
    });
}

void SurfaceGeneratorPanel::poll_generation() {
    if (!m_running || !m_future.valid() ||
        m_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
    m_result = m_future.get();
    m_running = false;
    if (m_result.success) {
        m_error.clear();
        m_status = "Preview ready";
        rebuild_preview();
    } else if (m_result.cancelled) {
        m_status = "Generation cancelled";
        m_error.clear();
    } else {
        m_status.clear();
        m_error = m_result.error;
    }
}

void SurfaceGeneratorPanel::rebuild_preview() {
    m_preview.reset();
    if (!m_renderer || !m_result.success) return;
    std::vector<std::uint32_t> pixels(
        static_cast<std::size_t>(m_result.width) * m_result.height
    );
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::size_t offset = index * 4;
        pixels[index] =
            (static_cast<std::uint32_t>(m_result.rgba[offset]) << 24) |
            (static_cast<std::uint32_t>(m_result.rgba[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(m_result.rgba[offset + 2]) << 8) |
            m_result.rgba[offset + 3];
    }
    m_preview = std::make_unique<Texture>(m_renderer, m_result.width, m_result.height, pixels.data());
    if (auto* texture = m_preview->get_sdl_texture()) {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    } else {
        m_error = "Could not upload the generated preview to the renderer";
    }
}

std::filesystem::path SurfaceGeneratorPanel::resolved_output_path(std::string& error) const {
    error.clear();
    const std::filesystem::path project_root = AssetDatabase::get().project_root();
    const std::filesystem::path entered(m_output_path.data());
    const std::filesystem::path lexical = entered.is_absolute()
        ? entered.lexically_normal()
        : (project_root / entered).lexically_normal();
    std::error_code path_error;
    const auto resolved = std::filesystem::weakly_canonical(lexical, path_error);
    if (path_error) {
        error = "Could not resolve the output directory";
        return {};
    }
    const auto assets_root = std::filesystem::weakly_canonical(project_root / "Assets", path_error);
    if (path_error) {
        error = "Could not resolve the project's Assets directory";
        return {};
    }
    if (!is_path_inside(resolved, assets_root)) {
        error = "Output must be located below the project's Assets directory";
        return {};
    }
    if (resolved.extension() != ".png") {
        error = "Output must use the .png extension";
        return {};
    }
    for (const auto& source : m_generated_settings.sources) {
        const auto source_path = std::filesystem::weakly_canonical(source.path, path_error);
        if (!path_error && source_path == resolved) {
            error = "Output cannot replace a source texture; choose a different destination";
            return {};
        }
        path_error.clear();
    }
    return resolved;
}

std::filesystem::path SurfaceGeneratorPanel::profile_path_for(
    const std::filesystem::path& output
) const {
    std::filesystem::path profile = output;
    profile.replace_extension(".surface.json");
    return profile;
}

void SurfaceGeneratorPanel::save_profile(const std::filesystem::path& output_path) {
    const auto& settings = m_generated_settings;
    nlohmann::json sources = nlohmann::json::array();
    const auto& database = AssetDatabase::get();
    for (int index = 0; index < m_generated_source_count; ++index) {
        const auto& source = m_generated_sources[index];
        const std::string path = source.path.data();
        if (path.empty()) continue;
        const AssetRecord* record = database.find_by_path(path);
        sources.push_back({
            {"handle", record ? record->handle.to_string() : ""},
            {"path", path},
            {"weight", source.weight},
            {"flip_x", source.flip_x},
            {"flip_y", source.flip_y},
            {"quarter_turns", source.rotate_quarter_turns}
        });
    }
    const nlohmann::json profile{
        {"version", PROFILE_VERSION},
        {"output", std::filesystem::relative(output_path, database.project_root()).generic_string()},
        {"width", settings.width}, {"height", settings.height},
        {"patch_size", settings.patch_size}, {"overlap", settings.overlap},
        {"candidate_count", settings.candidate_count},
        {"repetition_radius", settings.repetition_radius},
        {"seed", settings.seed},
        {"luminance_normalization", settings.luminance_normalization},
        {"sources", std::move(sources)}
    };
    const auto profile_path = profile_path_for(output_path);
    if (!FileSystem::write_text_atomically(profile_path.string(), profile.dump(2) + '\n')) {
        m_error = "PNG was saved, but its generation profile could not be written";
        return;
    }
    if (!AssetDatabase::get().import_asset(profile_path)) {
        m_error = "PNG and profile were saved, but the profile could not be imported";
    }
}

void SurfaceGeneratorPanel::save_result() {
    m_error.clear();
    std::string path_error;
    const auto output = resolved_output_path(path_error);
    if (output.empty()) {
        m_error = path_error;
        return;
    }
    if (!SurfaceGenerationService::save_png_atomic(output, m_result, m_error)) return;
    const auto imported = AssetDatabase::get().import_asset(output);
    if (!imported) {
        m_error = "PNG was written, but the Asset Database could not import it";
        return;
    }
    save_profile(output);
    if (m_error.empty()) {
        m_status = "Saved and imported " +
            std::filesystem::relative(output, AssetDatabase::get().project_root()).generic_string();
    }
}

void SurfaceGeneratorPanel::load_profile() {
    std::string path_error;
    const auto output = resolved_output_path(path_error);
    if (output.empty()) {
        m_error = path_error;
        return;
    }
    const auto profile_path = profile_path_for(output);
    const std::string text = FileSystem::read_text(profile_path.string());
    const nlohmann::json profile = nlohmann::json::parse(text, nullptr, false);
    if (profile.is_discarded() || !profile.is_object() ||
        !profile.contains("sources") || !profile["sources"].is_array() ||
        profile["sources"].empty() || profile["sources"].size() > m_sources.size()) {
        m_error = "No valid generation profile exists for this output path";
        return;
    }
    try {
        if (profile.at("version").get<int>() != PROFILE_VERSION) {
            m_error = "Unsupported generation profile version";
            return;
        }
        SurfaceGenerationSettings settings;
        settings.width = profile.at("width").get<int>();
        settings.height = profile.at("height").get<int>();
        settings.patch_size = profile.at("patch_size").get<int>();
        settings.overlap = profile.at("overlap").get<int>();
        settings.candidate_count = profile.at("candidate_count").get<int>();
        settings.repetition_radius = profile.at("repetition_radius").get<int>();
        settings.seed = profile.at("seed").get<std::uint32_t>();
        settings.luminance_normalization = profile.at("luminance_normalization").get<float>();
        const int source_count = static_cast<int>(profile["sources"].size());
        std::array<SourceRow, 6> rows{};
        for (int index = 0; index < source_count; ++index) {
            const auto& source = profile["sources"][index];
            std::string source_path = source.value("path", std::string{});
            if (const auto handle = AssetHandle::from_string(source.value("handle", std::string{}))) {
                if (const AssetRecord* record = AssetDatabase::get().find(*handle);
                    record && record->status == AssetStatus::Ready && record->type == AssetType::Texture) {
                    source_path = record->source_path;
                }
            }
            if (source_path.size() >= rows[index].path.size()) {
                m_error = "Source path is too long";
                return;
            }
            copy_path(rows[index].path, source_path);
            rows[index].weight = source.value("weight", 1.0f);
            rows[index].flip_x = source.value("flip_x", true);
            rows[index].flip_y = source.value("flip_y", true);
            rows[index].rotate_quarter_turns = source.value("quarter_turns", false);
            settings.sources.push_back({source_path, rows[index].weight,
                rows[index].flip_x, rows[index].flip_y, rows[index].rotate_quarter_turns});
        }
        if (!SurfaceGenerationService::validate_settings(settings, m_error)) return;
        m_sources = rows;
        m_source_count = source_count;
        m_width = settings.width;
        m_height = settings.height;
        m_patch_size = settings.patch_size;
        m_overlap = settings.overlap;
        m_candidate_count = settings.candidate_count;
        m_repetition_radius = settings.repetition_radius;
        m_seed = settings.seed;
        m_luminance_normalization = settings.luminance_normalization;
        m_result = {};
        m_preview.reset();
        m_error.clear();
        m_status = "Generation profile loaded";
    } catch (const nlohmann::json::exception&) {
        m_error = "Invalid generation profile: missing fields or incorrect field types";
    }
}

void SurfaceGeneratorPanel::on_gui() {
    poll_generation();
    if (!is_open) return;
    if (m_result.success && !m_preview) rebuild_preview();

    ImGui::SetNextWindowSize(ImVec2(920.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::End();
        return;
    }

    const float controls_width = 390.0f;
    ImGui::BeginChild("SurfaceGeneratorControls", ImVec2(controls_width, 0.0f), true);
    ImGui::BeginDisabled(m_running);
    ImGui::TextUnformatted("SOURCE TEXTURES");
    ImGui::TextDisabled("Use neutral, compatible textures without baked lighting.");
    for (int index = 0; index < m_source_count; ++index) {
        ImGui::PushID(index);
        ImGui::SeparatorText(("Source " + std::to_string(index + 1)).c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##source_path", m_sources[index].path.data(), m_sources[index].path.size());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetPayload)) {
                if (payload->Data && payload->DataSize > 0) {
                    const char* begin = static_cast<const char*>(payload->Data);
                    const char* end = std::find(begin, begin + payload->DataSize, '\0');
                    const std::string path(begin, end);
                    const std::array accepted_types{AssetType::Texture};
                    const auto drop = resolve_asset_drop(AssetDatabase::get(), path, accepted_types);
                    if (drop.accepted() && drop.record->source_path.size() < m_sources[index].path.size()) {
                        copy_path(m_sources[index].path, drop.record->source_path);
                        m_error.clear();
                    } else {
                        m_error = drop.accepted() ? "Source path is too long" : asset_drop_status_message(drop.status);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SliderFloat("Weight", &m_sources[index].weight, 0.1f, 10.0f, "%.1f");
        ImGui::Checkbox("Flip X", &m_sources[index].flip_x);
        ImGui::SameLine();
        ImGui::Checkbox("Flip Y", &m_sources[index].flip_y);
        ImGui::SameLine();
        ImGui::Checkbox("Rotate 90", &m_sources[index].rotate_quarter_turns);
        if (m_source_count > 1 && ImGui::Button("Remove source")) {
            for (int move = index; move + 1 < m_source_count; ++move) m_sources[move] = m_sources[move + 1];
            m_sources[m_source_count - 1] = {};
            --m_source_count;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (m_source_count < static_cast<int>(m_sources.size()) && ImGui::Button("Add source")) {
        ++m_source_count;
    }

    ImGui::SeparatorText("OUTPUT");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##output_path", m_output_path.data(), m_output_path.size());
    ImGui::TextDisabled("PNG destination below Assets/");
    ImGui::SetNextItemWidth(130.0f);
    ImGui::InputInt("Width", &m_width);
    ImGui::SetNextItemWidth(130.0f);
    ImGui::InputInt("Height", &m_height);

    ImGui::SeparatorText("SYNTHESIS");
    ImGui::SliderInt("Patch size", &m_patch_size, 16, 512);
    ImGui::SliderInt("Overlap", &m_overlap, 1, std::max(1, m_patch_size - 1));
    ImGui::SliderInt("Candidates", &m_candidate_count, 1, 128);
    ImGui::SliderInt("Repeat radius", &m_repetition_radius, 0, 8);
    ImGui::SliderFloat("Luminance match", &m_luminance_normalization, 0.0f, 1.0f, "%.2f");
    ImGui::InputScalar("Seed", ImGuiDataType_U32, &m_seed);
    ImGui::SameLine();
    if (ImGui::Button("New seed")) m_seed = std::random_device{}();

    if (ImGui::Button("Generate Preview", ImVec2(-1.0f, 30.0f))) start_generation();
    ImGui::EndDisabled();
    if (m_running && m_control) {
        const float progress = m_control->progress.load();
        ImGui::ProgressBar(progress, ImVec2(-70.0f, 0.0f));
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) m_control->cancel.store(true);
    }

    ImGui::BeginDisabled(!m_result.success || m_running);
    if (ImGui::Button("Save to Assets", ImVec2(-1.0f, 28.0f))) {
        std::string path_error;
        const auto output = resolved_output_path(path_error);
        if (output.empty()) {
            m_error = path_error;
        } else if (std::filesystem::exists(output) || std::filesystem::exists(profile_path_for(output))) {
            m_confirm_replace = true;
        } else {
            save_result();
        }
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(m_running);
    if (ImGui::Button("Load saved settings", ImVec2(-1.0f, 0.0f))) load_profile();
    ImGui::EndDisabled();

    if (!m_status.empty()) ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s", m_status.c_str());
    if (!m_error.empty()) ImGui::TextWrapped("Error: %s", m_error.c_str());
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("SurfaceGeneratorPreview", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted("PREVIEW");
    if (m_result.success) {
        ImGui::Text("%d x %d  |  Hash %016llx", m_result.width, m_result.height,
            static_cast<unsigned long long>(m_result.content_hash));
        ImGui::Text("Seam error %.4f  |  Repetition %.3f",
            m_result.metrics.average_overlap_error, m_result.metrics.repetition_score);
        ImGui::TextDisabled("Saved settings always describe this preview.");
        for (std::size_t index = 0; index < m_result.metrics.source_usage.size(); ++index) {
            ImGui::Text("Source %zu: %.1f%% of patches", index + 1,
                m_result.metrics.source_usage[index] * 100.0f);
        }
        for (const std::string& warning : m_result.warnings) {
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.20f, 1.0f), "%s", warning.c_str());
        }
        ImGui::SliderFloat("Preview zoom", &m_preview_zoom, 0.1f, 2.0f, "%.2fx");
        if (m_preview && m_preview->get_sdl_texture()) {
            ImGui::Image(
                reinterpret_cast<ImTextureID>(m_preview->get_sdl_texture()),
                ImVec2(m_result.width * m_preview_zoom, m_result.height * m_preview_zoom)
            );
        }
    } else {
        EditorUi::empty_state(
            EditorIcon::Sprite,
            "No generated surface",
            "Add source textures and generate a preview"
        );
    }
    ImGui::EndChild();

    if (m_confirm_replace) {
        ImGui::OpenPopup("Replace Generated Surface?");
        m_confirm_replace = false;
    }
    if (ImGui::BeginPopupModal(
            "Replace Generated Surface?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The existing PNG and profile will be replaced.");
        if (ImGui::Button("Replace", ImVec2(120.0f, 0.0f))) {
            m_confirm_replace = false;
            save_result();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            m_confirm_replace = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace Engine
