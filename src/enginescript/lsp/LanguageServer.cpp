#include "LanguageServer.hpp"
#include "enginescript/api/ApiCatalog.hpp"
#include <nlohmann/json.hpp>
#include <cctype>
#include <filesystem>
#include <optional>
#include <regex>
#include <sstream>

namespace EngineScript {

using json = nlohmann::ordered_json;

namespace {
json parse_id(const std::string& id) {
    if (id.empty()) return 1;
    try {
        return json::parse(id);
    } catch (...) {
        return id;
    }
}

std::string decode_uri_path(std::string_view encoded) {
    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] == '%' && index + 2 < encoded.size()) {
            const auto hex_value = [](char value) -> int {
                if (value >= '0' && value <= '9') return value - '0';
                if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                if (value >= 'A' && value <= 'F') return value - 'A' + 10;
                return -1;
            };
            const int high = hex_value(encoded[index + 1]);
            const int low = hex_value(encoded[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(encoded[index]);
    }
    return decoded;
}

struct DeclarationLocation {
    std::string source_path;
    uint32_t line = 0;
    uint32_t character = 0;
    uint32_t length = 0;
};

std::optional<DeclarationLocation> find_declaration(
    const SourceManager& source_manager,
    const std::string& name
) {
    if (name.empty()) return std::nullopt;

    const std::regex declaration_pattern(
        R"(^\s*(?:export\s+)?(?:class|struct|interface|enum)\s+([A-Za-z_][A-Za-z0-9_]*))"
    );
    const std::regex method_pattern(
        R"(^\s*(?:async\s+)?(?:override\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*\()"
    );
    const std::regex property_pattern(
        R"(^\s*(?:public|private|protected|readonly|static\s+)*([A-Za-z_][A-Za-z0-9_]*)\s*:)"
    );

    for (const SourceFile& file : source_manager.all_files()) {
        std::istringstream lines(file.content);
        std::string line;
        uint32_t line_number = 0;
        while (std::getline(lines, line)) {
            std::smatch match;
            for (const std::regex* pattern : {&declaration_pattern, &method_pattern, &property_pattern}) {
                if (std::regex_search(line, match, *pattern) && match[1].str() == name) {
                    return DeclarationLocation{
                        .source_path = file.path,
                        .line = line_number,
                        .character = static_cast<uint32_t>(match.position(1)),
                        .length = static_cast<uint32_t>(match.length(1))
                    };
                }
            }
            ++line_number;
        }
    }
    return std::nullopt;
}

std::string base_type_name(std::string type_name) {
    const size_t generic_start = type_name.find('<');
    if (generic_start != std::string::npos) type_name.resize(generic_start);
    if (!type_name.empty() && type_name.back() == '?') type_name.pop_back();
    return type_name;
}

std::string parameter_signature(const ApiParameterDescriptor& parameter) {
    std::string signature = parameter.name + ": " + parameter.type;
    if (!parameter.default_value.empty()) signature += " = " + parameter.default_value;
    else if (parameter.optional) signature += "?";
    return signature;
}

std::string callable_signature(
    std::string_view name,
    const std::vector<ApiParameterDescriptor>& parameters,
    uint32_t min_arguments,
    uint32_t max_arguments,
    std::string_view return_type
) {
    std::string signature(name);
    signature += '(';
    if (!parameters.empty()) {
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (index > 0) signature += ", ";
            signature += parameter_signature(parameters[index]);
        }
    } else if (max_arguments > 0) {
        for (uint32_t index = 0; index < max_arguments; ++index) {
            if (index > 0) signature += ", ";
            signature += "arg" + std::to_string(index + 1);
            if (index >= min_arguments) signature += '?';
        }
    }
    signature += ") -> ";
    signature += return_type;
    return signature;
}

std::string callable_snippet(
    std::string_view name,
    const std::vector<ApiParameterDescriptor>& parameters,
    uint32_t max_arguments
) {
    std::string snippet(name);
    snippet += '(';
    if (!parameters.empty()) {
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (index > 0) snippet += ", ";
            snippet += "${" + std::to_string(index + 1) + ':' + parameters[index].name + '}';
        }
        snippet += ")$0";
    } else if (max_arguments > 0) {
        for (uint32_t index = 0; index < max_arguments; ++index) {
            if (index > 0) snippet += ", ";
            snippet += "${" + std::to_string(index + 1) + ":arg" + std::to_string(index + 1) + '}';
        }
        snippet += ")$0";
    } else {
        snippet += ")$0";
    }
    return snippet;
}

