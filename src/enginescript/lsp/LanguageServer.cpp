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
        m_workspace.source_manager().find_file_id(uri), line + 1
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
