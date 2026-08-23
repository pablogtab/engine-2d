#pragma once

#include "SourceLocation.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace EngineScript {

class SourceManager;

enum class DiagnosticSeverity {
    Error,
    Warning,
    Info,
    Hint
};

inline const char* severity_to_string(DiagnosticSeverity sev) {
    switch (sev) {
        case DiagnosticSeverity::Error:   return "error";
        case DiagnosticSeverity::Warning: return "warning";
        case DiagnosticSeverity::Info:    return "info";
        case DiagnosticSeverity::Hint:    return "hint";
    }
    return "unknown";
}

struct DiagnosticFix {
    std::string title;
    SourceSpan span;
    std::string replacement_text;
};

struct DiagnosticNote {
    std::string message;
    SourceSpan span;
};

struct Diagnostic {
    std::string code;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    SourceSpan span;
    std::vector<DiagnosticNote> notes;
    std::vector<DiagnosticFix> fixes;

    bool is_error() const { return severity == DiagnosticSeverity::Error; }
};

class DiagnosticEngine {
public:
    DiagnosticEngine() = default;

    void report(Diagnostic diag);
    void report_error(std::string code, std::string message, SourceSpan span);
    void report_warning(std::string code, std::string message, SourceSpan span);
    void report_info(std::string code, std::string message, SourceSpan span);

    bool has_errors() const { return m_error_count > 0; }
    size_t error_count() const { return m_error_count; }
    size_t warning_count() const { return m_warning_count; }

    const std::vector<Diagnostic>& get_diagnostics() const { return m_diagnostics; }
    void clear();

    std::string render_cli(const SourceManager& source_manager) const;
    std::string render_json(const SourceManager& source_manager) const;

private:
    std::vector<Diagnostic> m_diagnostics;
    size_t m_error_count = 0;
    size_t m_warning_count = 0;
};

} // namespace EngineScript