std::optional<std::string> infer_initializer_type(
    const SourceFile& file,
    const std::string& variable_name,
    uint32_t last_line
) {
    static const std::regex function_initializer_pattern(
        R"(\blet\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^>]+>)?\s*\()"
    );
    static const std::regex method_initializer_pattern(
        R"(\blet\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\s*\.\s*([A-Za-z_][A-Za-z0-9_]*)\s*\()"
    );

    std::istringstream lines(file.content);
    std::string source_line;
    for (uint32_t line = 0; line <= last_line && std::getline(lines, source_line); ++line) {
        std::smatch match;
        if (std::regex_search(source_line, match, method_initializer_pattern) && match[1].str() == variable_name) {
            if (const ApiMethodDescriptor* method = ApiCatalog::instance().find_available_method(match[2].str(), match[3].str())) {
                return method->return_type;
            }
        }
        if (std::regex_search(source_line, match, function_initializer_pattern) && match[1].str() == variable_name) {
            for (const ApiFunctionDescriptor& function : ApiCatalog::instance().functions()) {
                if (function.availability == ApiAvailability::Available && function.name == match[2].str()) {
                    return function.return_type;
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> find_typed_symbol(
    const SourceFile& file,
    const std::string& requested_name
) {
    static const std::regex typed_symbol_pattern(
        R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_]*(?:<[^;=(){}]+>)?\??))"
    );
    std::istringstream lines(file.content);
    std::string source_line;
    while (std::getline(lines, source_line)) {
        std::smatch match;
        if (std::regex_search(source_line, match, typed_symbol_pattern) && match[1].str() == requested_name) {
            return std::pair{match[1].str(), base_type_name(match[2].str())};
        }
    }
    return std::nullopt;
}

uint32_t edit_distance(std::string_view left, std::string_view right) {
    std::vector<uint32_t> previous(right.size() + 1);
    std::vector<uint32_t> current(right.size() + 1);
    for (uint32_t index = 0; index <= right.size(); ++index) previous[index] = index;
    for (uint32_t left_index = 1; left_index <= left.size(); ++left_index) {
        current[0] = left_index;
        for (uint32_t right_index = 1; right_index <= right.size(); ++right_index) {
            current[right_index] = std::min({
                previous[right_index] + 1,
                current[right_index - 1] + 1,
                previous[right_index - 1] + (left[left_index - 1] == right[right_index - 1] ? 0U : 1U)
            });
        }
        std::swap(previous, current);
    }
    return previous[right.size()];
}

std::optional<std::pair<std::string, std::string>> find_nearby_typed_symbol(
    const SourceFile& file,
    const std::string& requested_name
) {
    static const std::regex typed_symbol_pattern(
        R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([A-Za-z_][A-Za-z0-9_]*(?:<[^;=(){}]+>)?\??))"
    );
    std::optional<std::pair<std::string, std::string>> closest;
    // A four-character prefix such as "spri" is a common incomplete spelling
    // of a typed receiver such as "sprite". Keep corrections conservative,
    // while allowing two edits for those short partially typed names.
    uint32_t closest_distance = 3;
    std::istringstream lines(file.content);
    std::string source_line;
    while (std::getline(lines, source_line)) {
        std::smatch match;
        if (!std::regex_search(source_line, match, typed_symbol_pattern)) continue;
        const uint32_t distance = edit_distance(requested_name, match[1].str());
        if (distance < closest_distance) {
            closest = std::pair{match[1].str(), base_type_name(match[2].str())};
            closest_distance = distance;
        }
    }
    return closest;
}

std::optional<std::string> infer_receiver_type(
    const EngineScriptWorkspace& workspace,
    const std::string& source_path,
    uint32_t line,
    std::string_view line_prefix
) {
    static const std::regex receiver_pattern(
        R"(([A-Za-z_][A-Za-z0-9_]*(?:\s*\.\s*[A-Za-z_][A-Za-z0-9_]*)*)\s*\.\s*$)"
    );
    const std::string prefix(line_prefix);
    std::smatch receiver_match;
    if (!std::regex_search(prefix, receiver_match, receiver_pattern)) {
        return std::nullopt;
    }

    const std::string receiver_expression = receiver_match[1].str();
    static const std::regex identifier_pattern(R"([A-Za-z_][A-Za-z0-9_]*)");
    std::vector<std::string> receiver_chain;
    for (std::sregex_iterator it(receiver_expression.begin(), receiver_expression.end(), identifier_pattern), end;
         it != end; ++it) {
        receiver_chain.push_back(it->str());
    }
    if (receiver_chain.empty()) return std::nullopt;
    const std::string& receiver = receiver_chain.front();

    const SourceManager& source_manager = workspace.source_manager();
    const SourceFile* file = source_manager.get_file(source_path);
    if (!file) return std::nullopt;
    uint32_t receiver_offset = 0;
    std::optional<std::string> resolved_type;
    if (line < file->line_offsets.size()) {
        receiver_offset = file->line_offsets[line] +
            static_cast<uint32_t>(receiver_match.position(1));
    }
    if (ApiCatalog::instance().has_available_type(receiver)) resolved_type = receiver;
    if (!resolved_type) {
        if (const auto initializer_type = infer_initializer_type(*file, receiver, line)) {
            resolved_type = base_type_name(*initializer_type);
        }
    }
    if (const auto symbol_type = workspace.symbol_type_at(file->id, receiver, receiver_offset)) {
        resolved_type = base_type_name(*symbol_type);
    }
    if (!resolved_type) return std::nullopt;

    for (std::size_t index = 1; index < receiver_chain.size(); ++index) {
        const ApiPropertyDescriptor* property = ApiCatalog::instance().find_available_property(
            *resolved_type, receiver_chain[index]
        );
        if (!property || !property->readable) return std::nullopt;
        resolved_type = base_type_name(property->type);
    }
    return resolved_type;
}
}

