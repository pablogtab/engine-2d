#include "StatsPanel.hpp"
#include "../../core/Time.hpp"
#include "../../debug/FrameProfiler.hpp"
#include <imgui.h>
#include <filesystem>

namespace Engine {

StatsPanel::StatsPanel(FrameProfiler* profiler, std::string project_root)
    : m_profiler(profiler), m_project_root(std::move(project_root)) {
    for (float& val : m_frametime_history) {
        val = 16.6f;
    }
}

void StatsPanel::on_update(float delta_time) {
    m_accumulated_time += delta_time;
    if (m_accumulated_time >= 0.05f) { // Update plot at ~20Hz
        m_accumulated_time = 0.0f;
        m_frametime_history[m_history_offset] = delta_time * 1000.0f;
        m_history_offset = (m_history_offset + 1) % 60;
    }
}

void StatsPanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(340, 380), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        // --- 1. Performance Overview ---
        ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "PERFORMANCE OVERVIEW");
        ImGui::Separator();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("Framerate: %.1f FPS", io.Framerate);
        ImGui::Text("Frametime: %.2f ms", 1000.0f / (io.Framerate > 0.0f ? io.Framerate : 60.0f));

        // Frametime Plot
        ImGui::PlotLines("##frametime", m_frametime_history, 60, m_history_offset,
                         nullptr, 0.0f, 33.3f, ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));

        ImGui::Spacing();

        if (m_profiler) {
            ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "FRAME PROFILER");
            ImGui::Separator();
            if (ImGui::Button(m_profiler->enabled() ? "Disable capture" : "Enable capture")) {
                m_profiler->set_enabled(!m_profiler->enabled());
            }
            ImGui::SameLine(); ImGui::RadioButton("60", &m_history_window, 60);
            ImGui::SameLine(); ImGui::RadioButton("120", &m_history_window, 120);
            ImGui::SameLine(); ImGui::RadioButton("300", &m_history_window, 300);
            const ProfileStatistics stats = m_profiler->statistics(static_cast<std::size_t>(m_history_window));
            ImGui::Text("avg %.2f | min %.2f | max %.2f ms", stats.average, stats.minimum, stats.maximum);
            ImGui::Text("p50 %.2f | p95 %.2f | p99 %.2f ms", stats.p50, stats.p95, stats.p99);
            ImGui::TextDisabled("GPU timings: Unavailable");
            if (ImGui::Button("Export capture") && !m_project_root.empty()) {
                std::string error;
                const auto path = std::filesystem::path(m_project_root) / "Library" / "ProfilerCaptures" / "latest.json";
                if (!m_profiler->export_json(path.string(), error)) {
                    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", error.c_str());
                }
            }
            if (!m_profiler->frames().empty()) {
                const ProfileFrame& frame = m_profiler->frames().back();
                if (ImGui::TreeNode("Latest frame scopes")) {
                    for (const auto& scope : frame.scopes) ImGui::BulletText("%s: %.2f ms", scope.name.c_str(), scope.milliseconds);
                    ImGui::TreePop();
                }
            }
            ImGui::Spacing();
        }

        // --- 2. Global Time Scale ---
        ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "SIMULATION TIME SCALE");
        ImGui::Separator();

        float time_scale = Time::get_time_scale();
        if (ImGui::SliderFloat("Time Scale", &time_scale, 0.0f, 2.0f, "%.2fx")) {
            Time::set_time_scale(time_scale);
        }

        if (ImGui::Button("Pause (0x)")) {
            Time::set_time_scale(0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Normal (1x)")) {
            Time::set_time_scale(1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Slow-Mo (0.3x)")) {
            Time::set_time_scale(0.3f);
        }

        ImGui::Spacing();

        // --- 3. Quick Shortcuts Cheat Sheet ---
        ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "DEV SHORTCUTS");
        ImGui::Separator();
        ImGui::BulletText("F1 / ~ : Toggle Editor Overlay");
        ImGui::BulletText("TAB    : Switch Next Scene");
        ImGui::BulletText("F5     : QuickSave (Binary+CRC32)");
        ImGui::BulletText("F9     : QuickLoad");
        ImGui::BulletText("R      : Reset Active Level");
    }
    ImGui::End();
}

} // namespace Engine
