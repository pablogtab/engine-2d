#pragma once

#include "../EditorPanel.hpp"
#include <string>
#include <vector>

namespace Engine {

struct LogEntry {
    std::string timestamp;
    std::string level;
    std::string message;
};

class ConsolePanel : public EditorPanel {
public:
    ConsolePanel();
    ~ConsolePanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void add_log(const std::string& timestamp, const std::string& level, const std::string& message);
    void clear();

private:
    void execute_command(const std::string& cmd);

    std::string m_name{"Console & Logs"};
    std::vector<LogEntry> m_entries;
    char m_input_buffer[256]{""};
    char m_filter_buffer[64]{""};
    bool m_auto_scroll{true};
    bool m_show_info{true};
    bool m_show_warn{true};
    bool m_show_error{true};
};

} // namespace Engine