LanguageServer::LanguageServer(std::string project_root)
    : m_workspace(std::move(project_root)) {
}

std::string LanguageServer::handle_message(const std::string& json_rpc_msg) {
    m_pending_notifications.clear();
    try {
        json j = json::parse(json_rpc_msg);
        std::string method = j.value("method", "");
        std::string id = j.contains("id") ? (j["id"].is_string() ? j["id"].get<std::string>() : j["id"].dump()) : "";

        if (method == "initialize") {
            return on_initialize(id);
        }
        if (method == "textDocument/didOpen") {
            auto text_doc = j["params"]["textDocument"];
            std::string uri = text_doc.value("uri", "");
            std::string text = text_doc.value("text", "");
            uint32_t version = text_doc.value("version", 1);
            return on_did_open(uri, text, version);
        }
        if (method == "textDocument/didChange") {
            auto text_doc = j["params"]["textDocument"];
            std::string uri = text_doc.value("uri", "");
            uint32_t version = text_doc.value("version", 1);
            std::string text = j["params"]["contentChanges"][0].value("text", "");
            return on_did_change(uri, text, version);
        }
        if (method == "textDocument/didClose") {
            return on_did_close(j["params"]["textDocument"].value("uri", ""));
        }
        if (method == "shutdown") {
            json response = {{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", nullptr}};
            return response.dump();
        }
        if (method == "exit") return "";
        if (method == "textDocument/documentSymbol") {
            std::string uri = j["params"]["textDocument"].value("uri", "");
            return on_document_symbol(id, uri);
        }
        if (method == "textDocument/completion") {
            std::string uri = j["params"]["textDocument"].value("uri", "");
            uint32_t line = j["params"]["position"].value("line", 0);
            uint32_t char_pos = j["params"]["position"].value("character", 0);
            return on_completion(id, uri, line, char_pos);
        }
        if (method == "textDocument/hover") {
            std::string uri = j["params"]["textDocument"].value("uri", "");
            uint32_t line = j["params"]["position"].value("line", 0);
            uint32_t char_pos = j["params"]["position"].value("character", 0);
            return on_hover(id, uri, line, char_pos);
        }
        if (method == "textDocument/definition") {
            std::string uri = j["params"]["textDocument"].value("uri", "");
            uint32_t line = j["params"]["position"].value("line", 0);
            uint32_t char_pos = j["params"]["position"].value("character", 0);
            return on_definition(id, uri, line, char_pos);
        }
    } catch (...) {}
    return "";
}

std::vector<std::string> LanguageServer::take_pending_notifications() {
    std::vector<std::string> notifications = std::move(m_pending_notifications);
    m_pending_notifications.clear();
    return notifications;
}

std::string LanguageServer::on_initialize(const std::string& id) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = parse_id(id);

    json caps;
    caps["textDocumentSync"] = 1; // Full sync
    caps["hoverProvider"] = true;
    caps["completionProvider"] = {
        {"resolveProvider", false},
        {"triggerCharacters", {".", "@", ":"}}
    };
    caps["definitionProvider"] = true;
    caps["documentSymbolProvider"] = true;

    resp["result"] = {{"capabilities", caps}};
    return resp.dump();
}

std::string LanguageServer::on_did_open(const std::string& uri, const std::string& text, uint32_t version) {
    const std::string source_path = source_path_for_uri(uri);
    m_open_document_versions[uri] = version;
    m_uri_to_source_path[uri] = source_path;
    m_workspace.add_source_file(source_path, text);
    return publish_diagnostics(uri);
}

std::string LanguageServer::on_did_change(const std::string& uri, const std::string& text, uint32_t version) {
    const std::string source_path = source_path_for_uri(uri);
    m_open_document_versions[uri] = version;
    m_uri_to_source_path[uri] = source_path;
    m_workspace.update_source_file(source_path, text);
    return publish_diagnostics(uri);
}

std::string LanguageServer::on_did_close(const std::string& uri) {
    const std::string source_path = source_path_for_uri(uri);
    m_workspace.remove_source_file(source_path);
    m_open_document_versions.erase(uri);
    m_uri_to_source_path.erase(uri);

    json notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "textDocument/publishDiagnostics";
    notification["params"] = {{"uri", uri}, {"diagnostics", json::array()}};

    if (!m_open_document_versions.empty()) {
        const std::string remaining_uri = m_open_document_versions.begin()->first;
        m_pending_notifications.push_back(publish_diagnostics(remaining_uri));
    }
    return notification.dump();
}

std::string LanguageServer::source_path_for_uri(const std::string& uri) const {
    if (const auto known = m_uri_to_source_path.find(uri); known != m_uri_to_source_path.end()) {
        return known->second;
    }

    constexpr std::string_view file_scheme = "file://";
    if (!uri.starts_with(file_scheme)) {
        return m_workspace.source_manager().normalize_path(uri);
    }

    std::string path = decode_uri_path(std::string_view(uri).substr(file_scheme.size()));
#if defined(_WIN32)
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':') {
        path.erase(path.begin());
    }
#endif
    return m_workspace.source_manager().normalize_path(std::filesystem::path(path).lexically_normal().string());
}

