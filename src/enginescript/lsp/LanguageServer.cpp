#include "LanguageServer.hpp"
#include <nlohmann/json.hpp>
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
}

LanguageServer::LanguageServer(std::string project_root)
    : m_workspace(std::move(project_root)) {
}

std::string LanguageServer::handle_message(const std::string& json_rpc_msg) {
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
    return "{}";
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
    m_open_document_versions[uri] = version;
    m_workspace.add_source_file(uri, text);
    return publish_diagnostics(uri);
}

std::string LanguageServer::on_did_change(const std::string& uri, const std::string& text, uint32_t version) {
    m_open_document_versions[uri] = version;
    m_workspace.update_source_file(uri, text);
    return publish_diagnostics(uri);
}

std::string LanguageServer::publish_diagnostics(const std::string& uri) {
    auto res = m_workspace.compile();

    json notif;
    notif["jsonrpc"] = "2.0";
    notif["method"] = "textDocument/publishDiagnostics";

    json diags_arr = json::array();
    for (const auto& d : res.diagnostics) {
        json d_obj;
        d_obj["code"] = d.code;
        d_obj["message"] = d.message;
        d_obj["severity"] = (d.severity == DiagnosticSeverity::Error) ? 1 : 2;

        json range;
        range["start"] = {{"line", d.span.start_line > 0 ? d.span.start_line - 1 : 0}, {"character", d.span.start_column > 0 ? d.span.start_column - 1 : 0}};
        range["end"] = {{"line", d.span.end_line > 0 ? d.span.end_line - 1 : 0}, {"character", d.span.end_column > 0 ? d.span.end_column - 1 : 0}};
        d_obj["range"] = range;

        diags_arr.push_back(d_obj);
    }

    notif["params"] = {
        {"uri", uri},
        {"version", m_open_document_versions[uri]},
        {"diagnostics", diags_arr}
    };
    return notif.dump();
}

std::string LanguageServer::on_document_symbol(const std::string& id, const std::string& uri) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = parse_id(id);

    json syms = json::array();
    for (const auto& b : m_workspace.metadata_manifest().behaviours) {
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

    // Keywords
    std::vector<std::string> kw = {
        "class", "struct", "interface", "export", "import", "from", "extends",
        "let", "const", "if", "else", "while", "for", "return", "async", "await",
        "override", "constructor", "private", "protected", "public"
    };
    for (const auto& k : kw) {
        items.push_back({{"label", k}, {"kind", 14}}); // Keyword
    }

    // Decorators
    std::vector<std::string> decs = {"behaviour", "expose", "inject", "component", "optional", "range", "serializable"};
    for (const auto& d : decs) {
        items.push_back({{"label", "@" + d}, {"kind", 15}}); // Snippet / Decorator
    }

    // Builtin Types
    std::vector<std::string> types = {"Float", "Int", "Bool", "String", "Vec2", "Vec3", "Duration", "Task", "Result", "Array", "Map", "Set", "Entity"};
    for (const auto& t : types) {
        items.push_back({{"label", t}, {"kind", 7}}); // Class / Type
    }

    resp["result"] = items;
    return resp.dump();
}

std::string LanguageServer::on_hover(const std::string& id, const std::string& uri, uint32_t line, uint32_t character) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = parse_id(id);

    json hover_obj;
    hover_obj["contents"] = {
        {"kind", "markdown"},
        {"value", "**EngineScript Type**\nStatically typed gameplay language."}
    };
    resp["result"] = hover_obj;
    return resp.dump();
}

std::string LanguageServer::on_definition(const std::string& id, const std::string& uri, uint32_t line, uint32_t character) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = parse_id(id);

    json loc;
    loc["uri"] = uri;
    loc["range"] = {
        {"start", {{"line", 0}, {"character", 0}}},
        {"end", {{"line", 0}, {"character", 1}}}
    };
    resp["result"] = loc;
    return resp.dump();
}

} // namespace EngineScript
