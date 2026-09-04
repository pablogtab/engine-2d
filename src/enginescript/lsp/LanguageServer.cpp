#include "LanguageServer.hpp"
#include "enginescript/api/ApiCatalog.hpp"
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/parser/Parser.hpp"
#include "enginescript/types/NumericTypes.hpp"
#include <nlohmann/json.hpp>
#include <cctype>
#include <filesystem>
#include <format>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>

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

std::optional<uint32_t> byte_offset_at(
    const SourceManager& source_manager,
    FileId file_id,
    uint32_t line,
    uint32_t character
) {
    const SourceFile* file = source_manager.get_file(file_id);
    if (!file || line >= file->line_offsets.size()) return std::nullopt;
    const uint32_t line_start = file->line_offsets[line];
    const uint32_t line_end = line + 1 < file->line_offsets.size()
        ? file->line_offsets[line + 1]
        : static_cast<uint32_t>(file->content.size());
    return line_start + std::min(character, line_end - line_start);
}

json lsp_range(const SourceSpan& span) {
    return {
        {"start", {{"line", span.start_line - 1}, {"character", span.start_column - 1}}},
        {"end", {{"line", span.end_line - 1}, {"character", span.end_column - 1}}}
    };
}

std::optional<SourceSpan> exact_identifier_span(
    const SourceManager& source_manager,
    const SourceSpan& containing_span,
    std::string_view name
) {
    const SourceFile* file = source_manager.get_file(containing_span.file_id);
    if (!file || name.empty() || containing_span.start_offset >= file->content.size()) return std::nullopt;
    const std::size_t end = std::min<std::size_t>(containing_span.end_offset, file->content.size());
    std::size_t position = containing_span.start_offset;
    const auto is_identifier = [](char value) {
        return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
    };
    while ((position = file->content.find(name, position)) != std::string::npos && position < end) {
        const std::size_t name_end = position + name.size();
        if ((position == 0 || !is_identifier(file->content[position - 1])) &&
            (name_end >= file->content.size() || !is_identifier(file->content[name_end])) &&
            name_end <= end) {
            return source_manager.get_span(containing_span.file_id, static_cast<uint32_t>(position),
                                           static_cast<uint32_t>(name_end));
        }
        position = name_end;
    }
    return std::nullopt;
}

uint32_t semantic_token_type(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Class: return 2;
        case SymbolKind::Enum: return 3;
        case SymbolKind::Interface: return 4;
        case SymbolKind::Struct: return 5;
        case SymbolKind::TypeParameter: return 6;
        case SymbolKind::Parameter: return 7;
        case SymbolKind::Variable: return 8;
        case SymbolKind::Field: return 9;
        case SymbolKind::EnumMember: return 10;
        case SymbolKind::Function: return 11;
        case SymbolKind::Method: return 12;
        case SymbolKind::Import: return 8;
    }
    return 8;
}

bool is_member_call(std::string_view source, uint32_t offset) {
    std::size_t position = offset;
    const auto skip_whitespace = [&]() {
        while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position]))) {
            ++position;
        }
    };
    skip_whitespace();
    if (position < source.size() && source[position] == '(') return true;
    if (position >= source.size() || source[position] != '<') return false;

    uint32_t generic_depth = 0;
    do {
        if (source[position] == '<') ++generic_depth;
        else if (source[position] == '>') --generic_depth;
        ++position;
    } while (position < source.size() && generic_depth != 0);
    if (generic_depth != 0) return false;
    skip_whitespace();
    return position < source.size() && source[position] == '(';
}

std::string base_type_name(std::string type_name) {
    const size_t generic_start = type_name.find('<');
    if (generic_start != std::string::npos) type_name.resize(generic_start);
    if (!type_name.empty() && type_name.back() == '?') type_name.pop_back();
    return type_name;
}

bool span_contains_offset(const SourceSpan& span, uint32_t offset) {
    return span.is_valid() && offset >= span.start_offset && offset <= span.end_offset;
}

bool is_void_return(const std::optional<TypeAnnotation>& return_type) {
    return !return_type ||
        (return_type->name == "Void" && !return_type->is_nullable && return_type->generic_args.empty());
}