std::string LanguageServer::publish_diagnostics(const std::string& uri) {
    auto res = m_workspace.compile();

    const auto make_notification = [&](const std::string& target_uri) {
        const FileId document_file_id = m_workspace.source_manager().find_file_id(source_path_for_uri(target_uri));
        json diagnostics = json::array();
        for (const auto& diagnostic : res.diagnostics) {
            if (diagnostic.span.file_id == INVALID_FILE_ID && target_uri != uri) continue;
            if (diagnostic.span.file_id != INVALID_FILE_ID && diagnostic.span.file_id != document_file_id) continue;

            json item;
            item["code"] = diagnostic.code;
            item["message"] = diagnostic.message;
            item["severity"] = diagnostic.severity == DiagnosticSeverity::Error ? 1 : 2;
            item["range"] = {
                {"start", {
                    {"line", diagnostic.span.start_line > 0 ? diagnostic.span.start_line - 1 : 0},
                    {"character", diagnostic.span.start_column > 0 ? diagnostic.span.start_column - 1 : 0}
                }},
                {"end", {
                    {"line", diagnostic.span.end_line > 0 ? diagnostic.span.end_line - 1 : 0},
                    {"character", diagnostic.span.end_column > 0 ? diagnostic.span.end_column - 1 : 0}
                }}
            };
            diagnostics.push_back(std::move(item));
        }

        json notification;
        notification["jsonrpc"] = "2.0";
        notification["method"] = "textDocument/publishDiagnostics";
        notification["params"] = {
            {"uri", target_uri},
            {"version", m_open_document_versions.contains(target_uri) ? m_open_document_versions.at(target_uri) : 0},
            {"diagnostics", std::move(diagnostics)}
        };
        return notification.dump();
    };

    for (const auto& [open_uri, version] : m_open_document_versions) {
        static_cast<void>(version);
        if (open_uri != uri) m_pending_notifications.push_back(make_notification(open_uri));
    }
    return make_notification(uri);
}

