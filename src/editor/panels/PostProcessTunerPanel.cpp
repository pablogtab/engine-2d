#include "PostProcessTunerPanel.hpp"
#include "../../core/FileSystem.hpp"
#include <filesystem>
#include <imgui.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Engine {

PostProcessTunerPanel::PostProcessTunerPanel(PostProcessPipeline* pipeline)
    : m_pipeline(pipeline) {}

void PostProcessTunerPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(340, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "Visual Post-Processing Calibrator");
        ImGui::Separator();

        // --- Presets Bar ---
        ImGui::TextDisabled("Quick Presets:");
        if (ImGui::Button("Cinematic")) {
            m_bloom_intensity = 1.4f;
            m_vignette_radius = 0.75f;
            m_film_grain_intensity = 0.20f;
            m_contrast = 1.15f;
            m_saturation = 1.25f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Retro CRT")) {
            m_scanlines_intensity = 0.55f;
            m_chromatic_offset = 5.0f;
            m_film_grain_intensity = 0.30f;
            m_contrast = 1.20f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clean")) {
            m_bloom_intensity = 0.0f;
            m_scanlines_intensity = 0.0f;
            m_film_grain_intensity = 0.0f;
            m_chromatic_offset = 0.0f;
            m_vignette_radius = 1.0f;
        }

        ImGui::Spacing();

        // --- Bloom ---
        if (ImGui::CollapsingHeader("Bloom (Glow)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Threshold##bloom", &m_bloom_threshold, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Intensity##bloom", &m_bloom_intensity, 0.0f, 3.0f, "%.2f");
        }

        // --- Chromatic Aberration ---
        if (ImGui::CollapsingHeader("Chromatic Aberration", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Pixel Offset##chroma", &m_chromatic_offset, 0.0f, 15.0f, "%.1f px");
        }

        // --- Vignette ---
        if (ImGui::CollapsingHeader("Vignette (Edge Shadow)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Radius##vignette", &m_vignette_radius, 0.1f, 1.5f, "%.2f");
            ImGui::SliderFloat("Smoothness##vignette", &m_vignette_smoothness, 0.05f, 1.0f, "%.2f");
            ImGui::ColorEdit3("Color##vignette", m_vignette_color);
        }

        // --- Retro Effects ---
        if (ImGui::CollapsingHeader("Film Grain & Scanlines")) {
            ImGui::SliderFloat("Film Grain", &m_film_grain_intensity, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("CRT Scanlines", &m_scanlines_intensity, 0.0f, 1.0f, "%.2f");
        }

        // --- Color Grading ---
        if (ImGui::CollapsingHeader("Color Grading", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Exposure", &m_exposure, 0.2f, 2.5f, "%.2f");
            ImGui::SliderFloat("Contrast", &m_contrast, 0.5f, 2.0f, "%.2f");
            ImGui::SliderFloat("Saturation", &m_saturation, 0.0f, 2.5f, "%.2f");
        }

        ImGui::Separator();

        // --- Save / Load Preset JSON ---
        if (ImGui::Button("Save Preset (JSON)", ImVec2(150, 24))) {
            std::filesystem::create_directories("assets/presets");
            json p;
            p["bloom_threshold"] = m_bloom_threshold;
            p["bloom_intensity"] = m_bloom_intensity;
            p["chromatic_offset"] = m_chromatic_offset;
            p["vignette_radius"] = m_vignette_radius;
            p["vignette_smoothness"] = m_vignette_smoothness;
            p["film_grain"] = m_film_grain_intensity;
            p["scanlines"] = m_scanlines_intensity;
            p["exposure"] = m_exposure;
            p["contrast"] = m_contrast;
            p["saturation"] = m_saturation;
            FileSystem::write_text("assets/presets/custom_postfx.json", p.dump(4));
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Preset", ImVec2(120, 24))) {
            std::string text = FileSystem::read_text("assets/presets/custom_postfx.json");
            if (!text.empty()) {
                try {
                    json p = json::parse(text);
                    if (p.contains("bloom_threshold")) m_bloom_threshold = p["bloom_threshold"];
                    if (p.contains("bloom_intensity")) m_bloom_intensity = p["bloom_intensity"];
                    if (p.contains("chromatic_offset")) m_chromatic_offset = p["chromatic_offset"];
                    if (p.contains("vignette_radius")) m_vignette_radius = p["vignette_radius"];
                    if (p.contains("vignette_smoothness")) m_vignette_smoothness = p["vignette_smoothness"];
                    if (p.contains("film_grain")) m_film_grain_intensity = p["film_grain"];
                    if (p.contains("scanlines")) m_scanlines_intensity = p["scanlines"];
                    if (p.contains("exposure")) m_exposure = p["exposure"];
                    if (p.contains("contrast")) m_contrast = p["contrast"];
                    if (p.contains("saturation")) m_saturation = p["saturation"];
                } catch (...) {}
            }
        }
    }
    ImGui::End();
}

} // namespace Engine
