#include <cassert>
#include <iostream>
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/parser/Parser.hpp"
#include "enginescript/ast/ASTPrinter.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptParserTests...\n";

    DiagnosticEngine diags;

    // Test 1: Parse PlatformerPlayer
    std::string platformer_code = R"(
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

            override update(deltaTime: Float): Void {
                let axis: Float = input.axis("Horizontal");
                body.velocity.x = axis * walkSpeed;
                if (input.pressed("Jump") && body.isGrounded) {
                    body.velocity.y = -jumpSpeed;
                }
            }

            async respawn(): Task<Void> {
                await delay(500.ms);
                animator?.play("idle");
            }
        }
    )";

    Lexer l1(1, platformer_code, diags);
    auto tokens1 = l1.tokenize_all();
    Parser p1(1, "Behaviours/PlatformerPlayer.e2s", std::move(tokens1), diags);
    auto module1 = p1.parse_module();

    assert(!diags.has_errors());
    assert(module1 != nullptr);
    assert(module1->declarations.size() == 1);

    auto cls = dynamic_cast<ClassDecl*>(module1->declarations[0].get());
    assert(cls != nullptr);
    assert(cls->name == "PlatformerPlayer");
    assert(cls->is_behaviour());
    assert(cls->constructor_decl != nullptr);
    assert(cls->constructor_decl->params.size() == 3);
    assert(cls->properties.size() == 2);
    assert(cls->methods.size() == 2);

    // Test 2: Pratt precedence (a + b * c)
    std::string expr_code = "let x = a + b * c;";
    Lexer l2(2, expr_code, diags);
    Parser p2(2, "TestExpr.e2s", l2.tokenize_all(), diags);
    auto mod2 = p2.parse_module();
    assert(!diags.has_errors());
    assert(mod2 != nullptr);

    // Test 3: Optional chaining and duration literals
    std::string opt_code = "let res = player?.stats?.hp ?? 100.0;";
    Lexer l3(3, opt_code, diags);
    Parser p3(3, "TestOpt.e2s", l3.tokenize_all(), diags);
    auto mod3 = p3.parse_module();
    assert(!diags.has_errors());

    // Test 4: Parser recovery on malformed syntax
    DiagnosticEngine err_diags;
    std::string bad_code = R"(
        class Broken {
            let invalid syntax here;
            validMethod(): Void {}
        }
    )";
    Lexer l4(4, bad_code, err_diags);
    Parser p4(4, "Broken.e2s", l4.tokenize_all(), err_diags);
    auto mod4 = p4.parse_module();
    assert(err_diags.has_errors());
    assert(mod4 != nullptr); // Parser recovered and produced AST

    std::cout << "[PASS] EngineScriptParserTests passed!\n";
    return 0;
}
