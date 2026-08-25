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
                let label: String = "Coins";
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

    if (!ok) {
        std::cerr << diags.render_cli(sm);
    }
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

        async function loadEffect(assets: Assets): Task<Result<Asset<ParticleEffect2D>, String>> {
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

    // Test 7: Value constructors return their declared vector/color type.
    DiagnosticEngine value_constructor_diags;
    const std::string value_constructor_code = R"(
        function values(): Void {
            let v2: Vec2 = Vec2(-58.0, 28.0);
            let v3: Vec3 = Vec3(1.0, 2.0, 3.0);
            let v4: Vec4 = Vec4(1.0, 2.0, 3.0, 4.0);
            let color: Color = Color(1.0, 0.5, 0.25, 1.0);
        }
    )";
    const FileId f7 = sm.register_source("Source/ValueConstructors.e2s", value_constructor_code);
    Lexer l7(f7, value_constructor_code, value_constructor_diags);
    Parser p7(f7, "Source/ValueConstructors.e2s", l7.tokenize_all(), value_constructor_diags);
    const auto m7 = p7.parse_module();
    SymbolResolver res7(sm, value_constructor_diags);
    res7.resolve_module(m7);
    TypeChecker check7(res7, value_constructor_diags);
    assert(check7.check_module(m7));
    assert(!value_constructor_diags.has_errors());

    // EngineScript 2.0 value literals, deterministic collections, flow, and
    // typed events form one coherent frontend contract.
    DiagnosticEngine v2_diags;
    const std::string v2_code = R"(
        export struct EnemyDefeated {
            enemy: Entity;
            score: Int;
        }

        function process(events: Events, entity: Entity): Int {
            let event: EnemyDefeated = { enemy: entity, score: 10 };
            let scores: Map<String, Int> = Map { "player" => 10 };
            let tags: Set<String> = Set { "enemy", "flying" };
            let axis: FixedArray<Float, 2> = FixedArray [0.0, 1.0];
            let sum: Int = 0;
            for (let index: Int = 0; index < 2; index = index + 1) {
                if (index == 1) { continue; }
                sum = sum + Int(axis[index]);
            }
            for (let tag in tags) {
                if (tag == "flying") { break; }
            }
            events.emit(event);
            return sum + (scores["player"] ?? 0);
        }
    )";
    const FileId f8 = sm.register_source("Source/V2.e2s", v2_code);
    Lexer l8(f8, v2_code, v2_diags);
    Parser p8(f8, "Source/V2.e2s", l8.tokenize_all(), v2_diags);
    const auto m8 = p8.parse_module();
    SymbolResolver res8(sm, v2_diags);
    res8.resolve_module(m8);
    TypeChecker check8(res8, v2_diags);
    if (!check8.check_module(m8)) std::cerr << v2_diags.render_cli(sm);
    assert(!v2_diags.has_errors());

    DiagnosticEngine removed_api_diags;
    const std::string removed_api_code = R"(
        function invalidText(): Void {
            let removed: String = toString(1);
            let implicit: String = "score=" + 1;
        }
    )";
    const FileId f9 = sm.register_source("Source/RemovedTextApi.e2s", removed_api_code);
    Lexer l9(f9, removed_api_code, removed_api_diags);
    Parser p9(f9, "Source/RemovedTextApi.e2s", l9.tokenize_all(), removed_api_diags);
    const auto m9 = p9.parse_module();
    SymbolResolver res9(sm, removed_api_diags);
    res9.resolve_module(m9);
    TypeChecker check9(res9, removed_api_diags);
    check9.check_module(m9);
    assert(removed_api_diags.has_errors());
    bool missing_to_string = false;
    bool rejected_concat = false;
    for (const Diagnostic& diagnostic : removed_api_diags.get_diagnostics()) {
        missing_to_string |= diagnostic.message.find("toString") != std::string::npos;
        rejected_concat |= diagnostic.code == "E3063";
    }
    assert(missing_to_string && rejected_concat);

    // Jobs only accepts inline closures with immutable Send captures and Send results.
    DiagnosticEngine jobs_diags;
    const std::string jobs_code = R"(
        function validJob(jobs: Jobs, seed: Int): Task<Result<Int, String>> {
            const multiplier: Int = 2;
            return jobs.run(() => seed * multiplier);
        }

        function invalidJobs(jobs: Jobs, entity: Entity): Void {
            let mutableSeed: Int = 3;
            jobs.run(() => mutableSeed + 1);
            jobs.run(() => entity);
        }
    )";
    const FileId f10 = sm.register_source("Source/Jobs.e2s", jobs_code);
    Lexer l10(f10, jobs_code, jobs_diags);
    Parser p10(f10, "Source/Jobs.e2s", l10.tokenize_all(), jobs_diags);
    const auto m10 = p10.parse_module();
    SymbolResolver res10(sm, jobs_diags);
    res10.resolve_module(m10);
    TypeChecker check10(res10, jobs_diags);
    check10.check_module(m10);
    bool rejected_mutable_capture = false;
    bool rejected_ecs_capture = false;
    bool rejected_non_send_result = false;
    for (const Diagnostic& diagnostic : jobs_diags.get_diagnostics()) {
        rejected_mutable_capture |= diagnostic.code == "E3087" &&
            diagnostic.message.find("mutableSeed") != std::string::npos;
        rejected_ecs_capture |= diagnostic.code == "E3087" &&
            diagnostic.message.find("entity") != std::string::npos;
        rejected_non_send_result |= diagnostic.code == "E3085";
    }
    assert(rejected_mutable_capture && rejected_ecs_capture && rejected_non_send_result);

    DiagnosticEngine collection_contract_diags;
    const std::string bad_collection_contracts = R"(
        struct Box<T> { value: T; }
        function invalidCollections(): Void {
            let missingSize: FixedArray<Float>;
            let zeroSize: FixedArray<Float, 0>;
            let mapArity: Map<String>;
            let boxArity: Box<Int, String>;
            let numbers: Array<Int> = [1];
            numbers.push("wrong");
            numbers.pop(1);
            let tags: Set<String> = Set { "enemy" };
            tags.add(2);
            let scores: Map<String, Int> = Map { "player" => 1 };
            scores.set(2, "wrong");
            scores.get(2);
            scores.keys("unexpected");
        }
    )";
    const FileId f11 = sm.register_source("Source/BadCollectionContracts.e2s", bad_collection_contracts);
    Lexer l11(f11, bad_collection_contracts, collection_contract_diags);
    Parser p11(f11, "Source/BadCollectionContracts.e2s", l11.tokenize_all(), collection_contract_diags);
    const auto m11 = p11.parse_module();
    SymbolResolver res11(sm, collection_contract_diags);
    res11.resolve_module(m11);
    TypeChecker check11(res11, collection_contract_diags);
    check11.check_module(m11);
    std::size_t arity_errors = 0;
    std::size_t collection_method_errors = 0;
    bool invalid_fixed_size = false;
    for (const Diagnostic& diagnostic : collection_contract_diags.get_diagnostics()) {
        arity_errors += diagnostic.code == "E3089";
        collection_method_errors += diagnostic.code == "E3080" || diagnostic.code == "E3081";
        invalid_fixed_size |= diagnostic.code == "E3090";
    }
    assert(arity_errors == 3);
    assert(collection_method_errors == 6);
    assert(invalid_fixed_size);

    DiagnosticEngine generic_class_diags;
    const std::string generic_class_code = R"(
        class UnsafeHandle {}
        class Box<T extends Send> {
            constructor(private value: T) {}
            read(): T { return value; }
        }
        function invalidBox(): Void {
            let unsafe: UnsafeHandle = UnsafeHandle();
            let box: Box<UnsafeHandle> = Box<UnsafeHandle>(unsafe);
        }
    )";
    const FileId f12 = sm.register_source("Source/GenericClass.e2s", generic_class_code);
    Lexer l12(f12, generic_class_code, generic_class_diags);
    Parser p12(f12, "Source/GenericClass.e2s", l12.tokenize_all(), generic_class_diags);
    const auto m12 = p12.parse_module();
    SymbolResolver res12(sm, generic_class_diags);
    res12.resolve_module(m12);
    TypeChecker check12(res12, generic_class_diags);
    check12.check_module(m12);
    bool rejected_unsafe_generic_class = false;
    for (const Diagnostic& diagnostic : generic_class_diags.get_diagnostics()) {
        rejected_unsafe_generic_class |= diagnostic.code == "E3078";
    }
    assert(rejected_unsafe_generic_class);

    std::cout << "[PASS] EngineScriptTypeCheckerTests passed!\n";
    return 0;
}
