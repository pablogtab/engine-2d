#include "ConsolePanel.hpp"
#include "../../core/Time.hpp"
#include <imgui.h>
#include <algorithm>

namespace Engine {

ConsolePanel::ConsolePanel() {
    add_log({"00:00:00", LogLevel::INFO, "Engine2D Live Editor Console ready.", {"editor"}});
    add_log({"00:00:00", LogLevel::INFO, "Type 'help' in the prompt below for in-game debug commands.", {"editor"}});
}

void ConsolePanel::add_log(LogRecord record) {
    std::lock_guard lock(m_entries_mutex);
    if (m_entries.size() > 1000) {
        m_entries.erase(m_entries.begin(), m_entries.begin() + 100);
    }
    m_entries.push_back(std::move(record));
}

void ConsolePanel::clear() {
    std::lock_guard lock(m_entries_mutex);
    m_entries.clear();
}

void ConsolePanel::execute_command(const std::string& cmd) {
    add_log({"CMD", LogLevel::DEBUG, "> " + cmd, {"console"}});

    if (cmd == "help") {
        add_log({"HELP", LogLevel::INFO, "Available commands:", {"console"}});
        add_log({"HELP", LogLevel::INFO, "  help           - Show this help menu", {"console"}});
        add_log({"HELP", LogLevel::INFO, "  clear          - Clear console buffer", {"console"}});
        add_log({"HELP", LogLevel::INFO, "  timescale <v>  - Set simulation speed (e.g. timescale 0.5)", {"console"}});
        add_log({"HELP", LogLevel::INFO, "  pause          - Freeze simulation", {"console"}});
        add_log({"HELP", LogLevel::INFO, "  resume         - Resume normal 1.0x speed", {"console"}});
    } else if (cmd == "clear") {
        clear();
    } else if (cmd == "pause") {
        Time::set_time_scale(0.0f);
        add_log({"TIME", LogLevel::INFO, "Simulation paused.", {"time"}});
    } else if (cmd == "resume") {
        Time::set_time_scale(1.0f);
        add_log({"TIME", LogLevel::INFO, "Simulation resumed (1.0x).", {"time"}});
    } else if (cmd.rfind("timescale ", 0) == 0) {
        try {
            float s = std::stof(cmd.substr(10));
            Time::set_time_scale(s);
            add_log({"TIME", LogLevel::INFO, "Simulation timescale set to " + std::to_string(s) + "x", {"time"}});
        } catch (...) {
            add_log({"ERROR", LogLevel::ERR, "Invalid timescale value.", {"console"}});
        }
    } else {
        add_log({"ERROR", LogLevel::ERR, "Unknown command: '" + cmd + "'. Type 'help' for available commands.", {"console"}});
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
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputTextWithHint("##category", "Category...", m_category_filter_buffer, sizeof(m_category_filter_buffer));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::InputTextWithHint("##entity", "Entity...", m_entity_filter_buffer, sizeof(m_entity_filter_buffer));

        ImGui::Separator();

        // --- 2. Log History List ---
        float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar);

        std::string filter_str = m_filter_buffer;
        std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);
        std::string category_filter = m_category_filter_buffer;
        std::transform(category_filter.begin(), category_filter.end(), category_filter.begin(), ::tolower);
        const std::string entity_filter = m_entity_filter_buffer;

        std::vector<LogRecord> entries;
        {
            std::lock_guard lock(m_entries_mutex);
            entries = m_entries;
        }
        for (const auto& entry : entries) {
            if (entry.level == LogLevel::INFO && !m_show_info) continue;
            if (entry.level == LogLevel::WARN && !m_show_warn) continue;
            if ((entry.level == LogLevel::ERR || entry.level == LogLevel::CRITICAL) && !m_show_error) continue;

            std::string category = entry.metadata.category;
            std::transform(category.begin(), category.end(), category.begin(), ::tolower);
            if (!category_filter.empty() && category.find(category_filter) == std::string::npos) continue;
            if (!entity_filter.empty() && (!entry.metadata.entity || std::to_string(*entry.metadata.entity) != entity_filter)) continue;

            if (!filter_str.empty()) {
                std::string full_line = entry.message;
                std::transform(full_line.begin(), full_line.end(), full_line.begin(), ::tolower);
                if (full_line.find(filter_str) == std::string::npos) {
                    continue;
                }
            }

            ImVec4 col = ImVec4(0.85f, 0.85f, 0.88f, 1.0f);
            if (entry.level == LogLevel::WARN) {
                col = ImVec4(0.95f, 0.75f, 0.2f, 1.0f);
            } else if (entry.level == LogLevel::ERR || entry.level == LogLevel::CRITICAL) {
                col = ImVec4(0.95f, 0.3f, 0.3f, 1.0f);
            } else if (entry.level == LogLevel::DEBUG || entry.level == LogLevel::TRACE) {
                col = ImVec4(0.5f, 0.5f, 0.6f, 1.0f);
            }

            const char* level = entry.level == LogLevel::ERR ? "ERROR" :
                entry.level == LogLevel::CRITICAL ? "CRIT" :
                entry.level == LogLevel::WARN ? "WARN" :
                entry.level == LogLevel::DEBUG ? "DEBUG" :
                entry.level == LogLevel::TRACE ? "TRACE" : "INFO";

            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.55f, 1.0f), "[%s]", entry.timestamp.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col, "[%s]", level);
            if (!entry.metadata.category.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", entry.metadata.category.c_str());
            }
            if (entry.metadata.entity) {
                ImGui::SameLine();
                ImGui::TextDisabled("[entity=%u]", *entry.metadata.entity);
            }
            if (entry.metadata.source && entry.metadata.source->is_valid()) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s:%u:%u]", entry.metadata.source->path.c_str(), entry.metadata.source->line, entry.metadata.source->column);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(entry.message.c_str());
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
