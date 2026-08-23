#include <cassert>
#include <iostream>
#include "enginescript/core/SourceManager.hpp"
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/parser/Parser.hpp"
#include "enginescript/symbols/SymbolResolver.hpp"
#include "enginescript/types/TypeChecker.hpp"
#include "enginescript/ir/IRLowering.hpp"
#include "enginescript/codegen/CppEmitter.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptCodegenTests...\n";

    SourceManager sm("/workspace");
    DiagnosticEngine diags;

    std::string player_code = R"(
        @behaviour
        export class PlatformerPlayer extends Behaviour {
            constructor(
                @inject private input: Input,
                @component private body: RigidBody2D
            ) {}

            @expose({ min: 0.0, max: 1000.0 })
            walkSpeed: Float = 220.0;

            override update(deltaTime: Float): Void {
                let axis: Float = input.axis("Horizontal");
                body.velocity.x = axis * walkSpeed;
            }
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

    CppEmitter emitter(sm);
    CppOutput output = emitter.emit_module(ir_mod);

    assert(!output.header_content.empty());
    assert(!output.source_content.empty());
    assert(output.header_content.find("class PlatformerPlayer") != std::string::npos);
    assert(output.header_content.find("float walkSpeed") != std::string::npos);
    assert(output.source_content.find("create_PlatformerPlayer") != std::string::npos);
    assert(output.source_content.find("#line") != std::string::npos);

    std::cout << "[PASS] EngineScriptCodegenTests passed!\n";
    return 0;
}
