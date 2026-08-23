#include <cassert>
#include <iostream>
#include "enginescript/lsp/LanguageServer.hpp"
#include <nlohmann/json.hpp>

using namespace EngineScript;
using json = nlohmann::json;

int main() {
    std::cout << "[TEST] Running EngineScriptLspTests...\n";

    LanguageServer server("/workspace/MyGame");

    // Test 1: Initialize
    std::string init_req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    std::string init_res = server.handle_message(init_req);
    json j_init = json::parse(init_res);
    assert(j_init["result"]["capabilities"]["completionProvider"].is_object());

    // Test 2: DidOpen valid file
    std::string open_req = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didOpen",
        "params":{
            "textDocument":{
                "uri":"Source/Player.e2s",
                "version":1,
                "text":"@behaviour\nexport class Player extends Behaviour {\n  @expose\n  speed: Float = 100.0;\n}"
            }
        }
    })";
    std::string open_res = server.handle_message(open_req);
    json j_open = json::parse(open_res);
    assert(j_open["method"] == "textDocument/publishDiagnostics");
    assert(j_open["params"]["diagnostics"].empty()); // No errors!

    // Test 3: DidChange with error
    std::string change_req = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"Source/Player.e2s","version":2},
            "contentChanges":[{"text":"export class Broken { let x: Float = \"bad\"; }"}]
        }
    })";
    std::string change_res = server.handle_message(change_req);
    json j_change = json::parse(change_res);
    assert(!j_change["params"]["diagnostics"].empty()); // Error reported!

    // Test 4: Completion request
    std::string comp_req = R"({
        "jsonrpc":"2.0",
        "id":2,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"Source/Player.e2s"},
            "position":{"line":0,"character":1}
        }
    })";
    std::string comp_res = server.handle_message(comp_req);
    json j_comp = json::parse(comp_res);
    assert(j_comp["result"].is_array());
    assert(j_comp["result"].size() > 5);

    std::cout << "[PASS] EngineScriptLspTests passed!\n";
    return 0;
}