bool is_inside_void_callable(const ModuleNode& module, uint32_t offset) {
    for (const auto& declaration : module.declarations) {
        if (const auto* function = dynamic_cast<const FunctionDecl*>(declaration.get());
            function && function->body && span_contains_offset(function->body->span, offset)) {
            return is_void_return(function->return_type);
        }

        const auto inspect_owner = [&](const auto* owner) -> std::optional<bool> {
            if (!owner) return std::nullopt;
            if constexpr (requires { owner->constructor_decl; }) {
                if (owner->constructor_decl && owner->constructor_decl->body &&
                    span_contains_offset(owner->constructor_decl->body->span, offset)) {
                    return true;
                }
            }
            for (const auto& method : owner->methods) {
                if (method->body && span_contains_offset(method->body->span, offset)) {
                    return is_void_return(method->return_type);
                }
            }
            return std::nullopt;
        };
        if (const auto result = inspect_owner(dynamic_cast<const ClassDecl*>(declaration.get()))) return *result;
        if (const auto result = inspect_owner(dynamic_cast<const StructDecl*>(declaration.get()))) return *result;
    }
    return false;
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

std::string api_method_hover(const ApiMethodDescriptor& method) {
    std::string return_type = method.return_type;
    if (method.nullable_return && !return_type.ends_with('?')) return_type += '?';
    const std::string signature = method.owner_type + "." + callable_signature(
        method.name, method.parameters, method.min_arguments, method.max_arguments, return_type
    );
    const std::string documentation = method.documentation.empty()
        ? "Public EngineScript API method."
        : method.documentation;
    return "### `" + signature + "`\n\n```enginescript\n" + signature +
        "\n```\n\n" + documentation;
}

std::string api_property_hover(const ApiPropertyDescriptor& property) {
    const std::string type = property.type + (property.nullable && !property.type.ends_with('?') ? "?" : "");
    const std::string signature = property.owner_type + "." + property.name + ": " + type;
    const std::string access = property.readable && property.writable ? "read/write" :
        property.readable ? "read-only" : "write-only";
    const std::string documentation = property.documentation.empty()
        ? "Public EngineScript API property (" + access + ")."
        : property.documentation;
    return "### `" + signature + "`\n\n" + documentation;
}

std::string numeric_conversion_signature(std::string_view name) {
    return std::string(name) + "(value: Numeric) -> " + std::string(name);
}

std::string numeric_conversion_documentation(std::string_view name, const NumericTypeInfo& info) {
    std::string documentation = "Explicit numeric conversion to `" + std::string(name) + "` (" +
        std::to_string(info.bit_width) + "-bit " +
        (info.is_integer ? (info.is_signed ? "signed integer" : "unsigned integer") : "IEEE-754 floating point") +
        ", range " + std::string(info.range_description) + ").\n\n";
    if (info.is_integer) {
        documentation += "Integer inputs saturate. Floating inputs truncate toward zero and saturate; NaN becomes 0 and infinities become the destination limits.";
    } else {
        documentation += "Uses IEEE rounding to the nearest representable value and preserves NaN and infinities.";
    }
    return documentation;
}

std::string string_conversion_signature() {
    return "String(value: Numeric) -> String";
}

std::string string_conversion_documentation() {
    return "Explicitly formats any EngineScript numeric value as UTF-8 text. "
        "This conversion is required before concatenating a number with String.";
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

uint32_t active_parameter_index(
    std::string_view call_prefix,
    const std::vector<std::string>& parameter_names
) {
    const std::size_t open = call_prefix.rfind('(');
    if (open == std::string_view::npos) return 0;
    const std::string_view arguments = call_prefix.substr(open + 1);
    const std::size_t current_start = arguments.rfind(',') == std::string_view::npos
        ? 0 : arguments.rfind(',') + 1;
    const std::string current(arguments.substr(current_start));
    static const std::regex named_pattern(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:)");
    std::smatch named;
    if (std::regex_search(current, named, named_pattern)) {
        const auto found = std::find(parameter_names.begin(), parameter_names.end(), named[1].str());
        if (found != parameter_names.end()) return static_cast<uint32_t>(found - parameter_names.begin());
    }
    return static_cast<uint32_t>(std::count(arguments.begin(), arguments.end(), ','));
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
    if (!resolved_type) {
        if (const auto typed_symbol = find_typed_symbol(*file, receiver)) {
            resolved_type = typed_symbol->second;
        }
    }
    if (const auto symbol_type = workspace.symbol_type_at(file->id, receiver, receiver_offset);
        symbol_type && *symbol_type != "<inferred>") {
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
    m_workspace.discover_project_sources();
    static_cast<void>(m_workspace.compile());
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
        if (method == "workspace/didChangeWatchedFiles") {
            on_watched_files_changed(j.value("params", json::object()).dump());
            return "";
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
        if (method == "textDocument/signatureHelp") {
            std::string uri = j["params"]["textDocument"].value("uri", "");
            uint32_t line = j["params"]["position"].value("line", 0);
            uint32_t char_pos = j["params"]["position"].value("character", 0);
            return on_signature_help(id, uri, line, char_pos);
        }
        if (method == "textDocument/references") {
            return on_references(id, j["params"]["textDocument"].value("uri", ""),
                j["params"]["position"].value("line", 0), j["params"]["position"].value("character", 0),
                j["params"].value("context", json::object()).value("includeDeclaration", true));
        }
        if (method == "textDocument/rename") {
            return on_rename(id, j["params"]["textDocument"].value("uri", ""),
                j["params"]["position"].value("line", 0), j["params"]["position"].value("character", 0),
                j["params"].value("newName", ""));
        }
        if (method == "textDocument/formatting") {
            return on_formatting(id, j["params"]["textDocument"].value("uri", ""));
        }
        if (method == "textDocument/codeAction") {
            return on_code_action(id, j["params"]["textDocument"].value("uri", ""), j["params"].dump());
        }
        if (method == "workspace/symbol") {
            return on_workspace_symbol(id, j["params"].value("query", ""));
        }
        if (method == "textDocument/semanticTokens/full") {
            return on_semantic_tokens(id, j["params"]["textDocument"].value("uri", ""));
        }
        if (method == "textDocument/inlayHint") {
            return on_inlay_hints(id, j["params"]["textDocument"].value("uri", ""));
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
    caps["signatureHelpProvider"] = {{"triggerCharacters", {"(", ",", "<"}}};
    caps["documentSymbolProvider"] = true;
    caps["referencesProvider"] = true;
    caps["renameProvider"] = { {"prepareProvider", false} };
    caps["documentFormattingProvider"] = true;
    caps["codeActionProvider"] = { {"codeActionKinds", {"quickfix"}} };
    caps["workspaceSymbolProvider"] = true;
    caps["semanticTokensProvider"] = {
        {"legend", {
            {"tokenTypes", {"namespace", "type", "class", "enum", "interface", "struct", "typeParameter", "parameter", "variable", "property", "enumMember", "function", "method", "keyword", "string", "number", "comment", "decorator"}},
            {"tokenModifiers", {"declaration", "readonly", "async", "static"}}
        }},
        {"full", true}
    };
    caps["inlayHintProvider"] = true;

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
    if (!m_workspace.restore_source_file_from_disk(source_path)) {
        m_workspace.remove_source_file(source_path);
    }
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

void LanguageServer::on_watched_files_changed(const std::string& params_json) {
    json params;
    try { params = json::parse(params_json); } catch (...) { return; }
    bool changed = false;
    for (const auto& event : params.value("changes", json::array())) {
        const std::string uri = event.value("uri", "");
        if (uri.empty()) continue;
        const std::string path = source_path_for_uri(uri);
        if (!path.ends_with(".e2s") || !m_workspace.source_manager().is_within_project(path)) continue;
        const int kind = event.value("type", 2); // 1 create, 2 change, 3 delete
        if (m_open_document_versions.contains(uri)) continue; // unsaved overlay wins
        if (kind == 3) m_workspace.remove_source_file(path);
        else m_workspace.restore_source_file_from_disk(path);
        changed = true;
    }
    if (!changed) return;
    for (const auto& [uri, version] : m_open_document_versions) {
        static_cast<void>(version);
        m_pending_notifications.push_back(publish_diagnostics(uri));
    }
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

std::string LanguageServer::uri_for_source_path(const std::string& source_path) const {
    for (const auto& [uri, known_path] : m_uri_to_source_path) {
        if (known_path == source_path) return uri;
    }
    std::string path = (std::filesystem::path(m_workspace.project_root()) / source_path).generic_string();
    std::string encoded;
    for (const unsigned char character : path) {
        if (std::isalnum(character) || character == '/' || character == '-' || character == '_' || character == '.') {
            encoded.push_back(static_cast<char>(character));
        } else {
            constexpr char hex[] = "0123456789ABCDEF";
            encoded += '%';
            encoded += hex[(character >> 4) & 0xF];
            encoded += hex[character & 0xF];
        }
    }
    return "file://" + encoded;
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
    const std::string source_path = source_path_for_uri(uri);
    const SourceFile* source_file = m_workspace.source_manager().get_file(source_path);

    // Import paths are module references, not ordinary expression/type
    // contexts. Only project EngineScript modules are meaningful here.
    static const std::regex import_path_pattern(
        R"(^\s*import\s*\{[^}]*\}\s*from\s*[\"']([^\"']*)$)"
    );
    std::smatch import_path_match;
    if (std::regex_search(line_prefix, import_path_match, import_path_pattern)) {
        const std::filesystem::path importer_directory = std::filesystem::path(source_path).parent_path();
        const std::string typed_path = import_path_match[1].str();
        const uint32_t replacement_start = static_cast<uint32_t>(import_path_match.position(1));
        for (const std::string& registered_path : m_workspace.registered_files()) {
            if (registered_path == source_path || !registered_path.ends_with(".e2s")) continue;
            std::filesystem::path relative = std::filesystem::path(registered_path).lexically_relative(importer_directory);
            relative.replace_extension();
            std::string module_path = relative.generic_string();
            if (!module_path.starts_with(".")) module_path = "./" + module_path;
            if (!typed_path.empty() && !module_path.starts_with(typed_path)) continue;
            items.push_back({
                {"label", module_path},
                {"kind", 17},
                {"detail", "EngineScript module " + registered_path},
                {"sortText", "0_" + module_path},
                {"textEdit", {
                    {"newText", module_path},
                    {"range", {
                        {"start", {{"line", line}, {"character", replacement_start}}},
                        {"end", {{"line", line}, {"character", character}}}
                    }}
                }}
            });
        }
        resp["result"] = items;
        return resp.dump();
    }

    // Between import braces, completion is restricted to exports of the
    // selected module. Builtins and unrelated workspace symbols must not leak.
    const std::size_t import_open = line_content.find('{');
    const std::size_t import_close = line_content.find('}', import_open == std::string::npos ? 0 : import_open + 1);
    static const std::regex import_module_pattern(R"(\bfrom\s*[\"']([^\"']+)[\"'])");
    std::smatch import_module_match;
    if (line_content.find("import") != std::string::npos && import_open != std::string::npos &&
        import_close != std::string::npos && character > import_open && character <= import_close &&
        std::regex_search(line_content, import_module_match, import_module_pattern)) {
        const auto target = m_workspace.source_manager().resolve_import(source_path, import_module_match[1].str());
        if (target) {
            const std::string imported_text = line_content.substr(import_open + 1, import_close - import_open - 1);
            for (const WorkspaceExportedSymbol& symbol : m_workspace.exports_from(*target)) {
                const std::regex already_imported("\\b" + symbol.name + "\\b");
                if (std::regex_search(imported_text, already_imported)) continue;
                const int kind = symbol.kind == WorkspaceExportedSymbolKind::Function ? 3
                    : symbol.kind == WorkspaceExportedSymbolKind::Constant ? 21
                    : symbol.kind == WorkspaceExportedSymbolKind::Enum ? 13
                    : symbol.kind == WorkspaceExportedSymbolKind::Interface ? 8
                    : symbol.kind == WorkspaceExportedSymbolKind::Struct ? 22 : 7;
                items.push_back({
                    {"label", symbol.name},
                    {"kind", kind},
                    {"detail", "export from " + symbol.module_path},
                    {"sortText", "0_" + symbol.name}
                });
            }
        }
        resp["result"] = items;
        return resp.dump();
    }

    if (const auto receiver_type = infer_receiver_type(
            m_workspace, source_path, line, line_prefix)) {
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
        "override", "constructor", "private", "protected", "public", "enum", "match",
        "guard", "break", "continue", "true", "false", "null"
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
            const uint32_t minimum_arguments = static_cast<uint32_t>(std::count_if(
                function.parameters.begin(), function.parameters.end(),
                [](const auto& parameter) { return !parameter.optional; }
            ));
            items.push_back({
                {"label", function.name},
                {"kind", 3},
                {"detail", callable_signature(
                    function.name, function.parameters,
                    minimum_arguments,
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
        items.push_back({
            {"label", "guard let"},
            {"kind", 15},
            {"detail", "Unwrap a nullable value or terminate the current flow"},
            {"insertTextFormat", 2},
            {"insertText", "guard (let ${1:value} = ${2:optional}) else {\n    ${3:return};\n}\n$0"},
            {"sortText", "3_guard_let"}
        });
        items.push_back({
            {"label", "match expression"},
            {"kind", 15},
            {"detail", "Exhaustive match expression"},
            {"insertTextFormat", 2},
            {"insertText", "match (${1:value}) {\n    ${2:pattern} => ${3:result},\n    _ => ${4:fallback}\n}$0"},
            {"sortText", "3_match_expression"}
        });
        items.push_back({
            {"label", "interpolated string"},
            {"kind", 15},
            {"detail", "EngineScript interpolated string"},
            {"insertTextFormat", 2},
            {"insertText", "`${1:Text }\\${${2:value}}`$0"},
            {"sortText", "3_interpolated_string"}
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
            json item = {
                {"label", type.name},
                {"kind", 7},
                {"detail", "EngineScript " + type.name},
                {"sortText", "5_" + type.name}
            };
            if (const auto numeric_kind = numeric_type_for_name(type.name)) {
                const NumericTypeInfo* info = numeric_type_info(*numeric_kind);
                item["detail"] = numeric_conversion_signature(type.name);
                item["documentation"] = numeric_conversion_documentation(type.name, *info);
            } else if (type.name == "String") {
                item["detail"] = string_conversion_signature();
                item["documentation"] = string_conversion_documentation();
            }
            items.push_back(std::move(item));
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
        markdown = "### `@behaviour`\n\n```enginescript\n@behaviour\nexport class <Name> extends Behaviour\n```\n\nMarks this class as an **Engine2D Behaviour** script component.\n\nParticipates in entity lifecycle events:\n- `update(deltaTime: Float): Void`\n- `on_start(): Void`\n- `on_destroy(): Void`\n- `on_collision_enter(other: Entity): Void`\n- `on_button_clicked(): Void`";
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
    else if (const auto numeric_kind = numeric_type_for_name(word)) {
        const NumericTypeInfo* info = numeric_type_info(*numeric_kind);
        markdown = "### `" + word + "`\n\n```enginescript\n" +
            numeric_conversion_signature(word) + "\n```\n\n" +
            numeric_conversion_documentation(word, *info);
    } else if (word == "Bool") {
        markdown = "### `Bool`\n\nBoolean type representing `true` or `false`.";
    } else if (word == "String") {
        markdown = "### `String`\n\n```enginescript\n" + string_conversion_signature() +
            "\n```\n\n" + string_conversion_documentation();
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

    const FileId file_id = m_workspace.source_manager().find_file_id(source_path_for_uri(uri));
    const SourceFile* file = m_workspace.source_manager().get_file(file_id);
    if (file && line < file->line_offsets.size()) {
        if (markdown.empty() && !word.empty()) {
            const std::size_t word_start = line_content.rfind(word, std::min<std::size_t>(character, line_content.size()));
            if (word_start != std::string::npos) {
                const std::string receiver_prefix = line_content.substr(0, word_start);
                if (const auto receiver_type = infer_receiver_type(
                        m_workspace, source_path_for_uri(uri), line, receiver_prefix)) {
                    if (const ApiMethodDescriptor* method = ApiCatalog::instance().find_available_method(*receiver_type, word)) {
                        markdown = api_method_hover(*method);
                    } else if (const ApiPropertyDescriptor* property =
                                   ApiCatalog::instance().find_available_property(*receiver_type, word)) {
                        markdown = api_property_hover(*property);
                    }
                }
            }
        }
        const uint32_t byte_offset = file->line_offsets[line] +
            std::min(character, static_cast<uint32_t>(line_content.size()));
        auto inferred_type = m_workspace.inferred_type_at(file_id, byte_offset);
        if (!inferred_type && !word.empty()) {
            inferred_type = m_workspace.symbol_type_at(file_id, word, byte_offset);
        }
        if (!inferred_type) {
            inferred_type = infer_initializer_type(*file, word, line);
        }
        if (inferred_type && (*inferred_type == "<type-error>" || *inferred_type == "Void")) {
            inferred_type.reset();
        }
        if (inferred_type) {
            markdown += "\n\n**Inferred type:** `" + *inferred_type + "`";
        }
    }

    if (markdown.empty()) {
        resp["result"] = nullptr;
        return resp.dump();
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
    const auto byte_offset = byte_offset_at(m_workspace.source_manager(), file_id, line, character);
    const auto symbol_id = byte_offset ? m_workspace.symbol_id_at(file_id, *byte_offset) : std::nullopt;
    const auto occurrences = symbol_id
        ? m_workspace.occurrences_for_symbol(*symbol_id)
        : std::vector<WorkspaceSymbolOccurrence>{};
    const auto declaration = std::find_if(occurrences.begin(), occurrences.end(), [](const auto& occurrence) {
        return occurrence.is_declaration;
    });
    if (declaration == occurrences.end()) {
        resp["result"] = nullptr;
        return resp.dump();
    }

    const SourceFile* target_file = m_workspace.source_manager().get_file(declaration->span.file_id);
    if (!target_file) {
        resp["result"] = nullptr;
        return resp.dump();
    }

    resp["result"] = {
        {"uri", uri_for_source_path(target_file->path)},
        {"range", lsp_range(declaration->span)}
    };
    return resp.dump();
}

std::string LanguageServer::on_signature_help(
    const std::string& id, const std::string& uri, uint32_t line, uint32_t character
) {
    json response{{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", nullptr}};
    const FileId file_id = m_workspace.source_manager().find_file_id(source_path_for_uri(uri));
    const std::string content = m_workspace.source_manager().get_line_content(file_id, line + 1);
    const std::string prefix = content.substr(0, std::min<std::size_t>(character, content.size()));
    static const std::regex call_pattern(R"(([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^>]*>)?\s*\([^()]*$)");
    std::smatch match;
    if (!std::regex_search(prefix, match, call_pattern)) return response.dump();

    const std::string name = match[1].str();
    if (const auto numeric_kind = numeric_type_for_name(name)) {
        const NumericTypeInfo* info = numeric_type_info(*numeric_kind);
        response["result"] = {
            {"signatures", {{
                {"label", numeric_conversion_signature(name)},
                {"documentation", numeric_conversion_documentation(name, *info)},
                {"parameters", {{{"label", "value: Numeric"},
                    {"documentation", "Any EngineScript numeric value."}}}}
            }}},
            {"activeSignature", 0},
            {"activeParameter", 0}
        };
        return response.dump();
    }
    if (name == "String") {
        response["result"] = {
            {"signatures", {{
                {"label", string_conversion_signature()},
                {"documentation", string_conversion_documentation()},
                {"parameters", {{{"label", "value: Numeric"},
                    {"documentation", "Any EngineScript numeric value."}}}}
            }}},
            {"activeSignature", 0},
            {"activeParameter", 0}
        };
        return response.dump();
    }
    for (const ApiFunctionDescriptor& function : ApiCatalog::instance().functions()) {
        if (function.availability != ApiAvailability::Available || function.name != name) continue;
        json parameters = json::array();
        std::vector<std::string> parameter_names;
        for (const auto& parameter : function.parameters) {
            parameters.push_back({{"label", parameter_signature(parameter)}});
            parameter_names.push_back(parameter.name);
        }
        const uint32_t minimum = static_cast<uint32_t>(std::count_if(function.parameters.begin(), function.parameters.end(),
            [](const auto& parameter) { return !parameter.optional; }));
        response["result"] = {{"signatures", {{{"label", callable_signature(name, function.parameters,
            minimum, static_cast<uint32_t>(function.parameters.size()), function.return_type)},
            {"parameters", parameters}}}}, {"activeSignature", 0},
            {"activeParameter", active_parameter_index(prefix, parameter_names)}};
        return response.dump();
    }
    for (const ApiMethodDescriptor& method : ApiCatalog::instance().methods()) {
        if (method.availability != ApiAvailability::Available || method.name != name) continue;
        json parameters = json::array();
        std::vector<std::string> parameter_names;
        for (const auto& parameter : method.parameters) {
            parameters.push_back({{"label", parameter_signature(parameter)}});
            parameter_names.push_back(parameter.name);
        }
        response["result"] = {{"signatures", {{{"label", callable_signature(name, method.parameters, method.min_arguments,
            method.max_arguments, method.return_type)}, {"parameters", parameters}}}}, {"activeSignature", 0},
            {"activeParameter", active_parameter_index(prefix, parameter_names)}};
        return response.dump();
    }

    for (const SourceFile& source : m_workspace.source_manager().all_files()) {
        DiagnosticEngine recovery_diagnostics;
        Lexer lexer(source.id, source.content, recovery_diagnostics);
        Parser parser(source.id, source.path, lexer.tokenize_all(), recovery_diagnostics);
        const auto module = parser.parse_module();
        if (!module) continue;
        const auto make_signature = [&](const auto& parameters, std::string_view return_type) {
            std::string label = name + "(";
            json parameter_items = json::array();
            std::vector<std::string> parameter_names;
            for (std::size_t index = 0; index < parameters.size(); ++index) {
                if (index > 0) label += ", ";
                const auto& parameter = parameters[index];
                std::string parameter_label = parameter->name + ": " + parameter->type_annotation.to_string();
                if (parameter->default_value && parameter->default_value->span.end_offset <= source.content.size()) {
                    parameter_label += " = " + source.content.substr(
                        parameter->default_value->span.start_offset,
                        parameter->default_value->span.end_offset - parameter->default_value->span.start_offset
                    );
                }
                label += parameter_label;
                parameter_items.push_back({{"label", parameter_label}});
                parameter_names.push_back(parameter->name);
            }
            label += ") -> " + std::string(return_type);
            response["result"] = {{"signatures", {{{"label", label}, {"parameters", parameter_items}}}},
                {"activeSignature", 0}, {"activeParameter", active_parameter_index(prefix, parameter_names)}};
        };
        for (const auto& declaration : module->declarations) {
            if (const auto* function = dynamic_cast<const FunctionDecl*>(declaration.get());
                function && function->name == name) {
                make_signature(function->params, function->return_type ? function->return_type->to_string() : "Void");
                return response.dump();
            }
            if (const auto* class_declaration = dynamic_cast<const ClassDecl*>(declaration.get());
                class_declaration && class_declaration->name == name && class_declaration->constructor_decl) {
                make_signature(class_declaration->constructor_decl->params, class_declaration->name);
                return response.dump();
            }
            const auto inspect_members = [&](const auto* owner) {
                if (!owner) return false;
                for (const auto& method : owner->methods) {
                    if (method->name != name) continue;
                    make_signature(method->params, method->return_type ? method->return_type->to_string() : "Void");
                    return true;
                }
                return false;
            };
            if (inspect_members(dynamic_cast<const ClassDecl*>(declaration.get())) ||
                inspect_members(dynamic_cast<const StructDecl*>(declaration.get()))) return response.dump();
        }
    }
    return response.dump();
}

std::string LanguageServer::on_references(
    const std::string& id,
    const std::string& uri,
    uint32_t line,
    uint32_t character,
    bool include_declaration
) {
    json result = json::array();
    const FileId file_id = m_workspace.source_manager().find_file_id(source_path_for_uri(uri));
    const auto byte_offset = byte_offset_at(m_workspace.source_manager(), file_id, line, character);
    const auto symbol_id = byte_offset ? m_workspace.symbol_id_at(file_id, *byte_offset) : std::nullopt;
    if (symbol_id) {
        for (const WorkspaceSymbolOccurrence& occurrence : m_workspace.occurrences_for_symbol(*symbol_id)) {
            if (!include_declaration && occurrence.is_declaration) continue;
            const SourceFile* file = m_workspace.source_manager().get_file(occurrence.span.file_id);
            if (!file) continue;
            result.push_back({
                {"uri", uri_for_source_path(file->path)},
                {"range", lsp_range(occurrence.span)}
            });
        }
    }
    return json{{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", std::move(result)}}.dump();
}

std::string LanguageServer::on_rename(
    const std::string& id,
    const std::string& uri,
    uint32_t line,
    uint32_t character,
    const std::string& new_name
) {
    static const std::regex valid_identifier(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
    json response{{"jsonrpc", "2.0"}, {"id", parse_id(id)}};
    if (!std::regex_match(new_name, valid_identifier)) {
        response["error"] = {{"code", -32602}, {"message", "The rename target is not a valid EngineScript identifier."}};
        return response.dump();
    }
    const FileId file_id = m_workspace.source_manager().find_file_id(source_path_for_uri(uri));
    const auto byte_offset = byte_offset_at(m_workspace.source_manager(), file_id, line, character);
    const auto symbol_id = byte_offset ? m_workspace.symbol_id_at(file_id, *byte_offset) : std::nullopt;
    json changes = json::object();
    if (symbol_id) {
        for (const WorkspaceSymbolOccurrence& occurrence : m_workspace.occurrences_for_symbol(*symbol_id)) {
            const SourceFile* file = m_workspace.source_manager().get_file(occurrence.span.file_id);
            if (!file) continue;
            const std::string occurrence_uri = uri_for_source_path(file->path);
            if (!changes.contains(occurrence_uri)) changes[occurrence_uri] = json::array();
            changes[occurrence_uri].push_back({
                {"range", lsp_range(occurrence.span)},
                {"newText", new_name}
            });
        }
    }
    response["result"] = {{"changes", std::move(changes)}};
    return response.dump();
}

std::string LanguageServer::on_formatting(const std::string& id, const std::string& uri) {
    json edits = json::array();
    const SourceFile* file = m_workspace.source_manager().get_file(source_path_for_uri(uri));
    if (!file) return json{{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", edits}}.dump();
    std::unordered_set<uint32_t> invalid_lines;
    for (const Diagnostic& diagnostic : m_workspace.diagnostic_engine().get_diagnostics()) {
        if (!diagnostic.is_error() || diagnostic.span.file_id != file->id ||
            (diagnostic.code.starts_with("E2") || diagnostic.code.starts_with("E3"))) continue;
        const uint32_t first_line = diagnostic.span.start_line > 0 ? diagnostic.span.start_line - 1 : 0;
        const uint32_t last_line = diagnostic.span.end_line > 0 ? diagnostic.span.end_line - 1 : first_line;
        for (uint32_t invalid_line = first_line; invalid_line <= last_line; ++invalid_line) {
            invalid_lines.emplace(invalid_line);
        }
    }

    std::istringstream input(file->content);
    std::string line;
    std::string formatted;
    int indent = 0;
    uint32_t line_count = 0;
    bool in_block_comment = false;
    bool in_template = false;
    while (std::getline(input, line)) {
        ++line_count;
        const std::size_t first = line.find_first_not_of(" \t\r");
        std::string trimmed = first == std::string::npos ? std::string{} : line.substr(first);
        std::size_t leading_closes = 0;
        if (!in_block_comment && !in_template) {
            while (leading_closes < trimmed.size() && trimmed[leading_closes] == '}') ++leading_closes;
        }
        const int line_indent = std::max(0, indent - static_cast<int>(leading_closes));
        if (invalid_lines.contains(line_count - 1) || in_template) {
            formatted += line;
        } else {
            if (!trimmed.empty()) formatted.append(static_cast<std::size_t>(line_indent) * 4, ' ');
            formatted += trimmed;
        }
        formatted += '\n';

        bool in_string = false;
        bool escaped = false;
        int delta = 0;
        for (std::size_t index = 0; index < trimmed.size(); ++index) {
            const char character_value = trimmed[index];
            if (in_block_comment) {
                if (character_value == '*' && index + 1 < trimmed.size() && trimmed[index + 1] == '/') {
                    in_block_comment = false;
                    ++index;
                }
                continue;
            }
            if (!in_string && character_value == '`' && !escaped) {
                in_template = !in_template;
                continue;
            }
            if (in_template) {
                escaped = character_value == '\\' && !escaped;
                if (character_value != '\\') escaped = false;
                continue;
            }
            if (!in_string && character_value == '/' && index + 1 < trimmed.size()) {
                if (trimmed[index + 1] == '/') break;
                if (trimmed[index + 1] == '*') {
                    in_block_comment = true;
                    ++index;
                    continue;
                }
            }
            if (character_value == '"' && !escaped) in_string = !in_string;
            if (!in_string) {
                if (character_value == '{') ++delta;
                else if (character_value == '}') --delta;
            }
            escaped = character_value == '\\' && !escaped;
            if (character_value != '\\') escaped = false;
        }
        indent = std::max(0, indent + delta);
    }
    if (!file->content.ends_with('\n') && !formatted.empty()) formatted.pop_back();
    if (formatted != file->content) {
        edits.push_back({
            {"range", {
                {"start", {{"line", 0}, {"character", 0}}},
                {"end", {{"line", line_count + 1}, {"character", 0}}}
            }},
            {"newText", formatted}
        });
    }
    return json{{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", std::move(edits)}}.dump();
}

std::string LanguageServer::on_code_action(
    const std::string& id, const std::string& uri, const std::string& params_json
) {
    json actions = json::array();
    json params;
    try { params = json::parse(params_json); } catch (...) {
        return json{{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", actions}}.dump();
    }
    static const std::regex destination_pattern(R"(of type '([A-Za-z_][A-Za-z0-9_]*)')");
    static const std::regex unresolved_pattern(R"(Unresolved identifier '([A-Za-z_][A-Za-z0-9_]*)')");
    static const std::regex interface_pattern(
        R"('([A-Za-z_][A-Za-z0-9_]*)' does not implement (method|property) '([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)')"
    );
    const std::string current_source_path = source_path_for_uri(uri);
    const SourceFile* current_file = m_workspace.source_manager().get_file(current_source_path);
    for (const auto& diagnostic : params["context"].value("diagnostics", json::array())) {
        const std::string code = diagnostic.value("code", "");
        const std::string message = diagnostic.value("message", "");
        std::smatch match;
        if (code == "E3103" && current_file && diagnostic.contains("range")) {
            const json& range = diagnostic["range"];
            const auto start_offset = byte_offset_at(
                m_workspace.source_manager(), current_file->id,
                range["start"].value("line", 0U), range["start"].value("character", 0U));
            const auto end_offset = byte_offset_at(
                m_workspace.source_manager(), current_file->id,
                range["end"].value("line", 0U), range["end"].value("character", 0U));
            if (!start_offset || !end_offset || *end_offset <= *start_offset ||
                *end_offset > current_file->content.size()) {
                continue;
            }

            std::size_t closing_brace = *end_offset;
            while (closing_brace > *start_offset &&
                   std::isspace(static_cast<unsigned char>(current_file->content[closing_brace - 1]))) {
                --closing_brace;
            }
            if (closing_brace == *start_offset || current_file->content[closing_brace - 1] != '}') continue;
            --closing_brace;

            DiagnosticEngine recovery_diagnostics;
            Lexer lexer(current_file->id, current_file->content, recovery_diagnostics);
            Parser parser(current_file->id, current_file->path, lexer.tokenize_all(), recovery_diagnostics);
            const auto module = parser.parse_module();
            if (!module || !is_inside_void_callable(*module, *start_offset)) continue;

            const std::size_t line_start_position = current_file->content.rfind('\n', closing_brace);
            const std::size_t line_start = line_start_position == std::string::npos ? 0 : line_start_position + 1;
            const std::string prefix = current_file->content.substr(line_start, closing_brace - line_start);
            const bool brace_on_indented_line = std::all_of(prefix.begin(), prefix.end(), [](char value) {
                return value == ' ' || value == '\t';
            });
            const std::size_t indent_end = current_file->content.find_first_not_of(" \t", line_start);
            const std::string indent = current_file->content.substr(
                line_start,
                (indent_end == std::string::npos || indent_end > closing_brace ? closing_brace : indent_end) - line_start
            );
            const std::string insertion_text = brace_on_indented_line
                ? "    return;\n" + prefix
                : "\n" + indent + "    return;\n" + indent;
            const SourceLocation insertion = m_workspace.source_manager().get_location(
                current_file->id, static_cast<uint32_t>(closing_brace));
            const json insertion_range = {
                {"start", {{"line", insertion.line - 1}, {"character", insertion.column - 1}}},
                {"end", {{"line", insertion.line - 1}, {"character", insertion.column - 1}}}
            };
            actions.push_back({
                {"title", "Terminate guard else with return"},
                {"kind", "quickfix"},
                {"diagnostics", json::array({diagnostic})},
                {"edit", {{"changes", {{uri, json::array({{
                    {"range", insertion_range}, {"newText", insertion_text}
                }})}}}}}
            });
            continue;
        }

        if (code == "E3010" && std::regex_search(message, match, destination_pattern) && match[1].str() != "String" &&
            numeric_type_for_name(match[1].str())) {
            const json range = diagnostic["range"];
            json conversion_edits = json::array();
            conversion_edits.push_back({
                {"range", {{"start", range["start"]}, {"end", range["start"]}}},
                {"newText", match[1].str() + "("}
            });
            conversion_edits.push_back({
                {"range", {{"start", range["end"]}, {"end", range["end"]}}},
                {"newText", ")"}
            });
            actions.push_back({
                {"title", "Convert explicitly to " + match[1].str()},
                {"kind", "quickfix"},
                {"diagnostics", json::array({diagnostic})},
                {"edit", {{"changes", {{uri, std::move(conversion_edits)}}}}}
            });
            continue;
        }

        if (code == "E2030" && std::regex_search(message, match, unresolved_pattern)) {
            const std::string symbol_name = match[1].str();
            for (const WorkspaceExportedSymbol& symbol : m_workspace.workspace_symbols()) {
                if (!symbol.is_exported || symbol.name != symbol_name || symbol.module_path == current_source_path) continue;
                std::filesystem::path relative = std::filesystem::path(symbol.module_path).replace_extension();
                const std::filesystem::path parent = std::filesystem::path(current_source_path).parent_path();
                relative = relative.lexically_relative(parent);
                std::string import_path = relative.generic_string();
                if (!import_path.starts_with('.')) import_path = "./" + import_path;
                const std::string import_text = std::format(
                    "import {{ {} }} from \"{}\";\n", symbol_name, import_path);
                actions.push_back({
                    {"title", "Import " + symbol_name + " from " + import_path},
                    {"kind", "quickfix"},
                    {"diagnostics", json::array({diagnostic})},
                    {"edit", {{"changes", {{uri, json::array({{
                        {"range", {
                            {"start", {{"line", 0}, {"character", 0}}},
                            {"end", {{"line", 0}, {"character", 0}}}
                        }},
                        {"newText", import_text}
                    }})}}}}}
                });
            }
            continue;
        }

        if (code == "E3074" && current_file && std::regex_search(message, match, interface_pattern)) {
            const std::string owner_name = match[1].str();
            const std::string member_kind = match[2].str();
            const std::string interface_name = match[3].str();
            const std::string member_name = match[4].str();

            std::shared_ptr<InterfaceDecl> interface_decl;
            const InterfaceMember* required_member = nullptr;
            for (const SourceFile& source : m_workspace.source_manager().all_files()) {
                DiagnosticEngine recovery_diagnostics;
                Lexer lexer(source.id, source.content, recovery_diagnostics);
                Parser parser(source.id, source.path, lexer.tokenize_all(), recovery_diagnostics);
                const auto module = parser.parse_module();
                if (!module) continue;
                for (const auto& declaration : module->declarations) {
                    const auto candidate = std::dynamic_pointer_cast<InterfaceDecl>(declaration);
                    if (!candidate || candidate->name != interface_name) continue;
                    interface_decl = candidate;
                    for (const InterfaceMember& member : candidate->members) {
                        if (member.name == member_name && member.is_method == (member_kind == "method")) {
                            required_member = &member;
                            break;
                        }
                    }
                    break;
                }
                if (required_member) break;
            }
            if (!required_member) continue;

            const std::regex owner_pattern("(?:class|struct)\\s+" + owner_name + "\\b");
            std::smatch owner_match;
            if (!std::regex_search(current_file->content, owner_match, owner_pattern)) continue;
            const std::size_t owner_offset = static_cast<std::size_t>(owner_match.position());
            const std::size_t open_brace = current_file->content.find('{', owner_offset);
            if (open_brace == std::string::npos) continue;
            std::size_t close_brace = std::string::npos;
            int depth = 0;
            bool in_string = false;
            bool escaped = false;
            for (std::size_t index = open_brace; index < current_file->content.size(); ++index) {
                const char character = current_file->content[index];
                if (character == '"' && !escaped) in_string = !in_string;
                if (!in_string) {
                    if (character == '{') ++depth;
                    else if (character == '}' && --depth == 0) {
                        close_brace = index;
                        break;
                    }
                }
                escaped = character == '\\' && !escaped;
                if (character != '\\') escaped = false;
            }
            if (close_brace == std::string::npos) continue;

            std::string stub = "\n    " + member_name;
            const std::string return_type = required_member->return_type.to_string();
            if (!required_member->is_method) {
                stub += ": " + return_type + ";\n";
            } else {
                stub += '(';
                for (std::size_t index = 0; index < required_member->params.size(); ++index) {
                    if (index != 0) stub += ", ";
                    stub += required_member->params[index]->name + ": " +
                        required_member->params[index]->type_annotation.to_string();
                }
                stub += "): " + return_type + " {";
                if (return_type == "Void") {
                    stub += "\n    }\n";
                } else {
                    std::string default_value = return_type.ends_with('?') ? "null" : return_type + "()";
                    if (return_type == "Bool") default_value = "false";
                    else if (return_type == "String") default_value = "\"\"";
                    else if (numeric_type_for_name(return_type)) default_value = "0";
                    stub += "\n        return " + default_value + ";\n    }\n";
                }
            }
            const SourceLocation insertion = m_workspace.source_manager().get_location(
                current_file->id, static_cast<uint32_t>(close_brace));
            actions.push_back({
                {"title", "Implement " + interface_name + "." + member_name},
                {"kind", "quickfix"},
                {"diagnostics", json::array({diagnostic})},
                {"edit", {{"changes", {{uri, json::array({{
                    {"range", {
                        {"start", {{"line", insertion.line - 1}, {"character", insertion.column - 1}}},
                        {"end", {{"line", insertion.line - 1}, {"character", insertion.column - 1}}}
                    }},
                    {"newText", stub}
                }})}}}}}
            });
        }
    }
    return json{{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", std::move(actions)}}.dump();
}

std::string LanguageServer::on_workspace_symbol(const std::string& id, const std::string& query) {
    json symbols = json::array();
    for (const WorkspaceExportedSymbol& symbol : m_workspace.workspace_symbols()) {
        if (!query.empty() && symbol.name.find(query) == std::string::npos) continue;
        int symbol_kind = 5;
        switch (symbol.kind) {
            case WorkspaceExportedSymbolKind::Class: symbol_kind = 5; break;
            case WorkspaceExportedSymbolKind::Struct: symbol_kind = 23; break;
            case WorkspaceExportedSymbolKind::Interface: symbol_kind = 11; break;
            case WorkspaceExportedSymbolKind::Enum: symbol_kind = 10; break;
            case WorkspaceExportedSymbolKind::Function: symbol_kind = 12; break;
            case WorkspaceExportedSymbolKind::Constant: symbol_kind = 14; break;
        }
        const SourceFile* file = m_workspace.source_manager().get_file(symbol.file_id);
        if (!file) continue;
        const SourceSpan range = exact_identifier_span(
            m_workspace.source_manager(), symbol.declaration_span, symbol.name).value_or(symbol.declaration_span);
        symbols.push_back({
            {"name", symbol.name}, {"kind", symbol_kind},
            {"location", {{"uri", uri_for_source_path(file->path)}, {"range", lsp_range(range)}}}
        });
    }
    return json{{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", std::move(symbols)}}.dump();
}

std::string LanguageServer::on_semantic_tokens(const std::string& id, const std::string& uri) {
    json data = json::array();
    const SourceFile* file = m_workspace.source_manager().get_file(source_path_for_uri(uri));
    if (file) {
        DiagnosticEngine lexical_diagnostics;
        Lexer lexer(file->id, file->content, lexical_diagnostics);
        const std::vector<Token> tokens = lexer.tokenize_all();
        uint32_t previous_line = 0;
        uint32_t previous_start = 0;
        TokenType previous_type = TokenType::Invalid;
        auto append_semantic_token = [&](uint32_t token_line, uint32_t start,
                                               uint32_t length, uint32_t type,
                                               uint32_t modifiers) mutable {
            if (length == 0) return;
            const uint32_t delta_line = token_line - previous_line;
            const uint32_t delta_start = delta_line == 0 ? start - previous_start : start;
            data.push_back(delta_line);
            data.push_back(delta_start);
            data.push_back(length);
            data.push_back(type);
            data.push_back(modifiers);
            previous_line = token_line;
            previous_start = start;
        };
        for (const Token& token : tokens) {
            uint32_t type = 0;
            uint32_t modifiers = 0;
            bool emit = true;
            if (token.is_keyword()) type = 13;
            else if (token.type == TokenType::StringLiteral || token.type == TokenType::TemplateStart ||
                     token.type == TokenType::TemplateText || token.type == TokenType::TemplateEnd) type = 14;
            else if (token.type == TokenType::IntegerLiteral || token.type == TokenType::FloatLiteral ||
                     token.type == TokenType::DurationLiteral) type = 15;
            else if (token.type == TokenType::Identifier) {
                if (previous_type == TokenType::At) {
                    type = 17;
                } else if (const auto occurrence = m_workspace.symbol_occurrence_at(file->id, token.span.start_offset)) {
                    type = semantic_token_type(occurrence->kind);
                    if (occurrence->is_declaration) modifiers |= 1U;
                } else if (previous_type == TokenType::Dot &&
                           is_member_call(file->content, token.span.end_offset)) {
                    type = 12; // method
                } else {
                    type = std::isupper(static_cast<unsigned char>(token.text.front())) ? 1U : 8U;
                }
            } else {
                emit = false;
            }
            if (emit && token.span.is_valid()) {
                if (token.span.start_line == token.span.end_line) {
                    append_semantic_token(token.span.start_line - 1, token.span.start_column - 1,
                        token.span.end_column - token.span.start_column, type, modifiers);
                } else {
                    for (uint32_t source_line = token.span.start_line; source_line <= token.span.end_line; ++source_line) {
                        const std::string source_text = m_workspace.source_manager().get_line_content(file->id, source_line);
                        const uint32_t start = source_line == token.span.start_line
                            ? token.span.start_column - 1 : 0;
                        const uint32_t end = source_line == token.span.end_line
                            ? token.span.end_column - 1 : static_cast<uint32_t>(source_text.size());
                        if (end > start) append_semantic_token(source_line - 1, start, end - start, type, modifiers);
                    }
                }
            }
            previous_type = token.type;
        }
    }
    return json{{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", {{"data", std::move(data)}}}}.dump();
}

std::string LanguageServer::on_inlay_hints(const std::string& id, const std::string& uri) {
    json hints = json::array();
    const SourceFile* file = m_workspace.source_manager().get_file(source_path_for_uri(uri));
    if (file) {
        std::unordered_set<uint32_t> hinted_name_offsets;
        for (const WorkspaceDocumentSymbol& symbol : m_workspace.document_symbols(file->id)) {
            if (!symbol.is_inferred || (symbol.kind != WorkspaceDocumentSymbolKind::Variable &&
                                        symbol.kind != WorkspaceDocumentSymbolKind::Field)) {
                continue;
            }
            const auto name_span = exact_identifier_span(
                m_workspace.source_manager(), symbol.declaration_span, symbol.name);
            if (!name_span) continue;
            hinted_name_offsets.insert(name_span->start_offset);
            hints.push_back({
                {"position", {{"line", name_span->end_line - 1}, {"character", name_span->end_column - 1}}},
                {"label", ": " + symbol.type_name}, {"kind", 1},
                {"paddingLeft", false}, {"paddingRight", true}
            });
        }

        // Semantic analysis can still infer a local when another module in the
        // workspace is incomplete and therefore prevented the document-symbol
        // index from being finalized. Recover those hints from lexer spans and
        // the TypeChecker's inferred-type table, without guessing from literal
        // spelling.
        DiagnosticEngine lexical_diagnostics;
        Lexer lexer(file->id, file->content, lexical_diagnostics);
        const std::vector<Token> tokens = lexer.tokenize_all();
        for (std::size_t index = 0; index + 3 < tokens.size(); ++index) {
            if (tokens[index].type != TokenType::Let && tokens[index].type != TokenType::Const) continue;
            const Token& name = tokens[index + 1];
            if (name.type != TokenType::Identifier || tokens[index + 2].type != TokenType::Equals ||
                hinted_name_offsets.contains(name.span.start_offset)) {
                continue;
            }
            const auto inferred = m_workspace.inferred_type_at(file->id, tokens[index + 3].span.start_offset);
            if (!inferred || *inferred == "<type-error>" || *inferred == "Void") continue;
            hints.push_back({
                {"position", {{"line", name.span.end_line - 1}, {"character", name.span.end_column - 1}}},
                {"label", ": " + *inferred}, {"kind", 1},
                {"paddingLeft", false}, {"paddingRight", true}
            });
        }
    }
    return json{{"jsonrpc", "2.0"}, {"id", parse_id(id)}, {"result", std::move(hints)}}.dump();
}

} // namespace EngineScript
