#pragma once

#include "../EditorPanel.hpp"
#include "../../core/Log.hpp"
#include <mutex>
#include <string>
#include <vector>

namespace Engine {

class ConsolePanel : public EditorPanel {
public:
    ConsolePanel();
    ~ConsolePanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void add_log(LogRecord record);
    void clear();

private:
    void execute_command(const std::string& cmd);

    std::string m_name{"Console & Logs"};
    mutable std::mutex m_entries_mutex;
    std::vector<LogRecord> m_entries;
    char m_input_buffer[256]{""};
    char m_filter_buffer[64]{""};
    bool m_auto_scroll{true};
    bool m_show_info{true};
    bool m_show_warn{true};
    bool m_show_error{true};
    char m_category_filter_buffer[64]{};
    char m_entity_filter_buffer[16]{};
};

} // namespace Engine
