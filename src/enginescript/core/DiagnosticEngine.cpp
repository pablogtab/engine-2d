#include "DiagnosticEngine.hpp"
#include "SourceManager.hpp"
#include <sstream>
#include <format>

namespace EngineScript {

void DiagnosticEngine::report(Diagnostic diag) {
    if (diag.severity == DiagnosticSeverity::Error) {
        ++m_error_count;
    } else if (diag.severity == DiagnosticSeverity::Warning) {
        ++m_warning_count;
    }
    m_diagnostics.push_back(std::move(diag));
}

void DiagnosticEngine::report_error(std::string code, std::string message, SourceSpan span) {
    Diagnostic d;
    d.code = std::move(code);
    d.severity = DiagnosticSeverity::Error;
    d.message = std::move(message);
    d.span = span;
    report(std::move(d));
}

void DiagnosticEngine::report_warning(std::string code, std::string message, SourceSpan span) {
    Diagnostic d;
    d.code = std::move(code);
    d.severity = DiagnosticSeverity::Warning;
    d.message = std::move(message);
    d.span = span;
    report(std::move(d));
}

void DiagnosticEngine::report_info(std::string code, std::string message, SourceSpan span) {
    Diagnostic d;
    d.code = std::move(code);
    d.severity = DiagnosticSeverity::Info;
    d.message = std::move(message);
    d.span = span;
    report(std::move(d));
}

void DiagnosticEngine::clear() {
    m_diagnostics.clear();
    m_error_count = 0;
    m_warning_count = 0;
}

std::string DiagnosticEngine::render_cli(const SourceManager& source_manager) const {
    std::ostringstream ss;
    for (const auto& d : m_diagnostics) {
        const SourceFile* sf = source_manager.get_file(d.span.file_id);
        std::string file_name = sf ? sf->path : "<unknown>";

        ss << std::format("{}:{}:{}: {}: [{}] {}\n",
            file_name, d.span.start_line, d.span.start_column,
            severity_to_string(d.severity), d.code, d.message);

        if (d.span.is_valid()) {
            ss << source_manager.get_snippet(d.span, 1);
        }

        for (const auto& note : d.notes) {
            ss << std::format("  note: {}\n", note.message);
            if (note.span.is_valid()) {
                ss << source_manager.get_snippet(note.span, 0);
            }
        }

        for (const auto& fix : d.fixes) {
            ss << std::format("  suggestion: {}\n", fix.title);
        }
    }
    return ss.str();
}

std::string DiagnosticEngine::render_json(const SourceManager& source_manager) const {
    std::ostringstream ss;
    ss << "[\n";
    for (size_t i = 0; i < m_diagnostics.size(); ++i) {
        const auto& d = m_diagnostics[i];
        const SourceFile* sf = source_manager.get_file(d.span.file_id);
        std::string file_path = sf ? sf->path : "";

        // Escape JSON string characters
        auto json_escape = [](const std::string& s) {
            std::string res;
            for (char c : s) {
                if (c == '"') res += "\\\"";
                else if (c == '\\') res += "\\\\";
                else if (c == '\n') res += "\\n";
                else if (c == '\r') res += "\\r";
                else if (c == '\t') res += "\\t";
                else res += c;
            }
            return res;
        };

        ss << "  {\n";
        ss << std::format("    \"code\": \"{}\",\n", json_escape(d.code));
        ss << std::format("    \"severity\": \"{}\",\n", severity_to_string(d.severity));
        ss << std::format("    \"message\": \"{}\",\n", json_escape(d.message));
        ss << std::format("    \"file\": \"{}\",\n", json_escape(file_path));
        ss << "    \"range\": {\n";
        ss << std::format("      \"startLine\": {},\n", d.span.start_line);
        ss << std::format("      \"startColumn\": {},\n", d.span.start_column);
        ss << std::format("      \"endLine\": {},\n", d.span.end_line);
        ss << std::format("      \"endColumn\": {}\n", d.span.end_column);
        ss << "    }\n";
        ss << "  }" << (i + 1 < m_diagnostics.size() ? "," : "") << "\n";
    }
    ss << "]\n";
    return ss.str();
}

} // namespace EngineScript
