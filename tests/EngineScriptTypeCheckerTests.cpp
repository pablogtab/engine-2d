#include <cassert>
#include <iostream>
#include "enginescript/core/SourceManager.hpp"
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/parser/Parser.hpp"
#include "enginescript/symbols/SymbolResolver.hpp"
#include "enginescript/types/TypeChecker.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptTypeCheckerTests...\n";

    SourceManager sm("/workspace");
    DiagnosticEngine diags;

    // Test 1: Valid Platformer Player type checking
    std::string player_code = R"(
        @behaviour
        export class PlatformerPlayer extends Behaviour {
            constructor(
                @inject private input: Input,
                @component private body: RigidBody2D,
                @component private sprite: SpriteRenderer2D,
                @optional private animator: Animator2D?
            ) {}

            @expose({ min: 0.0, max: 1000.0 })
            walkSpeed: Float = 220.0;

            @expose
            jumpSpeed: Float = 480.0;

            override update(deltaTime: Float): Void {
                let axis: Float = input.axis("Horizontal");
                let label: String = "Coins: " + toString(7);
                body.velocity.x = axis * walkSpeed;
                body.setIgnoreOneWay(false);
                if (axis < 0.0) {
                    sprite.flipX = true;
                } else if (axis > 0.0) {
                    sprite.flipX = false;
                }
                if (input.pressed("Jump") && body.isGrounded) {
                    body.velocity.y = jumpSpeed;
                }
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
    bool ok = checker.check_module(m1);

    assert(ok);
    assert(!diags.has_errors());

    // Test 2: Incompatible assignment type error
    DiagnosticEngine err_diags;
    std::string bad_code = R"(
        function test(): Void {
            let speed: Float = "fast";
        }
    )";
    FileId f2 = sm.register_source("Source/Bad.e2s", bad_code);
    Lexer l2(f2, bad_code, err_diags);
    Parser p2(f2, "Source/Bad.e2s", l2.tokenize_all(), err_diags);
    auto m2 = p2.parse_module();
    SymbolResolver res2(sm, err_diags);
    res2.resolve_module(m2);
    TypeChecker check2(res2, err_diags);
    check2.check_module(m2);

    assert(err_diags.has_errors());
    assert(err_diags.error_count() == 1);

    // Test 3: Missing return on all paths
    DiagnosticEngine ret_diags;
    std::string no_ret_code = R"(
        function calculate(): Float {
            let x: Float = 10.0;
        }
    )";
    FileId f3 = sm.register_source("Source/NoRet.e2s", no_ret_code);
    Lexer l3(f3, no_ret_code, ret_diags);
    Parser p3(f3, "Source/NoRet.e2s", l3.tokenize_all(), ret_diags);
    auto m3 = p3.parse_module();
    SymbolResolver res3(sm, ret_diags);
    res3.resolve_module(m3);
    TypeChecker check3(res3, ret_diags);
    check3.check_module(m3);

    assert(ret_diags.has_errors());

    // Test 4: String and Entity method contracts reject invalid arguments.
    DiagnosticEngine method_diags;
    std::string bad_methods_code = R"(
        function invalid(other: Entity): Void {
            let missing: Bool = other.name.includes();
            let wrong: Bool = other.name.startsWith(42);
            let extra: String = other.name.toLowerCase("unused");
            other.destroy(1);
        }

        @behaviour
        export class InvalidLog extends Behaviour {
            constructor(@inject private log: Log) {}

            override update(deltaTime: Float): Void {
                log(42);
            }
        }
    )";
    FileId f4 = sm.register_source("Source/BadMethods.e2s", bad_methods_code);
    Lexer l4(f4, bad_methods_code, method_diags);
    Parser p4(f4, "Source/BadMethods.e2s", l4.tokenize_all(), method_diags);
    auto m4 = p4.parse_module();
    SymbolResolver res4(sm, method_diags);
    res4.resolve_module(m4);
    TypeChecker check4(res4, method_diags);
    check4.check_module(m4);

    assert(method_diags.has_errors());
    assert(method_diags.error_count() == 5);

    // Test 5: Result constructors require an explicit matching <Value, Error> contract.
    DiagnosticEngine result_diags;
    std::string bad_results_code = R"(
        function invalidResults(): Void {
            let missing: Result<Int, String> = Result.success<Int>("wrong");
            let badError: Result<Int, String> = Result.failure<Int, String>(3);
        }
    )";
    FileId f5 = sm.register_source("Source/BadResults.e2s", bad_results_code);
    Lexer l5(f5, bad_results_code, result_diags);
    Parser p5(f5, "Source/BadResults.e2s", l5.tokenize_all(), result_diags);
    auto m5 = p5.parse_module();
    SymbolResolver res5(sm, result_diags);
    res5.resolve_module(m5);
    TypeChecker check5(res5, result_diags);
    check5.check_module(m5);

    assert(result_diags.has_errors());
    assert(result_diags.error_count() == 3);

    // Test 6: Particle assets, services, emitter controls, and options share one catalog contract.
    DiagnosticEngine particle_diags;
    std::string particle_code = R"(
        function emitEffect(
            particles: Particles,
            effect: Asset,
            position: Vec2,
            options: ParticleEmitOptions,
            emitter: ParticleEmitter2D
        ): Bool {
            emitter.play();
            emitter.burst(12);
            emitter.stop(true);
            emitter.setEffect(effect);
            options.scale = 2.0;
            return particles.emitWithOptions(effect, position, options);
        }

        async function loadEffect(assets: Assets): Task<Asset> {
            return await assets.load<ParticleEffect2D>("Assets/Particles/dust.particle");
        }
    )";
    FileId f6 = sm.register_source("Source/Particles.e2s", particle_code);
    Lexer l6(f6, particle_code, particle_diags);
    Parser p6(f6, "Source/Particles.e2s", l6.tokenize_all(), particle_diags);
    auto m6 = p6.parse_module();
    SymbolResolver res6(sm, particle_diags);
    res6.resolve_module(m6);
    TypeChecker check6(res6, particle_diags);
    assert(check6.check_module(m6));
    assert(!particle_diags.has_errors());

    std::cout << "[PASS] EngineScriptTypeCheckerTests passed!\n";
    return 0;
}
