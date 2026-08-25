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
                let score: Int = Int(walkSpeed);
                let label: String = String(score);
                let position: Vec2 = Vec2(10.0, 20.0);
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

    const auto& update_statements = ir_cls.methods[0].blocks[0].statements;
    assert(update_statements.size() >= 3);
    const auto* score_declaration = dynamic_cast<const IRVarDecl*>(update_statements[1].op.get());
    const auto* label_declaration = dynamic_cast<const IRVarDecl*>(update_statements[2].op.get());
    const auto* position_declaration = dynamic_cast<const IRVarDecl*>(update_statements[3].op.get());
    const auto* numeric_conversion = score_declaration
        ? dynamic_cast<const IRNumericConvert*>(score_declaration->initializer.get()) : nullptr;
    const auto* value_construction = position_declaration
        ? dynamic_cast<const IRValueConstruct*>(position_declaration->initializer.get()) : nullptr;
    const auto* string_conversion = label_declaration
        ? dynamic_cast<const IRStringConvert*>(label_declaration->initializer.get()) : nullptr;
    assert(numeric_conversion && numeric_conversion->kind == IROpKind::NumericConvert);
    assert(string_conversion && string_conversion->kind == IROpKind::StringConvert);
    assert(value_construction && value_construction->kind == IROpKind::ValueConstruct);
    assert(!dynamic_cast<const IRCall*>(score_declaration->initializer.get()));
    assert(!dynamic_cast<const IRCall*>(position_declaration->initializer.get()));

    // Verify Invariants
    IRVerifier verifier(diags);
    bool ok = verifier.verify_module(ir_mod);
    assert(ok);
    assert(!diags.has_errors());

    DiagnosticEngine invalid_ir_diagnostics;
    auto invalid_source = std::make_shared<IRConstant>();
    invalid_source->result_type = Type::make_primitive(TypeKind::Bool);
    auto invalid_conversion = std::make_shared<IRNumericConvert>();
    invalid_conversion->kind = IROpKind::NumericConvert;
    invalid_conversion->target_kind = TypeKind::Int32;
    invalid_conversion->result_type = Type::make_primitive(TypeKind::Int32);
    invalid_conversion->operand = invalid_source;
    auto invalid_string_conversion = std::make_shared<IRStringConvert>();
    invalid_string_conversion->kind = IROpKind::StringConvert;
    invalid_string_conversion->result_type = Type::make_primitive(TypeKind::String);
    invalid_string_conversion->operand = invalid_source;
    auto invalid_construction = std::make_shared<IRValueConstruct>();
    invalid_construction->kind = IROpKind::ValueConstruct;
    invalid_construction->target_kind = TypeKind::Vec2;
    invalid_construction->result_type = Type::make_primitive(TypeKind::Vec2);
    IRModule invalid_module;
    IRClass invalid_class;
    invalid_class.name = "InvalidIR";
    IRMethod invalid_method;
    invalid_method.name = "test";
    IRBlock invalid_block;
    invalid_block.statements.push_back({invalid_conversion, {}});
    invalid_block.statements.push_back({invalid_string_conversion, {}});
    invalid_block.statements.push_back({invalid_construction, {}});
    invalid_method.blocks.push_back(std::move(invalid_block));
    invalid_class.methods.push_back(std::move(invalid_method));
    invalid_module.classes.push_back(std::move(invalid_class));
    IRVerifier invalid_verifier(invalid_ir_diagnostics);
    assert(!invalid_verifier.verify_module(invalid_module));
    bool rejected_conversion = false;
    bool rejected_string_conversion = false;
    bool rejected_construction = false;
    for (const Diagnostic& diagnostic : invalid_ir_diagnostics.get_diagnostics()) {
        rejected_conversion |= diagnostic.code == "E4026";
        rejected_string_conversion |= diagnostic.code == "E4040";
        rejected_construction |= diagnostic.code == "E4027";
    }
    assert(rejected_conversion && rejected_string_conversion && rejected_construction);

    DiagnosticEngine service_diags;
    const std::string service_code = R"(
        struct Ping { value: Int; }
        function services(events: Events, assets: Assets, jobs: Jobs, ping: Ping, seed: Int): Void {
            events.emit(ping);
            events.next<Ping>();
            assets.load<Texture>("Assets/ping.png");
            jobs.run(() => seed + 1);
        }
    )";
    const FileId f2 = sm.register_source("Source/Services.e2s", service_code);
    Lexer l2(f2, service_code, service_diags);
    Parser p2(f2, "Source/Services.e2s", l2.tokenize_all(), service_diags);
    const auto m2 = p2.parse_module();
    SymbolResolver service_resolver(sm, service_diags);
    service_resolver.resolve_module(m2);
    TypeChecker service_checker(service_resolver, service_diags);
    assert(service_checker.check_module(m2));
    IRLowering service_lowering(service_checker);
    const IRModule service_ir = service_lowering.lower_module(*m2);
    assert(service_ir.functions.size() == 1);
    const auto& service_statements = service_ir.functions[0].blocks[0].statements;
    assert(dynamic_cast<const IRTypedEventEmit*>(service_statements[0].op.get()));
    assert(dynamic_cast<const IRTypedEventNext*>(service_statements[1].op.get()));
    assert(dynamic_cast<const IRAssetLoad*>(service_statements[2].op.get()));
    assert(dynamic_cast<const IRJobRun*>(service_statements[3].op.get()));
    IRVerifier service_verifier(service_diags);
    assert(service_verifier.verify_module(service_ir));
    assert(!service_diags.has_errors());

    std::cout << "[PASS] EngineScriptIRTests passed!\n";
    return 0;
}
