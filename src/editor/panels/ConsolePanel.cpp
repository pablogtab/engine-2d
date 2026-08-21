#include "ConsolePanel.hpp"
#include "../../core/Time.hpp"
#include <imgui.h>
#include <algorithm>

namespace Engine {

ConsolePanel::ConsolePanel() {
    add_log("00:00:00", "INFO", "Engine2D Live Editor Console ready.");
    add_log("00:00:00", "INFO", "Type 'help' in the prompt below for in-game debug commands.");
}

void ConsolePanel::add_log(const std::string& timestamp, const std::string& level, const std::string& message) {
    std::lock_guard lock(m_entries_mutex);
    if (m_entries.size() > 1000) {
        m_entries.erase(m_entries.begin(), m_entries.begin() + 100);
    }
    m_entries.push_back({timestamp, level, message});
}

void ConsolePanel::clear() {
    std::lock_guard lock(m_entries_mutex);
    m_entries.clear();
}

void ConsolePanel::execute_command(const std::string& cmd) {
    add_log("CMD", "DEBUG", "> " + cmd);

    if (cmd == "help") {
        add_log("HELP", "INFO", "Available commands:");
        add_log("HELP", "INFO", "  help           - Show this help menu");
        add_log("HELP", "INFO", "  clear          - Clear console buffer");
        add_log("HELP", "INFO", "  timescale <v>  - Set simulation speed (e.g. timescale 0.5)");
        add_log("HELP", "INFO", "  pause          - Freeze simulation");
        add_log("HELP", "INFO", "  resume         - Resume normal 1.0x speed");
    } else if (cmd == "clear") {
        clear();
    } else if (cmd == "pause") {
        Time::set_time_scale(0.0f);
        add_log("TIME", "INFO", "Simulation paused.");
    } else if (cmd == "resume") {
        Time::set_time_scale(1.0f);
        add_log("TIME", "INFO", "Simulation resumed (1.0x).");
    } else if (cmd.rfind("timescale ", 0) == 0) {
        try {
            float s = std::stof(cmd.substr(10));
            Time::set_time_scale(s);
            add_log("TIME", "INFO", "Simulation timescale set to " + std::to_string(s) + "x");
        } catch (...) {
            add_log("ERROR", "ERROR", "Invalid timescale value.");
        }
    } else {
        add_log("ERROR", "ERROR", "Unknown command: '" + cmd + "'. Type 'help' for available commands.");
    }
}

void ConsolePanel::on_gui() {
    if (!is_open) return;

    ImGui::SetNextWindowSize(ImVec2(500, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(m_name.c_str(), &is_open)) {
        // --- 1. Top Controls Bar ---
        if (ImGui::Button("Clear")) {
            clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_auto_scroll);
        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_show_info);
        ImGui::SameLine();
        ImGui::Checkbox("Warn", &m_show_warn);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &m_show_error);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputTextWithHint("##filter", "Filter...", m_filter_buffer, sizeof(m_filter_buffer));

        ImGui::Separator();

        // --- 2. Log History List ---
        float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);

        std::string filter_str = m_filter_buffer;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

        std::vector<LogEntry> entries;
        {
            std::lock_guard lock(m_entries_mutex);
            entries = m_entries;
        }
        for (const auto& entry : entries) {
            if (entry.level == "INFO" && !m_show_info) continue;
            if (entry.level == "WARN" && !m_show_warn) continue;
            if (entry.level == "ERROR" && !m_show_error) continue;

            if (!filter_str.empty()) {
                std::string full_line = entry.message;
                std::transform(full_line.begin(), full_line.end(), full_line.begin(), ::tolower);
                if (full_line.find(filter_str) == std::string::npos) {
                    continue;
                }
            }

            ImVec4 col = ImVec4(0.85f, 0.85f, 0.88f, 1.0f);
            if (entry.level == "WARN") {
                col = ImVec4(0.95f, 0.75f, 0.2f, 1.0f);
            } else if (entry.level == "ERROR") {
                col = ImVec4(0.95f, 0.3f, 0.3f, 1.0f);
            } else if (entry.level == "DEBUG" || entry.level == "CMD") {
                col = ImVec4(0.5f, 0.5f, 0.6f, 1.0f);
            }

            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.55f, 1.0f), "[%s]", entry.timestamp.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col, "[%s] %s", entry.level.c_str(), entry.message.c_str());
        }

        if (m_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
        ImGui::Separator();

        // --- 3. Command Line Input ---
        ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue;
        if (ImGui::InputTextWithHint("##cmd", "Enter console command (try 'help')...", m_input_buffer, sizeof(m_input_buffer), input_flags)) {
            if (m_input_buffer[0] != '\0') {
                execute_command(m_input_buffer);
                m_input_buffer[0] = '\0';
            }
            ImGui::SetKeyboardFocusHere(-1); // Keep focus on input box
        }
    }
    ImGui::End();
}

} // namespace Engine
