#include <cassert>
#include <iostream>
#include "enginescript/core/SourceManager.hpp"
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/parser/Parser.hpp"
#include "enginescript/symbols/SymbolResolver.hpp"
#include "enginescript/types/TypeChecker.hpp"
#include "enginescript/ir/IRLowering.hpp"
#include "enginescript/codegen/MetadataEmitter.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptMetadataTests...\n";

    SourceManager sm("/workspace");
    DiagnosticEngine diags;

    std::string player_code = R"(
        @behaviour
        export class PlatformerPlayer extends Behaviour {
            constructor(
                @inject private input: Input,
                @component private body: RigidBody2D,
                @optional private animator: Animator2D?
            ) {}

            @expose({ min: 0.0, max: 1000.0 })
            walkSpeed: Float = 220.0;

            @expose
            jumpSpeed: Float = 480.0;
        }
    )";

    FileId f1 = sm.register_source("Source/Behaviours/PlatformerPlayer.e2s", player_code);
    Lexer l1(f1, player_code, diags);
    Parser p1(f1, "Source/Behaviours/PlatformerPlayer.e2s", l1.tokenize_all(), diags);
    auto m1 = p1.parse_module();

    SymbolResolver resolver(sm, diags);
    resolver.resolve_module(m1);

    TypeChecker checker(resolver, diags);
    checker.check_module(m1);

    IRLowering lowering(checker);
    IRModule ir_mod = lowering.lower_module(*m1);

    std::string json_manifest = MetadataEmitter::emit_json({ir_mod});
    assert(!json_manifest.empty());
    assert(json_manifest.find("PlatformerPlayer") != std::string::npos);
    assert(json_manifest.find("walkSpeed") != std::string::npos);
    assert(json_manifest.find("RigidBody2D") != std::string::npos);

    // Parse back to inspect headlessly
    ProjectMetadataManifest manifest = MetadataEmitter::parse_json(json_manifest);
    assert(manifest.language_version == "1.0.0");
    assert(manifest.behaviours.size() == 1);

    const auto& b = manifest.behaviours[0];
    assert(b.name == "PlatformerPlayer");
    assert(b.dependencies.size() == 3);
    assert(b.dependencies[0].name == "input" && b.dependencies[0].is_service);
    assert(b.dependencies[1].name == "body" && b.dependencies[1].is_component && !b.dependencies[1].is_optional);
    assert(b.dependencies[2].name == "animator" && b.dependencies[2].is_optional);
    assert(b.properties.size() == 2);
    assert(b.properties[0].name == "walkSpeed");
    assert(b.properties[0].min_value.has_value() && *b.properties[0].min_value == 0.0);
    assert(b.properties[0].max_value.has_value() && *b.properties[0].max_value == 1000.0);

    std::cout << "[PASS] EngineScriptMetadataTests passed!\n";
    return 0;
}
