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

    // Test 4: Relational '<' after member access must not be speculatively
    // diagnosed as a malformed generic call.
    DiagnosticEngine comparison_diags;
    const std::string comparison_code = R"(
        let horizontal = difference.x < -amount;
        let vertical = difference.y < -amount;
        let threshold = difference.x < -1.0;
        let texture = assets.load<Array<Texture>>("Assets/Textures/pet.png");
    )";
    Lexer comparison_lexer(4, comparison_code, comparison_diags);
    Parser comparison_parser(
        4,
        "ComparisonAndGenericCall.e2s",
        comparison_lexer.tokenize_all(),
        comparison_diags
    );
    const auto comparison_module = comparison_parser.parse_module();
    assert(comparison_module != nullptr);
    assert(!comparison_diags.has_errors());

    // Test 5: Parser recovery on malformed syntax
    DiagnosticEngine err_diags;
    std::string bad_code = R"(
        class Broken {
            let invalid syntax here;
            validMethod(): Void {}
        }
    )";
    Lexer l4(5, bad_code, err_diags);
    Parser p4(5, "Broken.e2s", l4.tokenize_all(), err_diags);
    auto mod4 = p4.parse_module();
    assert(err_diags.has_errors());
    assert(mod4 != nullptr); // Parser recovered and produced AST

    DiagnosticEngine v2_diags;
    const std::string v2_code = R"(
        interface Factory<T extends Send> { make(value: T): T; }
        struct Box<T extends Send> implements Factory<T> {
            value: T;
            make(value: T): T { return value; }
        }
        function literals(): Void {
            let map = Map { "one" => 1 };
            let set = Set { "one" };
            let fixed = FixedArray [1, 2];
            let closure = (value: Int) => value + 1;
        }
    )";
    Lexer v2_lexer(6, v2_code, v2_diags);
    Parser v2_parser(6, "V2.e2s", v2_lexer.tokenize_all(), v2_diags);
    const auto v2_module = v2_parser.parse_module();
    assert(v2_module != nullptr);
    assert(!v2_diags.has_errors());
    const auto* interface_decl = dynamic_cast<InterfaceDecl*>(v2_module->declarations[0].get());
    const auto* struct_decl = dynamic_cast<StructDecl*>(v2_module->declarations[1].get());
    assert(interface_decl && interface_decl->generic_params.front().constraint->name == "Send");
    assert(struct_decl && struct_decl->interfaces.front().name == "Factory");

    DiagnosticEngine interpolation_diags;
    const std::string interpolation_code = R"(
        function label(score: Int, damage: Float): String {
            return `Score ${score:04}\nDamage ${damage:.2f}`;
        }
    )";
    Lexer interpolation_lexer(7, interpolation_code, interpolation_diags);
    Parser interpolation_parser(
        7,
        "Interpolation.e2s",
        interpolation_lexer.tokenize_all(),
        interpolation_diags
    );
    const auto interpolation_module = interpolation_parser.parse_module();
    assert(interpolation_module != nullptr);
    assert(!interpolation_diags.has_errors());
    const auto* interpolation_function = dynamic_cast<FunctionDecl*>(interpolation_module->declarations.front().get());
    const auto* interpolation_return = interpolation_function
        ? dynamic_cast<ReturnStmt*>(interpolation_function->body->statements.front().get()) : nullptr;
    const auto* interpolation = interpolation_return
        ? dynamic_cast<InterpolatedStringExpr*>(interpolation_return->value.get()) : nullptr;
    assert(interpolation && interpolation->segments.size() == 4);
    assert(interpolation->segments[1].format == "04");
    assert(interpolation->segments[3].format == ".2f");

    DiagnosticEngine named_diags;
    const std::string named_code = R"(
        function score(base: Int, multiplier: Int = 2, bonus: Int = 1): Int {
            return base * multiplier + bonus;
        }
        function use(): Int { return score(10, bonus: 4, multiplier: 3); }
    )";
    Lexer named_lexer(8, named_code, named_diags);
    Parser named_parser(8, "NamedArguments.e2s", named_lexer.tokenize_all(), named_diags);
    const auto named_module = named_parser.parse_module();
    assert(named_module && !named_diags.has_errors());
    const auto* use_function = dynamic_cast<FunctionDecl*>(named_module->declarations[1].get());
    const auto* use_return = dynamic_cast<ReturnStmt*>(use_function->body->statements.front().get());
    const auto* named_call = dynamic_cast<CallExpr*>(use_return->value.get());
    assert(named_call && named_call->argument_names.size() == 3);
    assert(!named_call->argument_names[0]);
    assert(named_call->argument_names[1] == "bonus");
    assert(named_call->argument_names[2] == "multiplier");

    std::cout << "[PASS] EngineScriptParserTests passed!\n";
    return 0;
}
