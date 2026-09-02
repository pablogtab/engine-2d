#include "StatsPanel.hpp"
#include "../ui/EditorUi.hpp"
#include "../../core/Time.hpp"
#include "../../debug/FrameProfiler.hpp"
#include "../../debug/StressScenarios.hpp"
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <unordered_map>
#include <vector>

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
        EditorUi::area_heading(EditorIcon::Zap, "Performance", "Frame and subsystem metrics");

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

            const auto& frames = m_profiler->frames();
            if (!frames.empty()) {
                const std::size_t visible_count = std::min<std::size_t>(
                    static_cast<std::size_t>(m_history_window), frames.size()
                );
                const std::size_t first = frames.size() - visible_count;
                std::vector<float> timeline;
                timeline.reserve(visible_count);
                float timeline_max = 16.67f;
                for (std::size_t index = first; index < frames.size(); ++index) {
                    timeline.push_back(frames[index].frame_milliseconds);
                    timeline_max = std::max(timeline_max, frames[index].frame_milliseconds);
                }
                ImGui::PlotLines(
                    "Frame timeline", timeline.data(), static_cast<int>(timeline.size()), 0,
                    nullptr, 0.0f, timeline_max * 1.1f,
                    ImVec2(ImGui::GetContentRegionAvail().x, 90.0f)
                );
                ImGui::Checkbox("Follow latest", &m_follow_latest_frame);
                int visible_frame = m_follow_latest_frame
                    ? static_cast<int>(visible_count - 1)
                    : std::clamp(m_selected_frame, 0, static_cast<int>(visible_count - 1));
                if (ImGui::SliderInt("Selected frame", &visible_frame, 0, static_cast<int>(visible_count - 1))) {
                    m_follow_latest_frame = false;
                    m_selected_frame = visible_frame;
                }
                if (m_follow_latest_frame) m_selected_frame = static_cast<int>(visible_count - 1);
                const ProfileFrame& selected = frames[first + static_cast<std::size_t>(m_selected_frame)];
                ImGui::Text("Frame #%llu: %.2f ms", static_cast<unsigned long long>(selected.index), selected.frame_milliseconds);

                std::unordered_map<std::string, float> totals;
                for (const auto& scope : selected.scopes) totals[scope.name] += scope.milliseconds;
                std::vector<std::pair<std::string, float>> sorted(totals.begin(), totals.end());
                std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
                    return left.second > right.second;
                });
                if (ImGui::BeginTable("ProfilerScopes", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Scope");
                    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableSetupColumn("Frame share");
                    ImGui::TableHeadersRow();
                    for (const auto& [name, milliseconds] : sorted) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name.c_str());
                        ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f ms", milliseconds);
                        ImGui::TableSetColumnIndex(2);
                        const float share = selected.frame_milliseconds > 0.0f
                            ? std::clamp(milliseconds / selected.frame_milliseconds, 0.0f, 1.0f) : 0.0f;
                        ImGui::ProgressBar(share, ImVec2(-1.0f, 0.0f));
                    }
                    ImGui::EndTable();
                }
                if (!selected.counters.empty() && ImGui::TreeNode("Counters")) {
                    std::vector<std::pair<std::string, std::uint64_t>> counters(
                        selected.counters.begin(), selected.counters.end()
                    );
                    std::sort(counters.begin(), counters.end());
                    for (const auto& [name, value] : counters) {
                        ImGui::Text("%s: %llu", name.c_str(), static_cast<unsigned long long>(value));
                    }
                    ImGui::TreePop();
                }
            }
            if (ImGui::Button("Export capture") && !m_project_root.empty()) {
                std::string error;
                const auto path = std::filesystem::path(m_project_root) / "Library" / "ProfilerCaptures" / "latest.json";
                if (!m_profiler->export_json(path.string(), error)) {
                    m_export_feedback = "Export failed: " + error;
                } else {
                    m_export_feedback = "Saved: " + path.string();
                }
            }
            if (!m_export_feedback.empty()) {
                const bool failed = m_export_feedback.starts_with("Export failed:");
                ImGui::TextColored(
                    failed ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.35f, 0.85f, 0.5f, 1.0f),
                    "%s", m_export_feedback.c_str()
                );
            }
            ImGui::Spacing();
        }

        ImGui::TextColored(ImVec4(0.49f, 0.46f, 0.95f, 1.0f), "STRESS REGRESSION");
        ImGui::Separator();
        if (ImGui::Button("Run deterministic scenarios") && !m_project_root.empty()) {
            std::vector<std::pair<StressScenario, StressScenarioResult>> results;
            bool passed = true;
            std::string first_error;
            for (const StressScenario& scenario : built_in_stress_scenarios()) {
                StressScenarioResult result = run_stress_scenario(scenario);
                std::string budget_error;
                if (!validate_stress_budget(scenario, result, budget_error)) {
                    passed = false;
                    if (first_error.empty()) first_error = scenario.name + ": " + budget_error;
                }
                results.emplace_back(scenario, std::move(result));
            }
            std::string export_error;
            const auto path = std::filesystem::path(m_project_root) / "Library" / "StressResults" / "latest.json";
            if (!export_stress_results(path.string(), results, export_error)) {
                m_stress_feedback = "Stress export failed: " + export_error;
            } else if (!passed) {
                m_stress_feedback = "Stress failed: " + first_error;
            } else {
                m_stress_feedback = "Stress passed: " + path.string();
            }
        }
        if (!m_stress_feedback.empty()) {
            const bool passed = m_stress_feedback.starts_with("Stress passed:");
            ImGui::TextColored(
                passed ? ImVec4(0.35f, 0.85f, 0.5f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                "%s", m_stress_feedback.c_str()
            );
        }
        ImGui::Spacing();

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