std::string LanguageServer::on_document_symbol(const std::string& id, const std::string& uri) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = parse_id(id);

    json syms = json::array();
    const std::string source_path = source_path_for_uri(uri);
    for (const auto& b : m_workspace.metadata_manifest().behaviours) {
        if (m_workspace.source_manager().normalize_path(b.module_path) != source_path) continue;
        json sym;
        sym["name"] = b.name;
        sym["kind"] = 5; // Class
        json range;
        range["start"] = {{"line", 0}, {"character", 0}};
        range["end"] = {{"line", 100}, {"character", 0}};
        sym["range"] = range;
        sym["selectionRange"] = range;

        json children = json::array();
        for (const auto& prop : b.properties) {
            json psym;
            psym["name"] = prop.name;
            psym["kind"] = 7; // Property
            psym["range"] = range;
            psym["selectionRange"] = range;
            children.push_back(psym);
        }
        for (const auto& m : b.methods) {
            json msym;
            msym["name"] = m;
            msym["kind"] = 6; // Method
            msym["range"] = range;
            msym["selectionRange"] = range;
            children.push_back(msym);
        }
        sym["children"] = children;
        syms.push_back(sym);
    }

    resp["result"] = syms;
    return resp.dump();
}

std::string LanguageServer::on_completion(const std::string& id, const std::string& uri, uint32_t line, uint32_t character) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = parse_id(id);

    json items = json::array();
    const FileId file_id = m_workspace.source_manager().find_file_id(source_path_for_uri(uri));
    const std::string line_content = m_workspace.source_manager().get_line_content(file_id, line + 1);
    const bool completing_decorator = character > 0 && character <= line_content.size() &&
        line_content[character - 1] == '@';
    const std::string line_prefix = line_content.substr(0, std::min<std::size_t>(character, line_content.size()));
    if (const auto receiver_type = infer_receiver_type(
            m_workspace, source_path_for_uri(uri), line, line_prefix)) {
        for (const ApiPropertyDescriptor& property : ApiCatalog::instance().properties()) {
            if (property.availability != ApiAvailability::Available || property.owner_type != *receiver_type) continue;
            items.push_back({
                {"label", property.name},
                {"kind", 10},
                {"detail", property.owner_type + "." + property.name + ": " + property.type +
                    (property.writable ? "" : " (read-only)")},
                {"sortText", "0_" + property.name}
            });
        }
        for (const ApiMethodDescriptor& method : ApiCatalog::instance().methods()) {
            if (method.availability != ApiAvailability::Available || method.owner_type != *receiver_type) continue;
            items.push_back({
                {"label", method.name},
                {"kind", 2},
                {"detail", method.owner_type + "." + callable_signature(
                    method.name, method.parameters, method.min_arguments, method.max_arguments, method.return_type)},
                {"insertTextFormat", 2},
                {"insertText", callable_snippet(method.name, method.parameters, method.max_arguments)},
                {"sortText", "1_" + method.name}
            });
        }
        resp["result"] = items;
        return resp.dump();
    }

    static const std::regex receiver_pattern(
        R"(([A-Za-z_][A-Za-z0-9_]*(?:\s*\.\s*[A-Za-z_][A-Za-z0-9_]*)*)\s*\.\s*$)"
    );
    std::smatch receiver_match;
    const std::string source_path = source_path_for_uri(uri);
    const SourceFile* source_file = m_workspace.source_manager().get_file(source_path);
    if (source_file && std::regex_search(line_prefix, receiver_match, receiver_pattern)) {
        const std::string receiver = receiver_match[1].str();
        if (receiver.find('.') == std::string::npos) {
          if (const auto correction = find_nearby_typed_symbol(*source_file, receiver)) {
            const uint32_t receiver_start = static_cast<uint32_t>(receiver_match.position(1));
            items.push_back({
                {"label", correction->first},
                {"kind", 6},
                {"detail", "Did you mean " + correction->first + ": " + correction->second + "?"},
                {"sortText", "0"},
                {"textEdit", {
                    {"newText", correction->first},
                    {"range", {
                        {"start", {{"line", line}, {"character", receiver_start}}},
                        {"end", {{"line", line}, {"character", character}}}
                    }}
                }}
            });
          }
        }
        // A member-access expression must never fall back to language keywords
        // or global API entries. That list is misleading while its receiver is
        // unresolved and hides the actual typing problem.
        resp["result"] = items;
        return resp.dump();
    }

    std::string identifier_prefix;
    static const std::regex identifier_prefix_pattern(R"(([A-Za-z_][A-Za-z0-9_]*)$)");
    std::smatch identifier_prefix_match;
    if (std::regex_search(line_prefix, identifier_prefix_match, identifier_prefix_pattern)) {
        identifier_prefix = identifier_prefix_match[1].str();
    }
    if (source_file && line < source_file->line_offsets.size()) {
        const uint32_t cursor_offset = source_file->line_offsets[line] +
            static_cast<uint32_t>(std::min<std::size_t>(character, line_content.size()));
        for (const WorkspaceDocumentSymbol& symbol : m_workspace.symbols_in_scope(file_id, cursor_offset)) {
            const bool is_import = symbol.kind == WorkspaceDocumentSymbolKind::Import;
            const bool is_field = symbol.kind == WorkspaceDocumentSymbolKind::Field;
            json item = {
                {"label", symbol.name},
                {"kind", is_import ? 18 : (is_field ? 5 : 6)},
                {"detail", is_import
                    ? "import " + symbol.type_name + (symbol.name == symbol.type_name ? "" : " as " + symbol.name)
                    : symbol.name + ": " + symbol.type_name},
                {"sortText", "0_" + symbol.name}
            };
            if (!identifier_prefix.empty() && symbol.name.starts_with(identifier_prefix)) item["preselect"] = true;
            items.push_back(std::move(item));
        }
    }

    // Keywords
    std::vector<std::string> kw = {
        "class", "struct", "interface", "export", "import", "from", "extends",
        "let", "const", "if", "else", "while", "for", "return", "async", "await",
        "override", "constructor", "private", "protected", "public"
    };
    if (!completing_decorator) {
        for (const auto& k : kw) {
            items.push_back({{"label", k}, {"kind", 14}, {"sortText", "4_" + k}}); // Keyword
        }

        items.push_back({
            {"label", "behaviour class"},
            {"kind", 15},
            {"detail", "Engine2D behaviour template"},
            {"insertTextFormat", 2},
            {"insertText", "@behaviour\\nexport class ${1:Name} extends Behaviour {\\n    $0\\n}"},
            {"sortText", "3_behaviour_class"}
        });
        for (const ApiFunctionDescriptor& function : ApiCatalog::instance().functions()) {
            if (function.availability != ApiAvailability::Available) continue;
            items.push_back({
                {"label", function.name},
                {"kind", 3},
                {"detail", callable_signature(
                    function.name, function.parameters,
                    static_cast<uint32_t>(function.parameters.size()),
                    static_cast<uint32_t>(function.parameters.size()), function.return_type)},
                {"insertTextFormat", 2},
                {"insertText", callable_snippet(
                    function.name, function.parameters, static_cast<uint32_t>(function.parameters.size()))},
                {"sortText", "2_" + function.name}
            });
        }
        items.push_back({
            {"label", "update"},
            {"kind", 15},
            {"detail", "EngineScript lifecycle method"},
            {"insertTextFormat", 2},
            {"insertText", "override update(deltaTime: Float): Void {\\n    $0\\n}"},
            {"sortText", "3_update"}
        });
    }

    // Decorators
    std::vector<std::string> decs = {"behaviour", "expose", "inject", "component", "optional", "range", "serializable"};
    for (const auto& d : decs) {
        json item = {{"label", "@" + d}, {"kind", 15}, {"sortText", "1_" + d}};
        if (d == "expose") {
            item["insertTextFormat"] = 2;
            item["insertText"] = "@expose({ min: ${1:0}, max: ${2:100} })";
        }
        items.push_back(std::move(item));
    }

    // Builtin Types
    if (!completing_decorator) {
        for (const ApiTypeDescriptor& type : ApiCatalog::instance().types()) {
            if (type.availability != ApiAvailability::Available) continue;
            items.push_back({
                {"label", type.name},
                {"kind", 7},
                {"detail", "EngineScript " + type.name},
                {"sortText", "5_" + type.name}
            });
        }
    }

    resp["result"] = items;
    return resp.dump();
}

