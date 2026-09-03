#include "StatsPanel.hpp"
#include "../EditorTheme.hpp"
#include "../ui/EditorUi.hpp"
#include "../../debug/FrameProfiler.hpp"
#include "../../debug/StressScenarios.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine {

namespace {

struct FrameAssessment {
    const char* label;
    EditorUi::BadgeTone tone;
};

FrameAssessment assess_frame(float milliseconds) {
    if (milliseconds <= 0.0f) return {"NO DATA", EditorUi::BadgeTone::Neutral};
    if (milliseconds <= 16.67f) return {"60 FPS", EditorUi::BadgeTone::Success};
    if (milliseconds <= 33.33f) return {"30 FPS", EditorUi::BadgeTone::Warning};
    return {"OVER BUDGET", EditorUi::BadgeTone::Error};
}

void draw_summary_value(const char* label, const char* value, const char* detail = nullptr) {
    ImGui::TextDisabled("%s", label);
    ImGui::TextUnformatted(value);
    if (detail) ImGui::TextDisabled("%s", detail);
}

void draw_budget_lines(float maximum) {
    if (maximum <= 0.0f) return;
    const auto& palette = EditorTheme::palette();
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum_point = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const auto draw_budget = [&](float value, const ImVec4& color, const char* label) {
        if (value > maximum) return;
        const float ratio = std::clamp(value / maximum, 0.0f, 1.0f);
        const float y = maximum_point.y - ratio * (maximum_point.y - minimum.y);
        const ImU32 packed = ImGui::ColorConvertFloat4ToU32(
            ImVec4(color.x, color.y, color.z, 0.52f)
        );
        draw_list->AddLine(ImVec2(minimum.x, y), ImVec2(maximum_point.x, y), packed);
        draw_list->AddText(ImVec2(maximum_point.x - 39.0f, y + 2.0f), packed, label);
    };
    draw_budget(16.67f, palette.success, "16.7");
    draw_budget(33.33f, palette.warning, "33.3");
}

int timeline_frame_at_cursor(std::size_t frame_count) {
    if (frame_count <= 1) return 0;
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    const float padding = ImGui::GetStyle().FramePadding.x;
    const float left = minimum.x + padding;
    const float right = maximum.x - padding;
    const float width = std::max(1.0f, right - left);
    const float ratio = std::clamp((ImGui::GetIO().MousePos.x - left) / width, 0.0f, 1.0f);
    return static_cast<int>(ratio * static_cast<float>(frame_count - 1) + 0.5f);
}

void draw_timeline_selection(
    int selected_frame, std::size_t frame_count, float frame_time, float maximum
) {
    if (frame_count == 0 || maximum <= 0.0f) return;
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum_point = ImGui::GetItemRectMax();
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const float left = minimum.x + padding.x;
    const float right = maximum_point.x - padding.x;
    const float top = minimum.y + padding.y;
    const float bottom = maximum_point.y - padding.y;
    const float horizontal_ratio = frame_count <= 1
        ? 0.0f
        : static_cast<float>(selected_frame) / static_cast<float>(frame_count - 1);
    const float vertical_ratio = std::clamp(frame_time / maximum, 0.0f, 1.0f);
    const float x = left + horizontal_ratio * (right - left);
    const float y = bottom - vertical_ratio * (bottom - top);
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(EditorTheme::palette().warning);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddLine(ImVec2(x, top), ImVec2(x, bottom), color, 1.5f);
    draw_list->AddCircleFilled(ImVec2(x, y), 3.5f * EditorTheme::metrics().scale, color);
}

void draw_feedback(const std::string& feedback, const char* success_label) {
    if (feedback.empty()) return;
    const bool failed = feedback.find("failed") != std::string::npos ||
        feedback.find("Failed") != std::string::npos;
    EditorUi::badge(
        failed ? "FAILED" : success_label,
        failed ? EditorUi::BadgeTone::Error : EditorUi::BadgeTone::Success
    );
    ImGui::SameLine();
    ImGui::TextWrapped("%s", feedback.c_str());
}

} // namespace

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

    ImGui::SetNextWindowSize(ImVec2(680.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(m_name.c_str(), &is_open)) {
        ImGui::End();
        return;
    }

    EditorUi::area_heading(EditorIcon::Zap, "Engine & Metrics");

    const ImGuiIO& io = ImGui::GetIO();
    const float framerate = io.Framerate > 0.0f ? io.Framerate : 0.0f;
    const float frame_milliseconds = framerate > 0.0f ? 1000.0f / framerate : 0.0f;
    const FrameAssessment assessment = assess_frame(frame_milliseconds);
    const ProfileStatistics statistics = m_profiler
        ? m_profiler->statistics(static_cast<std::size_t>(m_history_window))
        : ProfileStatistics{};
    char fps_label[32];
    char frame_label[32];
    char p95_label[32];
    std::snprintf(fps_label, sizeof(fps_label), "%.1f FPS", framerate);
    std::snprintf(frame_label, sizeof(frame_label), "%.2f ms", frame_milliseconds);
    std::snprintf(p95_label, sizeof(p95_label), "%.2f ms", statistics.p95);

    if (ImGui::BeginTable(
            "##metrics_summary", 3,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, 55.0f * EditorTheme::metrics().scale);
        ImGui::TableSetColumnIndex(0);
        draw_summary_value("FRAMERATE", fps_label, "Live UI frame rate");
        ImGui::TableSetColumnIndex(1);
        draw_summary_value("FRAME TIME", frame_label);
        EditorUi::badge(assessment.label, assessment.tone);
        ImGui::TableSetColumnIndex(2);
        draw_summary_value(
            "CAPTURE P95",
            m_profiler && !m_profiler->frames().empty() ? p95_label : "--",
            m_profiler && m_profiler->enabled() ? "Capturing" : "Profiler idle"
        );
        ImGui::EndTable();
    }

    EditorUi::section_heading("LIVE FRAME", "LAST 60 SAMPLES");
    float live_maximum = 33.33f;
    for (const float sample : m_frametime_history) {
        live_maximum = std::max(live_maximum, sample * 1.15f);
    }
    ImGui::PushStyleColor(ImGuiCol_PlotLines, EditorTheme::palette().accent_hovered);
    ImGui::PlotLines(
        "##live_frametime", m_frametime_history, 60, m_history_offset,
        nullptr, 0.0f, live_maximum,
        ImVec2(ImGui::GetContentRegionAvail().x, 72.0f * EditorTheme::metrics().scale)
    );
    ImGui::PopStyleColor();
    draw_budget_lines(live_maximum);

    if (ImGui::BeginTabBar("##metrics_modes")) {
        if (ImGui::BeginTabItem("Profiler")) {
            const std::string capture_count = m_profiler
                ? std::to_string(m_profiler->frames().size()) + " CAPTURED"
                : "UNAVAILABLE";
            EditorUi::section_heading("FRAME PROFILER", capture_count.c_str());

            const bool has_profiler = m_profiler != nullptr;
            const bool has_frames = has_profiler && !m_profiler->frames().empty();
            if (EditorUi::action_button(
                    "toggle_capture",
                    has_profiler && m_profiler->enabled() ? EditorIcon::Stop : EditorIcon::Play,
                    has_profiler && m_profiler->enabled() ? "Stop Capture" : "Start Capture",
                    has_profiler && m_profiler->enabled()
                        ? EditorUi::ButtonTone::Neutral
                        : EditorUi::ButtonTone::Primary,
                    has_profiler)) {
                m_profiler->set_enabled(!m_profiler->enabled());
            }
            ImGui::SameLine();
            if (EditorUi::action_button(
                    "export_capture", EditorIcon::Save, "Export",
                    EditorUi::ButtonTone::Neutral,
                    has_frames && !m_project_root.empty())) {
                std::string error;
                const auto path = std::filesystem::path(m_project_root) /
                    "Library" / "ProfilerCaptures" / "latest.json";
                if (!m_profiler->export_json(path.string(), error)) {
                    m_export_feedback = "Export failed: " + error;
                } else {
                    m_export_feedback = "Saved: " + path.string();
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(105.0f * EditorTheme::metrics().scale);
            const std::string history_preview = std::to_string(m_history_window) + " frames";
            if (ImGui::BeginCombo("##history_window", history_preview.c_str())) {
                for (const int option : {60, 120, 300}) {
                    const bool selected = m_history_window == option;
                    const std::string label = std::to_string(option) + " frames";
                    if (ImGui::Selectable(label.c_str(), selected)) m_history_window = option;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("Statistics and timeline history window");
            }

            draw_feedback(m_export_feedback, "SAVED");

            if (!has_profiler) {
                ImGui::Spacing();
                ImGui::TextDisabled("Frame profiler is unavailable in this editor session.");
            } else if (!has_frames) {
                ImGui::Spacing();
                ImGui::TextDisabled("Start capture to inspect frame scopes and counters.");
            } else {
                const auto& frames = m_profiler->frames();
                const std::size_t visible_count = std::min<std::size_t>(
                    static_cast<std::size_t>(m_history_window), frames.size()
                );
                const std::size_t first = frames.size() - visible_count;
                std::vector<float> timeline;
                timeline.reserve(visible_count);
                float timeline_maximum = 33.33f;
                for (std::size_t index = first; index < frames.size(); ++index) {
                    timeline.push_back(frames[index].frame_milliseconds);
                    timeline_maximum = std::max(
                        timeline_maximum, frames[index].frame_milliseconds * 1.15f
                    );
                }

                if (ImGui::BeginTable(
                        "##profile_statistics", 6,
                        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
                    const std::pair<const char*, float> values[] = {
                        {"AVG", statistics.average}, {"MIN", statistics.minimum},
                        {"MAX", statistics.maximum}, {"P50", statistics.p50},
                        {"P95", statistics.p95}, {"P99", statistics.p99}
                    };
                    ImGui::TableNextRow();
                    for (const auto& [label, value] : values) {
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", label);
                        ImGui::Text("%.2f ms", value);
                    }
                    ImGui::EndTable();
                }

                int visible_frame = static_cast<int>(visible_count - 1);
                if (!m_follow_latest_frame && m_selected_frame_index != 0) {
                    const auto selected_it = std::find_if(
                        frames.begin() + static_cast<std::ptrdiff_t>(first), frames.end(),
                        [&](const ProfileFrame& frame) {
                            return frame.index == m_selected_frame_index;
                        }
                    );
                    if (selected_it != frames.end()) {
                        visible_frame = static_cast<int>(
                            std::distance(
                                frames.begin() + static_cast<std::ptrdiff_t>(first), selected_it
                            )
                        );
                    } else {
                        visible_frame = m_selected_frame_index < frames[first].index
                            ? 0
                            : static_cast<int>(visible_count - 1);
                        m_selected_frame_index = frames[
                            first + static_cast<std::size_t>(visible_frame)
                        ].index;
                    }
                }

                ImGui::PushStyleColor(ImGuiCol_PlotLines, EditorTheme::palette().accent_hovered);
                ImGui::PlotLines(
                    "##captured_timeline", timeline.data(), static_cast<int>(timeline.size()), 0,
                    nullptr, 0.0f, timeline_maximum,
                    ImVec2(ImGui::GetContentRegionAvail().x, 90.0f * EditorTheme::metrics().scale)
                );
                ImGui::PopStyleColor();
                draw_budget_lines(timeline_maximum);

                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        visible_frame = timeline_frame_at_cursor(visible_count);
                        m_follow_latest_frame = false;
                        m_selected_frame_index = frames[
                            first + static_cast<std::size_t>(visible_frame)
                        ].index;
                    }
                }
                if (m_follow_latest_frame) {
                    visible_frame = static_cast<int>(visible_count - 1);
                    m_selected_frame_index = frames.back().index;
                }
                draw_timeline_selection(
                    visible_frame, visible_count,
                    timeline[static_cast<std::size_t>(visible_frame)], timeline_maximum
                );

                if (ImGui::Checkbox("Follow latest frame", &m_follow_latest_frame) &&
                    m_follow_latest_frame) {
                    visible_frame = static_cast<int>(visible_count - 1);
                    m_selected_frame_index = frames.back().index;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Click or drag the timeline to inspect");

                const ProfileFrame& selected = frames[
                    first + static_cast<std::size_t>(visible_frame)
                ];
                const std::string frame_heading = "FRAME #" + std::to_string(selected.index);
                char frame_detail[32];
                std::snprintf(
                    frame_detail, sizeof(frame_detail), "%.2f ms", selected.frame_milliseconds
                );
                EditorUi::section_heading(frame_heading.c_str(), frame_detail);

                std::unordered_map<std::string, float> totals;
                for (const auto& scope : selected.scopes) {
                    totals[scope.name] += scope.milliseconds;
                }
                std::vector<std::pair<std::string, float>> sorted(totals.begin(), totals.end());
                std::sort(sorted.begin(), sorted.end(), [](const auto& left, const auto& right) {
                    return left.second > right.second;
                });

                if (sorted.empty()) {
                    ImGui::TextDisabled("No subsystem scopes were recorded for this frame.");
                } else if (ImGui::BeginTable(
                        "##profiler_scopes", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
                    ImGui::TableSetupColumn("Subsystem", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 78.0f);
                    ImGui::TableSetupColumn("Frame share", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    for (const auto& [name, milliseconds] : sorted) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.2f ms", milliseconds);
                        ImGui::TableSetColumnIndex(2);
                        const float share = selected.frame_milliseconds > 0.0f
                            ? std::clamp(
                                milliseconds / selected.frame_milliseconds, 0.0f, 1.0f
                            )
                            : 0.0f;
                        ImGui::ProgressBar(share, ImVec2(-1.0f, 0.0f), "");
                    }
                    ImGui::EndTable();
                }

                if (!selected.counters.empty() && ImGui::CollapsingHeader("Counters")) {
                    std::vector<std::pair<std::string, std::uint64_t>> counters(
                        selected.counters.begin(), selected.counters.end()
                    );
                    std::sort(counters.begin(), counters.end());
                    if (ImGui::BeginTable(
                            "##profiler_counters", 2,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_NoPadOuterX)) {
                        ImGui::TableSetupColumn("Counter", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                        for (const auto& [name, value] : counters) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(name.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%llu", static_cast<unsigned long long>(value));
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::TextDisabled("GPU timings are not available on the current backend.");
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stress Tests")) {
            EditorUi::section_heading("DETERMINISTIC STRESS SUITE");
            ImGui::TextDisabled(
                "Validates structural budgets; hardware timing is recorded but does not decide pass/fail."
            );
            if (EditorUi::action_button(
                    "run_stress_suite", EditorIcon::Zap, "Run Suite",
                    EditorUi::ButtonTone::Primary, !m_project_root.empty())) {
                std::vector<std::pair<StressScenario, StressScenarioResult>> results;
                m_stress_results.clear();
                bool all_passed = true;
                std::string first_error;
                for (const StressScenario& scenario : built_in_stress_scenarios()) {
                    StressScenarioResult result = run_stress_scenario(scenario);
                    std::string budget_error;
                    const bool passed = validate_stress_budget(scenario, result, budget_error);
                    all_passed = all_passed && passed;
                    if (!passed && first_error.empty()) {
                        first_error = scenario.name + ": " + budget_error;
                    }
                    m_stress_results.push_back({
                        scenario.name,
                        result.entities,
                        result.sprites,
                        result.draw_calls,
                        scenario.max_draw_calls,
                        result.allocations,
                        scenario.max_allocations,
                        result.cpu_milliseconds,
                        passed,
                        passed ? "Within structural budgets" : budget_error
                    });
                    results.emplace_back(scenario, std::move(result));
                }
                std::string export_error;
                const auto path = std::filesystem::path(m_project_root) /
                    "Library" / "StressResults" / "latest.json";
                if (!export_stress_results(path.string(), results, export_error)) {
                    m_stress_feedback = "Stress export failed: " + export_error;
                } else if (!all_passed) {
                    m_stress_feedback = "Stress failed: " + first_error;
                } else {
                    m_stress_feedback = "Stress passed: " + path.string();
                }
            }

            draw_feedback(m_stress_feedback, "PASS");
            if (m_stress_results.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("No stress suite has been run in this session.");
            } else if (ImGui::BeginTable(
                    "##stress_results", 4,
                    ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableSetupColumn("Scenario", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Workload", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("CPU", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableSetupColumn("Budget", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableHeadersRow();
                for (const StressResultRow& row : m_stress_results) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(row.name.c_str());
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        ImGui::SetTooltip("%s", row.detail.c_str());
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text(
                        "%zu entities  |  %zu sprites\n%zu/%zu draws  |  %zu/%zu allocs",
                        row.entities, row.sprites,
                        row.draw_calls, row.max_draw_calls,
                        row.allocations, row.max_allocations
                    );
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.2f ms", row.cpu_milliseconds);
                    ImGui::TableSetColumnIndex(3);
                    EditorUi::badge(
                        row.passed ? "PASS" : "FAIL",
                        row.passed ? EditorUi::BadgeTone::Success : EditorUi::BadgeTone::Error
                    );
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace Engine
