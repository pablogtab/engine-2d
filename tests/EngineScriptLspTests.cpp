#include <cassert>
#include <iostream>
#include "enginescript/lsp/LanguageServer.hpp"
#include <nlohmann/json.hpp>

using namespace EngineScript;
using json = nlohmann::json;

int main() {
    std::cout << "[TEST] Running EngineScriptLspTests...\n";

    LanguageServer server("/workspace/My Game");

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
                "uri":"file:///workspace/My%20Game/Source/Player.e2s",
                "version":1,
                "text":"@behaviour\nexport class Player extends Behaviour {\n  @expose\n  speed: Float = 100.0;\n}"
            }
        }
    })";
    std::string open_res = server.handle_message(open_req);
    json j_open = json::parse(open_res);
    assert(j_open["method"] == "textDocument/publishDiagnostics");
    assert(j_open["params"]["diagnostics"].empty()); // No errors!
    assert(server.workspace().source_manager().find_file_id("Source/Player.e2s") != INVALID_FILE_ID);

    // Test 3: DidChange with error
    std::string change_req = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":2},
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
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":0,"character":1}
        }
    })";
    std::string comp_res = server.handle_message(comp_req);
    json j_comp = json::parse(comp_res);
    assert(j_comp["result"].is_array());
    assert(j_comp["result"].size() > 5);

    // Diagnostics belonging to another source file must not appear on Player.e2s.
    std::string other_open = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didOpen",
        "params":{"textDocument":{
            "uri":"file:///workspace/My%20Game/Source/Enemy.e2s",
            "version":1,
            "text":"export class Enemy { let hp: Float = \"bad\"; }"
        }}
    })";
    json other_diagnostics = json::parse(server.handle_message(other_open));
    assert(!other_diagnostics["params"]["diagnostics"].empty());
    auto other_updates = server.take_pending_notifications();
    assert(other_updates.size() == 1);
    json player_update = json::parse(other_updates.front());
    assert(player_update["params"]["uri"] == "file:///workspace/My%20Game/Source/Player.e2s");

    std::string player_fixed = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":3},
            "contentChanges":[{"text":"@behaviour\nexport class Player extends Behaviour {}"}]
        }
    })";
    json player_diagnostics = json::parse(server.handle_message(player_fixed));
    assert(player_diagnostics["params"]["uri"] == "file:///workspace/My%20Game/Source/Player.e2s");
    assert(player_diagnostics["params"]["diagnostics"].empty());
    auto player_updates = server.take_pending_notifications();
    assert(player_updates.size() == 1);
    json enemy_update = json::parse(player_updates.front());
    assert(enemy_update["params"]["uri"] == "file:///workspace/My%20Game/Source/Enemy.e2s");
    assert(!enemy_update["params"]["diagnostics"].empty());

    std::cout << "[PASS] EngineScriptLspTests passed!\n";
    return 0;
}
