#include <cassert>
#include <array>
#include <iostream>
#include <filesystem>
#include <fstream>
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
    assert(j_init["result"]["capabilities"]["referencesProvider"] == true);
    assert(j_init["result"]["capabilities"]["renameProvider"].is_object());
    assert(j_init["result"]["capabilities"]["documentFormattingProvider"] == true);
    assert(j_init["result"]["capabilities"]["codeActionProvider"].is_object());
    assert(j_init["result"]["capabilities"]["workspaceSymbolProvider"] == true);
    assert(j_init["result"]["capabilities"]["semanticTokensProvider"].is_object());
    assert(j_init["result"]["capabilities"]["inlayHintProvider"] == true);

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
    bool has_int_conversion = false;
    bool has_uint8_conversion = false;
    bool has_float64_conversion = false;
    for (const auto& item : j_comp["result"]) {
        if (item["label"] == "behaviour class") {
            has_behaviour_template = item["insertTextFormat"] == 2;
        }
        has_to_string |= item["label"] == "toString";
        has_int_conversion |= item["label"] == "Int" &&
            item.value("detail", "") == "Int(value: Numeric) -> Int";
        has_uint8_conversion |= item["label"] == "UInt8" &&
            item.value("detail", "") == "UInt8(value: Numeric) -> UInt8";
        has_float64_conversion |= item["label"] == "Float64" &&
            item.value("detail", "") == "Float64(value: Numeric) -> Float64";
    }
    assert(has_behaviour_template);
    assert(!has_to_string);
    assert(has_int_conversion && has_uint8_conversion && has_float64_conversion);

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

    // Completion in an import path only offers project modules matching the
    // typed relative path; global EngineScript types must not leak here.
    std::string import_path_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":10},
            "contentChanges":[{"text":"import {} from \"./Sh\";"}]
        }
    })";
    json empty_import_diagnostics = json::parse(server.handle_message(import_path_change));
    bool diagnosed_empty_import = false;
    for (const auto& diagnostic : empty_import_diagnostics["params"]["diagnostics"]) {
        diagnosed_empty_import |= diagnostic.value("code", "") == "E2013";
    }
    assert(diagnosed_empty_import);
    json import_path_completion = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":12, "method":"textDocument/completion",
        "params":{"textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},"position":{"line":0,"character":20}}
    })"));
    bool has_shared_module = false;
    bool leaked_global_type_in_path = false;
    for (const auto& item : import_path_completion["result"]) {
        has_shared_module |= item["label"] == "./Shared";
        leaked_global_type_in_path |= item["label"] == "PlayerPrefs";
    }
    assert(has_shared_module && !leaked_global_type_in_path);

    std::string missing_import_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":11},
            "contentChanges":[{"text":"import {} from \"./PlatformerPlaye\";"}]
        }
    })";
    json missing_import_diagnostics = json::parse(server.handle_message(missing_import_change));
    assert(missing_import_diagnostics["params"]["diagnostics"].size() == 2);
    assert(missing_import_diagnostics["params"]["diagnostics"][0]["code"] == "E2012");
    assert(missing_import_diagnostics["params"]["diagnostics"][1]["code"] == "E2013");

    // Completion between braces is constrained to exports of the selected
    // module and never proposes catalog types from unrelated namespaces.
    std::string import_names_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":12},
            "contentChanges":[{"text":"import { } from \"./Shared\";"}]
        }
    })";
    static_cast<void>(server.handle_message(import_names_change));
    json import_names_completion = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":13, "method":"textDocument/completion",
        "params":{"textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},"position":{"line":0,"character":9}}
    })"));
    bool has_shared_export = false;
    bool leaked_global_type_in_names = false;
    for (const auto& item : import_names_completion["result"]) {
        has_shared_export |= item["label"] == "SharedConfig";
        leaked_global_type_in_names |= item["label"] == "PlayerPrefs";
    }
    assert(has_shared_export && !leaked_global_type_in_names);

    // Entities completion exposes the semantic Gameplay Tags contract.
    const std::string entities_change = R"({
        "jsonrpc":"2.0",
        "method":"textDocument/didChange",
        "params":{
            "textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s","version":13},
            "contentChanges":[{"text":"@behaviour\nexport class Player extends Behaviour {\n  constructor(@inject private entities: Entities) {}\n  override update(deltaTime: Float): Void {\n    entities.\n  }\n}"}]
        }
    })";
    static_cast<void>(server.handle_message(entities_change));
    const json entities_completion = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":14, "method":"textDocument/completion",
        "params":{"textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},"position":{"line":4,"character":13}}
    })"));
    bool has_find_gameplay_tag = false;
    bool has_has_gameplay_tag = false;
    bool has_gameplay_tags = false;
    for (const auto& item : entities_completion["result"]) {
        has_find_gameplay_tag |= item["label"] == "findByGameplayTag";
        has_has_gameplay_tag |= item["label"] == "hasGameplayTag";
        has_gameplay_tags |= item["label"] == "gameplayTags";
    }
    assert(has_find_gameplay_tag && has_has_gameplay_tag && has_gameplay_tags);


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

    // Closed project files are indexed at server startup; generated and Library
    // copies never enter the semantic workspace.
    namespace fs = std::filesystem;
    const fs::path discovery_root = fs::temp_directory_path() / "engine2d_lsp_discovery_tests";
    fs::remove_all(discovery_root);
    fs::create_directories(discovery_root / "Source");
    fs::create_directories(discovery_root / "Library");
    {
        std::ofstream(discovery_root / "Source" / "Health.e2s")
            << "@behaviour\nexport class Health extends Behaviour { public current: Int = 10; }\n";
        std::ofstream(discovery_root / "Library" / "Ignored.e2s")
            << "export class Ignored {}\n";
    }
    LanguageServer discovered_server(discovery_root.string());
    assert(discovered_server.workspace().source_manager().find_file_id("Source/Health.e2s") != INVALID_FILE_ID);
    assert(discovered_server.workspace().source_manager().find_file_id("Library/Ignored.e2s") == INVALID_FILE_ID);

    const std::string health_uri = "file://" + (discovery_root / "Source" / "Health.e2s").generic_string();
    json closed_definition = json::parse(discovered_server.handle_message(R"({
        "jsonrpc":"2.0", "id":20, "method":"textDocument/definition",
        "params":{"textDocument":{"uri":")" + health_uri + R"("},"position":{"line":1,"character":14}}
    })"));
    assert(closed_definition["result"]["uri"] == health_uri);

    json signature_help = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":21, "method":"textDocument/signatureHelp",
        "params":{"textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},"position":{"line":4,"character":12}}
    })"));
    // The request is valid even while its document contains an incomplete edit.
    assert(signature_help.contains("result"));

    const std::array<std::string, 7> conversion_lines{{
        "function conversions(score: Int, speed: Float64): Void {",
        "  let a: Int = Int(score);",
        "  let b: UInt8 = UInt8(score);",
        "  let c: Float64 = Float64(speed);",
        "  let d: Float = Float(score);",
        "  let inferred = 1;",
        "}"
    }};
    std::string conversion_document;
    for (const std::string& conversion_line : conversion_lines) conversion_document += conversion_line + "\n";
    json conversion_change = {
        {"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}, {"version", 30}}},
            {"contentChanges", {{{"text", conversion_document}}}}
        }}
    };
    const json conversion_diagnostics = json::parse(server.handle_message(conversion_change.dump()));
    assert(conversion_diagnostics["params"]["diagnostics"].empty());
    const auto require_conversion_signature = [&](uint32_t request_id, uint32_t request_line,
                                                  const std::string& expected_name) {
        json request = {
            {"jsonrpc", "2.0"}, {"id", request_id}, {"method", "textDocument/signatureHelp"},
            {"params", {
                {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}}},
                {"position", {{"line", request_line},
                    {"character", conversion_lines[request_line].rfind('(') + 1}}}
            }}
        };
        const json result = json::parse(server.handle_message(request.dump()));
        assert(result["result"]["signatures"][0]["label"] ==
            expected_name + "(value: Numeric) -> " + expected_name);
        assert(result["result"]["signatures"][0]["documentation"].get<std::string>().find("saturate") !=
            std::string::npos || expected_name == "Float64");
    };
    require_conversion_signature(22, 1, "Int");
    require_conversion_signature(23, 2, "UInt8");
    require_conversion_signature(24, 3, "Float64");

    json numeric_hover_request = {
        {"jsonrpc", "2.0"}, {"id", 25}, {"method", "textDocument/hover"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}}},
            {"position", {{"line", 0}, {"character", 43}}}
        }}
    };
    const json numeric_hover = json::parse(server.handle_message(numeric_hover_request.dump()));
    const std::string numeric_hover_text = numeric_hover["result"]["contents"]["value"];
    assert(numeric_hover_text.find("Float64(value: Numeric) -> Float64") != std::string::npos);
    assert(numeric_hover_text.find("IEEE-754") != std::string::npos);

    const std::size_t score_character = conversion_lines[0].find("score") + 1;
    const json references = json::parse(server.handle_message(json{
        {"jsonrpc", "2.0"}, {"id", 26}, {"method", "textDocument/references"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}}},
            {"position", {{"line", 0}, {"character", score_character}}},
            {"context", {{"includeDeclaration", true}}}
        }}
    }.dump()));
    assert(references["result"].is_array() && references["result"].size() >= 3);

    const json rename = json::parse(server.handle_message(json{
        {"jsonrpc", "2.0"}, {"id", 27}, {"method", "textDocument/rename"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}}},
            {"position", {{"line", 0}, {"character", score_character}}},
            {"newName", "points"}
        }}
    }.dump()));
    assert(rename["result"]["changes"]["file:///workspace/My%20Game/Source/Player.e2s"].size() >= 3);

    const json formatting = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":28, "method":"textDocument/formatting",
        "params":{"textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},"options":{"tabSize":4,"insertSpaces":true}}
    })"));
    assert(formatting["result"].size() == 1);
    assert(formatting["result"][0]["newText"].get<std::string>().find("    let a") != std::string::npos);

    const json workspace_symbols = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":29, "method":"workspace/symbol", "params":{"query":"Shared"}
    })"));
    assert(!workspace_symbols["result"].empty());
    assert(workspace_symbols["result"][0]["name"] == "SharedConfig");

    const json semantic_tokens = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":30, "method":"textDocument/semanticTokens/full",
        "params":{"textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"}}
    })"));
    assert(!semantic_tokens["result"]["data"].empty());

    const json inlay_hints = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":31, "method":"textDocument/inlayHint",
        "params":{"textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},"range":{"start":{"line":0,"character":0},"end":{"line":7,"character":0}}}
    })"));
    assert(!inlay_hints["result"].empty());
    assert(inlay_hints["result"][0]["label"] == ": Int");

    const json numeric_diagnostic = {
        {"code", "E3010"},
        {"message", "Cannot initialize variable 'n' of type 'Int' with value of type 'Float'"},
        {"range", {{"start", {{"line", 1}, {"character", 15}}}, {"end", {{"line", 1}, {"character", 20}}}}}
    };
    const json string_diagnostic = {
        {"code", "E3010"},
        {"message", "Cannot initialize variable 's' of type 'String' with value of type 'Int'"},
        {"range", {{"start", {{"line", 1}, {"character", 15}}}, {"end", {{"line", 1}, {"character", 20}}}}}
    };
    const json code_actions = json::parse(server.handle_message(json{
        {"jsonrpc", "2.0"}, {"id", 32}, {"method", "textDocument/codeAction"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}}},
            {"range", numeric_diagnostic["range"]},
            {"context", {{"diagnostics", json::array({numeric_diagnostic, string_diagnostic})}}}
        }}
    }.dump()));
    assert(code_actions["result"].size() == 1);
    assert(code_actions["result"][0]["title"] == "Convert explicitly to Int");

    const json removed_change = {
        {"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}, {"version", 31}}},
            {"contentChanges", json::array({{{"text", "function removed(): Void { toString(1); }"}}})}
        }}
    };
    static_cast<void>(server.handle_message(removed_change.dump()));
    const json removed_hover = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":33, "method":"textDocument/hover",
        "params":{"textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},"position":{"line":0,"character":31}}
    })"));
    assert(removed_hover["result"].is_null());
    const json removed_signature = json::parse(server.handle_message(R"({
        "jsonrpc":"2.0", "id":34, "method":"textDocument/signatureHelp",
        "params":{"textDocument":{"uri":"file:///workspace/My%20Game/Source/Player.e2s"},"position":{"line":0,"character":37}}
    })"));
    assert(removed_signature["result"].is_null());

    // References, definition, and rename use resolver symbol identity. A
    // homonymous local in another function must never be included.
    const std::array<std::string, 8> scoped_reference_lines{{
        "function first(): Int {",
        "    let score: Int = 1;",
        "    return score;",
        "}",
        "function second(): Int {",
        "    let score: Int = 2;",
        "    return score;",
        "}"
    }};
    std::string scoped_reference_document;
    for (const std::string& source_line : scoped_reference_lines) scoped_reference_document += source_line + "\n";
    static_cast<void>(server.handle_message(json{
        {"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}, {"version", 32}}},
            {"contentChanges", json::array({{{"text", scoped_reference_document}}})}
        }}
    }.dump()));
    const uint32_t first_score = static_cast<uint32_t>(scoped_reference_lines[2].find("score") + 1);
    const json scoped_references = json::parse(server.handle_message(json{
        {"jsonrpc", "2.0"}, {"id", 35}, {"method", "textDocument/references"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}}},
            {"position", {{"line", 2}, {"character", first_score}}},
            {"context", {{"includeDeclaration", true}}}
        }}
    }.dump()));
    assert(scoped_references["result"].size() == 2);
    for (const auto& location : scoped_references["result"]) {
        assert(location["range"]["start"]["line"].get<uint32_t>() < 4);
    }
    const json scoped_definition = json::parse(server.handle_message(json{
        {"jsonrpc", "2.0"}, {"id", 36}, {"method", "textDocument/definition"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}}},
            {"position", {{"line", 2}, {"character", first_score}}}
        }}
    }.dump()));
    assert(scoped_definition["result"]["range"]["start"]["line"] == 1);
    const json scoped_rename = json::parse(server.handle_message(json{
        {"jsonrpc", "2.0"}, {"id", 37}, {"method", "textDocument/rename"},
        {"params", {
            {"textDocument", {{"uri", "file:///workspace/My%20Game/Source/Player.e2s"}}},
            {"position", {{"line", 2}, {"character", first_score}}},
            {"newName", "points"}
        }}
    }.dump()));
    assert(scoped_rename["result"]["changes"]["file:///workspace/My%20Game/Source/Player.e2s"].size() == 2);
    fs::remove_all(discovery_root);

    std::cout << "[PASS] EngineScriptLspTests passed!\n";
    return 0;
}
