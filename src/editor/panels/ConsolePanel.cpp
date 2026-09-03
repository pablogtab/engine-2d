#include "ConsolePanel.hpp"
#include "../../core/Time.hpp"
#include <imgui.h>
#include <algorithm>

namespace Engine {

namespace {

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

void draw_selectable_console(const std::string& text) {
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    std::string wrapped = wrap_selectable_text(text, width);
    const std::size_t line_count = 1 + static_cast<std::size_t>(
        std::count(wrapped.begin(), wrapped.end(), '\n')
    );
    const float height = static_cast<float>(line_count) * ImGui::GetTextLineHeight() + 4.0f;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 1.0f));
    ImGui::InputTextMultiline(
        "##log_text",
        wrapped.data(),
        wrapped.size() + 1,
        ImVec2(width, height),
        ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll
    );
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
}

} // namespace

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
    if (ImGui::Begin(
        m_name.c_str(),
        &is_open,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    )) {
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
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false);

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
        std::string console_text;
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

            const char* level = entry.level == LogLevel::ERR ? "ERROR" :
                entry.level == LogLevel::CRITICAL ? "CRIT" :
                entry.level == LogLevel::WARN ? "WARN" :
                entry.level == LogLevel::DEBUG ? "DEBUG" :
                entry.level == LogLevel::TRACE ? "TRACE" : "INFO";

            std::string line = "[" + entry.timestamp + "] [" + level + "]";
            if (!entry.metadata.category.empty()) {
                line += " [" + entry.metadata.category + "]";
            }
            if (entry.metadata.entity) {
                line += " [entity=" + std::to_string(*entry.metadata.entity) + "]";
            }
            if (entry.metadata.source && entry.metadata.source->is_valid()) {
                line += " [" + entry.metadata.source->path + ":" +
                    std::to_string(entry.metadata.source->line) + ":" +
                    std::to_string(entry.metadata.source->column) + "]";
            }
            line += " " + entry.message;
            if (!console_text.empty()) console_text.push_back('\n');
            console_text += line;
        }

        draw_selectable_console(console_text);

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
