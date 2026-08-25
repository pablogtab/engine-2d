#include <cassert>
#include <iostream>
#include "enginescript/api/ApiCatalog.hpp"
#include "enginescript/lsp/LanguageServer.hpp"
#include "enginescript/module/GameModule.hpp"
#include <nlohmann/json.hpp>

using namespace EngineScript;
using json = nlohmann::json;

int main() {
    std::cout << "[TEST] Running EngineScriptLspTests...\n";

    const ApiCatalog& api = ApiCatalog::instance();
    assert(api.abi_version() == ENGINE_SCRIPT_ABI_VERSION);
    assert(api.find_available_method("Input", "down") != nullptr);
    assert(api.find_available_method("Input", "released") != nullptr);
    assert(api.find_available_method("Animator2D", "stop") == nullptr);
    assert(api.find_available_method("PlayerPrefs", "setVec3") != nullptr);
    const ApiPropertyDescriptor* body_velocity = api.find_available_property("RigidBody2D", "velocity");
    assert(body_velocity && body_velocity->writable && body_velocity->type == "Vec2");
    const ApiPropertyDescriptor* grounded = api.find_available_property("RigidBody2D", "isGrounded");
    assert(grounded && !grounded->writable);

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
    bool has_behaviour_template = false;
    bool has_to_string = false;
    for (const auto& item : j_comp["result"]) {
        if (item["label"] == "behaviour class") {
            has_behaviour_template = item["insertTextFormat"] == 2;
        }
        has_to_string |= item["label"] == "toString" &&
            item.value("detail", "").find("value: Int") != std::string::npos;
    }
    assert(has_behaviour_template);
    assert(has_to_string);

    // Definitions resolve to the declaration span instead of a placeholder range.
    std::string definition_req = R"({
        "jsonrpc":"2.0",
        "id":3,
        "method":"textDocument/definition",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":0,"character":14}
        }
    })";
    json definition = json::parse(server.handle_message(definition_req));
    assert(definition["result"]["uri"] == "file:///workspace/My%20Game/Source/Player.e2s");
    assert(definition["result"]["range"]["start"]["line"] == 0);
    assert(definition["result"]["range"]["start"]["character"] == 13);

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
            "contentChanges":[{"text":"@behaviour\nexport class Player extends Behaviour {\n  audio: Audio;\n  sprite: SpriteRenderer2D;\n  update(deltaTime: Float): Void {\n    let waiting = delay(1.ms);\n    audio.playSound(\"jump\");\n    sprite.flipX = true;\n  }\n}"}]
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

    // Completion after a typed receiver is restricted to the inferred API type.
    std::string member_completion_req = R"({
        "jsonrpc":"2.0",
        "id":4,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":6,"character":10}
        }
    })";
    json member_completion = json::parse(server.handle_message(member_completion_req));
    bool has_play_sound = false;
    bool has_scene_load = false;
    for (const auto& item : member_completion["result"]) {
        has_play_sound |= item["label"] == "playSound";
        has_scene_load |= item["label"] == "load";
    }
    assert(has_play_sound);
    assert(!has_scene_load);

    // Component properties are suggested from the catalog after an injected field.
    std::string sprite_completion_req = R"({
        "jsonrpc":"2.0",
        "id":5,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":7,"character":11}
        }
    })";
    json sprite_completion = json::parse(server.handle_message(sprite_completion_req));
    bool has_flip_x = false;
    for (const auto& item : sprite_completion["result"]) {
        has_flip_x |= item["label"] == "flipX";
    }
    assert(has_flip_x);

    // Hover exposes the TypeChecker result for an inferred local variable.
    std::string inferred_hover_req = R"({
        "jsonrpc":"2.0",
        "id":5,
        "method":"textDocument/hover",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":5,"character":10}
        }
    })";
    json inferred_hover = json::parse(server.handle_message(inferred_hover_req));
    assert(inferred_hover["result"]["contents"]["value"].get<std::string>().find("Task<Void>") != std::string::npos);

    std::string component_receiver_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":4},
            "contentChanges":[{"text":"@behaviour\nexport class Player extends Behaviour {\n  constructor(\n    @inject private input: Input,\n    @component private body: RigidBody2D\n  ) {}\n  override update(deltaTime: Float): Void {\n    body.\n  }\n}"}]
        }
    })";
    static_cast<void>(server.handle_message(component_receiver_change));
    std::string component_completion_req = R"({
        "jsonrpc":"2.0",
        "id":6,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":7,"character":9}
        }
    })";
    json component_completion = json::parse(server.handle_message(component_completion_req));
    bool has_velocity = false;
    bool has_grounded = false;
    bool has_ignore_one_way = false;
    for (const auto& item : component_completion["result"]) {
        has_velocity |= item["label"] == "velocity";
        has_grounded |= item["label"] == "isGrounded";
        has_ignore_one_way |= item["label"] == "setIgnoreOneWay";
    }
    assert(has_velocity && has_grounded && has_ignore_one_way);

    std::string nested_receiver_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":5},
            "contentChanges":[{"text":"@behaviour\nexport class Player extends Behaviour {\n  constructor(\n    @component private body: RigidBody2D\n  ) {}\n  override update(deltaTime: Float): Void {\n    body.velocity.\n  }\n}"}]
        }
    })";
    static_cast<void>(server.handle_message(nested_receiver_change));
    std::string nested_completion_req = R"({
        "jsonrpc":"2.0",
        "id":7,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":6,"character":18}
        }
    })";
    json nested_completion = json::parse(server.handle_message(nested_completion_req));
    bool has_x = false;
    bool has_y = false;
    bool leaked_keyword = false;
    for (const auto& item : nested_completion["result"]) {
        has_x |= item["label"] == "x";
        has_y |= item["label"] == "y";
        leaked_keyword |= item["label"] == "if";
    }
    assert(has_x && has_y && !leaked_keyword);

    std::string scoped_symbols_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":6},
            "contentChanges":[{"text":"@behaviour\nexport class Player extends Behaviour {\n  constructor(\n    @component private body: RigidBody2D,\n    @component private sprite: SpriteRenderer2D\n  ) {}\n  private coyoteTimer: Float = 0.0;\n  override update(deltaTime: Float): Void {\n    let movement: Float = 1.0;\n    spri\n    let futureValue: Float = 2.0;\n  }\n}"}]
        }
    })";
    static_cast<void>(server.handle_message(scoped_symbols_change));
    std::string scoped_symbols_completion_req = R"({
        "jsonrpc":"2.0",
        "id":8,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":9,"character":8}
        }
    })";
    json scoped_symbols_completion = json::parse(server.handle_message(scoped_symbols_completion_req));
    bool has_sprite_symbol = false;
    bool has_body_symbol = false;
    bool has_movement_symbol = false;
    bool has_delta_time_symbol = false;
    bool has_class_field = false;
    bool leaked_future_symbol = false;
    for (const auto& item : scoped_symbols_completion["result"]) {
        has_sprite_symbol |= item["label"] == "sprite" && item.value("sortText", "").starts_with("0_");
        has_body_symbol |= item["label"] == "body";
        has_movement_symbol |= item["label"] == "movement";
        has_delta_time_symbol |= item["label"] == "deltaTime";
        has_class_field |= item["label"] == "coyoteTimer";
        leaked_future_symbol |= item["label"] == "futureValue";
    }
    assert(has_sprite_symbol && has_body_symbol && has_movement_symbol && has_delta_time_symbol && has_class_field);
    assert(!leaked_future_symbol);

    std::string inferred_symbols_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":7},
            "contentChanges":[{"text":"@behaviour\nexport class Player extends Behaviour {\n  constructor(@inject private input: Input) {}\n  override update(deltaTime: Float): Void {\n    let inferredMovement = input.axis(\"Horizontal\");\n    \n  }\n}"}]
        }
    })";
    static_cast<void>(server.handle_message(inferred_symbols_change));
    std::string inferred_symbols_completion_req = R"({
        "jsonrpc":"2.0",
        "id":9,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":5,"character":4}
        }
    })";
    json inferred_symbols_completion = json::parse(server.handle_message(inferred_symbols_completion_req));
    bool has_inferred_local = false;
    for (const auto& item : inferred_symbols_completion["result"]) {
        has_inferred_local |= item["label"] == "inferredMovement" &&
            item.value("detail", "").find("Float") != std::string::npos;
    }
    assert(has_inferred_local);

    std::string method_snippet_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":8},
            "contentChanges":[{"text":"@behaviour\nexport class Player extends Behaviour {\n  constructor(@inject private input: Input) {}\n  override update(deltaTime: Float): Void {\n    input.\n  }\n}"}]
        }
    })";
    static_cast<void>(server.handle_message(method_snippet_change));
    std::string method_snippet_completion_req = R"({
        "jsonrpc":"2.0",
        "id":10,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":4,"character":10}
        }
    })";
    json method_snippet_completion = json::parse(server.handle_message(method_snippet_completion_req));
    bool has_axis_signature = false;
    for (const auto& item : method_snippet_completion["result"]) {
        has_axis_signature |= item["label"] == "axis" &&
            item.value("detail", "").find("name: String") != std::string::npos &&
            item.value("insertText", "").find("${1:name}") != std::string::npos;
    }
    assert(has_axis_signature);

    std::string imported_source_open = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didOpen",
        "params":{
            "textDocument":{
                "uri":"file:///workspace/My%20Game/Source/Shared.e2s",
                "version":1,
                "text":"export class SharedConfig {}"
            }
        }
    })";
    static_cast<void>(server.handle_message(imported_source_open));
    std::string imported_symbol_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":9},
            "contentChanges":[{"text":"import { SharedConfig as Config } from \"./Shared\";\n@behaviour\nexport class Player extends Behaviour {\n  override update(deltaTime: Float): Void {\n    Conf\n  }\n}"}]
        }
    })";
    static_cast<void>(server.handle_message(imported_symbol_change));
    std::string imported_symbol_completion_req = R"({
        "jsonrpc":"2.0",
        "id":11,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":4,"character":8}
        }
    })";
    json imported_symbol_completion = json::parse(server.handle_message(imported_symbol_completion_req));
    bool has_import_alias = false;
    bool leaked_original_import_name = false;
    for (const auto& item : imported_symbol_completion["result"]) {
        has_import_alias |= item["label"] == "Config" && item.value("detail", "").find("SharedConfig as Config") != std::string::npos;
        leaked_original_import_name |= item["label"] == "SharedConfig" && item.value("sortText", "").starts_with("0_");
    }
    assert(has_import_alias && !leaked_original_import_name);


    // A one-character typo before a member access offers a replacement for the typed field.
    std::string typo_change_req = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":4},
            "contentChanges":[{"text":"export class Player { sprite: SpriteRenderer2D; update(): Void { spri. } }"}]
        }
    })";
    static_cast<void>(server.handle_message(typo_change_req));
    std::string typo_completion_req = R"({
        "jsonrpc":"2.0",
        "id":6,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":0,"character":70}
        }
    })";
    json typo_completion = json::parse(server.handle_message(typo_completion_req));
    bool has_sprite_correction = false;
    for (const auto& item : typo_completion["result"]) {
        if (item["label"] == "sprite" && item.contains("textEdit")) {
            has_sprite_correction = item["textEdit"]["newText"] == "sprite";
        }
    }
    assert(has_sprite_correction);

    // Particle component completion is catalog-driven, including control methods and state.
    std::string particle_change_req = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":11},
            "contentChanges":[{"text":"@behaviour\nexport class Player extends Behaviour {\n  constructor(@component private emitter: ParticleEmitter2D) {}\n  override update(deltaTime: Float): Void {\n    emitter.\n  }\n}"}]
        }
    })";
    static_cast<void>(server.handle_message(particle_change_req));
    std::string particle_completion_req = R"({
        "jsonrpc":"2.0",
        "id":12,
        "method":"textDocument/completion",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},
            "position":{"line":4,"character":12}
        }
    })";
    json particle_completion = json::parse(server.handle_message(particle_completion_req));
    bool has_particle_play = false;
    bool has_particle_burst = false;
    bool has_particle_stop = false;
    bool has_particle_state = false;
    bool leaked_audio_method = false;
    for (const auto& item : particle_completion["result"]) {
        has_particle_play |= item["label"] == "play";
        has_particle_burst |= item["label"] == "burst";
        has_particle_stop |= item["label"] == "stop";
        has_particle_state |= item["label"] == "isPlaying";
        leaked_audio_method |= item["label"] == "playSound";
    }
    assert(has_particle_play && has_particle_burst && has_particle_stop && has_particle_state);
    assert(!leaked_audio_method);

    std::cout << "[PASS] EngineScriptLspTests passed!\n";
    return 0;
}