static std::string extract_word_at(const std::string& line_text, uint32_t char_pos) {
    if (line_text.empty()) return "";
    int len = static_cast<int>(line_text.size());
    int pos = std::min(static_cast<int>(char_pos), len - 1);
    if (pos < 0) return "";

    // Scan backwards to include '@' or word boundary
    int start = pos;
    while (start > 0 && (isalnum(line_text[start - 1]) || line_text[start - 1] == '_' || line_text[start - 1] == '@')) {
        start--;
    }
    // Scan forwards
    int end = pos;
    while (end < len && (isalnum(line_text[end]) || line_text[end] == '_')) {
        end++;
    }
    if (start < end) {
        return line_text.substr(start, end - start);
    }
    return "";
}

std::string LanguageServer::on_hover(const std::string& id, const std::string& uri, uint32_t line, uint32_t character) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = parse_id(id);

    std::string line_content = m_workspace.source_manager().get_line_content(
        m_workspace.source_manager().find_file_id(source_path_for_uri(uri)), line + 1
    );
    std::string word = extract_word_at(line_content, character);

    std::string markdown = "";

    // 1. Decorators
    if (word == "@behaviour" || word == "behaviour") {
        markdown = "### `@behaviour`\n\n```enginescript\n@behaviour\nexport class <Name> extends Behaviour\n```\n\nMarks this class as an **Engine2D Behaviour** script component.\n\nParticipates in entity lifecycle events:\n- `update(deltaTime: Float): Void`\n- `on_start(): Void`\n- `on_destroy(): Void`";
    } else if (word == "@expose" || word == "expose") {
        markdown = "### `@expose`\n\n```enginescript\n@expose({ min?: Float, max?: Float, step?: Float })\n```\n\nExposes this field to the **Engine2D Inspector UI** for live editing and serializes it in scenes and prefabs.";
    } else if (word == "@inject" || word == "inject") {
        markdown = "### `@inject`\n\n```enginescript\n@inject private <param>: <ServiceType>\n```\n\nInjects an **Engine Application Service** (such as `Input`, `Audio`, `SceneManager`) via Constructor Dependency Injection.";
    } else if (word == "@component" || word == "component") {
        markdown = "### `@component`\n\n```enginescript\n@component private <param>: <ComponentType>\n```\n\nBinds a required **ECS Component** (e.g. `RigidBody2D`, `SpriteRenderer2D`) on the same entity. Fails validation if absent.";
    } else if (word == "@optional" || word == "optional") {
        markdown = "### `@optional`\n\n```enginescript\n@optional private <param>: <ComponentType>?\n```\n\nBinds an optional **ECS Component** handle that safely evaluates to `null` if not attached.";
    }
    // 2. Builtin Types
    else if (word == "Float") {
        markdown = "### `Float`\n\n64-bit IEEE-754 double precision floating-point number.";
    } else if (word == "Int") {
        markdown = "### `Int`\n\n64-bit signed integer numeric type.";
    } else if (word == "Bool") {
        markdown = "### `Bool`\n\nBoolean type representing `true` or `false`.";
    } else if (word == "String") {
        markdown = "### `String`\n\nUTF-8 encoded string.";
    } else if (word == "Void") {
        markdown = "### `Void`\n\nIndicates the absence of a value or return type.";
    } else if (word == "Vec2") {
        markdown = "### `Vec2`\n\n```enginescript\nstruct Vec2 { x: Float, y: Float }\n```\n\n2D vector used for positions, velocities, and 2D physics.";
    } else if (word == "Vec3") {
        markdown = "### `Vec3`\n\n```enginescript\nstruct Vec3 { x: Float, y: Float, z: Float }\n```\n\n3D vector representation.";
    } else if (word == "Duration") {
        markdown = "### `Duration`\n\nRepresents a time duration (e.g. `500.ms`, `2.seconds`).";
    } else if (word == "Task") {
        markdown = "### `Task<T>`\n\n```enginescript\ntype Task<T>\n```\n\nAsynchronous coroutine task scheduled on the engine's `AsyncScheduler`. Automatically cancelled if the owning entity is destroyed.";
    } else if (word == "delay") {
        markdown = "### `delay`\n\n```enginescript\nasync delay(duration: Duration): Task<Void>\n```\n\nSuspends coroutine execution for the specified duration (e.g. `500.ms`).";
    } else if (word == "Behaviour") {
        markdown = "### `Behaviour`\n\n```enginescript\nabstract class Behaviour\n```\n\nBase class for all EngineScript game components attached to entities.";
    } else if (word == "Input") {
        markdown = "### `Input` (Service)\n\nEngine input system for polling axis values (`axis(name)`) and button presses (`pressed(action)`).";
    } else if (word == "RigidBody2D") {
        markdown = "### `RigidBody2D` (Component)\n\n2D Box2D physics body component with `velocity: Vec2`, `isGrounded: Bool`, etc.";
    } else if (word == "Animator2D") {
        markdown = "### `Animator2D` (Component)\n\n2D sprite animation state controller with `play(clipName: String)`.";
    }
    // 3. Keywords
    else if (word == "let") {
        markdown = "### `let`\n\nDeclares a mutable block-scoped variable.";
    } else if (word == "const") {
        markdown = "### `const`\n\nDeclares an immutable constant variable.";
    } else if (word == "async") {
        markdown = "### `async`\n\nMarks a method or function as an asynchronous coroutine returning `Task<T>`.";
    } else if (word == "await") {
        markdown = "### `await`\n\nSuspends the current coroutine until the awaited `Task<T>` completes.";
    } else if (word == "override") {
        markdown = "### `override`\n\nExplicitly declares that a method overrides a virtual method in the base class.";
    } else if (word == "update") {
        markdown = "### `update(deltaTime: Float): Void`\n\nLifecycle method invoked every engine frame tick with elapsed delta time in seconds.";
    }

    // 4. Authored Workspace Symbols (Classes, Properties, Methods)
    if (markdown.empty()) {
        for (const auto& b : m_workspace.metadata_manifest().behaviours) {
            if (b.name == word) {
                markdown = "### `class " + b.name + " extends Behaviour`\n\n**Authored Behaviour Component**\n";
                if (!b.properties.empty()) {
                    markdown += "\n**Exposed Properties:**\n";
                    for (const auto& p : b.properties) {
                        markdown += "- `" + p.name + ": " + p.type + "`\n";
                    }
                }
                if (!b.methods.empty()) {
                    markdown += "\n**Methods:**\n";
                    for (const auto& m : b.methods) {
                        markdown += "- `" + m + "()`\n";
                    }
                }
                break;
            }
            for (const auto& p : b.properties) {
                if (p.name == word) {
                    markdown = "### `(property) " + b.name + "." + p.name + ": " + p.type + "`\n\nExposed field in the Engine2D Inspector and serialized in scenes.";
                    break;
                }
            }
            if (!markdown.empty()) break;
            for (const auto& m : b.methods) {
                if (m == word) {
                    markdown = "### `(method) " + b.name + "." + m + "()`\n\nAuthored Behaviour method.";
                    break;
                }
            }
            if (!markdown.empty()) break;
        }
    }

    if (markdown.empty()) {
        markdown = "**`" + (word.empty() ? "EngineScript Symbol" : word) + "`**\n\nEngineScript gameplay language symbol.";
    }

    const FileId file_id = m_workspace.source_manager().find_file_id(source_path_for_uri(uri));
    const SourceFile* file = m_workspace.source_manager().get_file(file_id);
    if (file && line < file->line_offsets.size()) {
        const uint32_t byte_offset = file->line_offsets[line] +
            std::min(character, static_cast<uint32_t>(line_content.size()));
        auto inferred_type = m_workspace.inferred_type_at(file_id, byte_offset);
        if (!inferred_type && !word.empty()) {
            inferred_type = m_workspace.symbol_type_at(file_id, word, byte_offset);
        }
        if (!inferred_type) {
            inferred_type = infer_initializer_type(*file, word, line);
        }
        if (inferred_type) {
            markdown += "\n\n**Inferred type:** `" + *inferred_type + "`";
        }
    }

    json hover_obj;
    hover_obj["contents"] = {
        {"kind", "markdown"},
        {"value", markdown}
    };
    resp["result"] = hover_obj;
    return resp.dump();
}

std::string LanguageServer::on_definition(const std::string& id, const std::string& uri, uint32_t line, uint32_t character) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = parse_id(id);

    const FileId file_id = m_workspace.source_manager().find_file_id(source_path_for_uri(uri));
    const std::string line_content = m_workspace.source_manager().get_line_content(file_id, line + 1);
    const std::string word = extract_word_at(line_content, character);
    const auto declaration = find_declaration(m_workspace.source_manager(), word);
    if (!declaration) {
        resp["result"] = nullptr;
        return resp.dump();
    }

    std::string target_uri = uri;
    for (const auto& [known_uri, source_path] : m_uri_to_source_path) {
        if (source_path == declaration->source_path) {
            target_uri = known_uri;
            break;
        }
    }

    resp["result"] = {
        {"uri", target_uri},
        {"range", {
            {"start", {{"line", declaration->line}, {"character", declaration->character}}},
            {"end", {{"line", declaration->line}, {"character", declaration->character + declaration->length}}}
        }}
    };
    return resp.dump();
}

} // namespace EngineScript
