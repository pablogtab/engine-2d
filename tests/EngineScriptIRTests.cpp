#include <cassert>
#include <iostream>
#include "enginescript/core/SourceManager.hpp"
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/parser/Parser.hpp"
#include "enginescript/symbols/SymbolResolver.hpp"
#include "enginescript/types/TypeChecker.hpp"
#include "enginescript/ir/IRLowering.hpp"
#include "enginescript/ir/IRVerifier.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptIRTests...\n";

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

            override update(deltaTime: Float): Void {
                let axis: Float = input.axis("Horizontal");
                body.velocity.x = axis * walkSpeed;
            }

            async respawn(): Task<Void> {
                await delay(500.ms);
                animator?.play("idle");
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
    assert(!diags.has_errors());

    IRLowering lowering(checker);
    IRModule ir_mod = lowering.lower_module(*m1);

    assert(ir_mod.classes.size() == 1);
    const auto& ir_cls = ir_mod.classes[0];
    assert(ir_cls.name == "PlatformerPlayer");
    assert(ir_cls.is_behaviour);
    assert(ir_cls.constructor_params.size() == 3);
    assert(ir_cls.properties.size() == 1);
    assert(ir_cls.properties[0].name == "walkSpeed");
    assert(ir_cls.properties[0].numeric_attributes.at("min") == 0.0);
    assert(ir_cls.properties[0].numeric_attributes.at("max") == 1000.0);

    // Verify Invariants
    IRVerifier verifier(diags);
    bool ok = verifier.verify_module(ir_mod);
    assert(ok);
    assert(!diags.has_errors());

    std::cout << "[PASS] EngineScriptIRTests passed!\n";
    return 0;
}
