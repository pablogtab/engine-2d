#include "DiagnosticEngine.hpp"
#include "SourceManager.hpp"
#include <sstream>
#include <format>

namespace EngineScript {

namespace {

std::string diagnostic_explanation(const Diagnostic& diagnostic) {
    if (diagnostic.code == "E3100" || diagnostic.code == "E3101") {
        return "Interpolated values must have a deterministic text conversion, and numeric formats are checked statically.";
    }
    if (diagnostic.code == "E3102" || diagnostic.code == "E3103") {
        return "Optional binding only accepts nullable values; guard bindings also require their else branch to terminate control flow.";
    }
    if (diagnostic.code == "E3104" || diagnostic.code == "E3105") {
        return "A conditional expression requires a truthy-compatible condition and compatible result branches.";
    }
    if (diagnostic.code >= "E3106" && diagnostic.code <= "E3109") {
        return "Match patterns are type-checked, cannot be duplicated, and must cover the complete input domain.";
    }
    if (diagnostic.code >= "E3111" && diagnostic.code <= "E3117") {
        return "Default and named arguments are normalized against the declared callable signature before code generation.";
    }
    if (diagnostic.code == "E3118" || diagnostic.code == "E3119") {
        return "Deque operations use a fixed generic element type and checked operation arities.";
    }
    return diagnostic.message;
}

std::string diagnostic_suggestion(const Diagnostic& diagnostic) {
    if (!diagnostic.fixes.empty()) return diagnostic.fixes.front().title;
    if (diagnostic.code == "E3100") return "Add a public synchronous toString(): String method or interpolate a supported built-in value.";
    if (diagnostic.code == "E3101") return "Use an integer width such as :02 or a floating precision such as :.2f on a numeric value.";
    if (diagnostic.code == "E3102") return "Bind a nullable expression or remove the optional binding syntax.";
    if (diagnostic.code == "E3103") return "End the guard else block with return, break, or continue.";
    if (diagnostic.code == "E3104") return "Use a Bool, numeric, String, nullable, class, or interface value as the condition.";
    if (diagnostic.code == "E3105") return "Convert one branch so both conditional branches have a compatible type.";
    if (diagnostic.code == "E3108") return "Add the missing closed-domain cases or a final _ arm.";
    if (diagnostic.code == "E3111") return "Move required parameters before every parameter with a default.";
    if (diagnostic.code == "E3112") return "Replace the default with a literal, null, enum member, or constant native value constructor.";
    if (diagnostic.code == "E3114") return "Move all positional arguments before the first named argument.";
    if (diagnostic.code == "E3115") return "Use a parameter name from the callable signature.";
    if (diagnostic.code == "E3116") return "Remove one of the duplicated arguments.";
    if (diagnostic.code == "E3117") return "Pass the missing argument or declare a valid default for it.";
    if (diagnostic.code == "E3118" || diagnostic.code == "E3119") return "Use the documented Deque<T> signature and element type.";
    return "Review the expression at the reported range and apply the type or syntax required by the diagnostic message.";
}

} // namespace

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
        ss << std::format("    \"explanation\": \"{}\",\n", json_escape(diagnostic_explanation(d)));
        ss << std::format("    \"suggestedFix\": \"{}\",\n", json_escape(diagnostic_suggestion(d)));
        ss << std::format("    \"file\": \"{}\",\n", json_escape(file_path));
        ss << "    \"range\": {\n";
        ss << std::format("      \"startLine\": {},\n", d.span.start_line);
        ss << std::format("      \"startColumn\": {},\n", d.span.start_column);
        ss << std::format("      \"endLine\": {},\n", d.span.end_line);
        ss << std::format("      \"endColumn\": {}\n", d.span.end_column);
        ss << "    },\n";
        ss << "    \"fixes\": [";
        for (size_t fix_index = 0; fix_index < d.fixes.size(); ++fix_index) {
            if (fix_index > 0) ss << ",";
            ss << "{\"title\":\"" << json_escape(d.fixes[fix_index].title)
               << "\",\"replacement\":\"" << json_escape(d.fixes[fix_index].replacement_text) << "\"}";
        }
        ss << "]\n";
        ss << "  }" << (i + 1 < m_diagnostics.size() ? "," : "") << "\n";
    }
    ss << "]\n";
    return ss.str();
}

} // namespace EngineScript
