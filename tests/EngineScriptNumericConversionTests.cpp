#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "enginescript/core/SourceManager.hpp"
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/module/GameModule.hpp"
#include "enginescript/parser/Parser.hpp"
#include "enginescript/symbols/SymbolResolver.hpp"
#include "enginescript/types/NumericTypes.hpp"
#include "enginescript/types/TypeChecker.hpp"

using namespace EngineScript;

namespace {

bool check_source(const std::string& path, const std::string& source, DiagnosticEngine& diagnostics) {
    SourceManager source_manager("/workspace");
    const FileId file = source_manager.register_source(path, source);
    Lexer lexer(file, source, diagnostics);
    Parser parser(file, path, lexer.tokenize_all(), diagnostics);
    const auto module = parser.parse_module();
    SymbolResolver resolver(source_manager, diagnostics);
    resolver.resolve_module(module);
    TypeChecker checker(resolver, diagnostics);
    return checker.check_module(module);
}

} // namespace

int main() {
    std::cout << "[TEST] Running EngineScriptNumericConversionTests...\n";

    constexpr std::array<std::array<bool, 10>, 10> expected_implicit{{
        {{true,  true,  true,  true,  false, false, false, false, true,  true }}, // Int8
        {{false, true,  true,  true,  false, false, false, false, true,  true }}, // Int16
        {{false, false, true,  true,  false, false, false, false, true,  true }}, // Int32
        {{false, false, false, true,  false, false, false, false, false, false}}, // Int64
        {{false, true,  true,  true,  true,  true,  true,  true,  true,  true }}, // UInt8
        {{false, false, true,  true,  false, true,  true,  true,  true,  true }}, // UInt16
        {{false, false, false, true,  false, false, true,  true,  false, true }}, // UInt32
        {{false, false, false, false, false, false, false, true,  false, false}}, // UInt64
        {{false, false, false, false, false, false, false, false, true,  true }}, // Float32
        {{false, false, false, false, false, false, false, false, false, true }}, // Float64
    }};
    for (std::size_t source = 0; source < NUMERIC_TYPES.size(); ++source) {
        for (std::size_t target = 0; target < NUMERIC_TYPES.size(); ++target) {
            assert(is_implicit_numeric_conversion(NUMERIC_TYPES[source].kind, NUMERIC_TYPES[target].kind) ==
                expected_implicit[source][target]);
        }
    }

    std::string explicit_matrix;
    for (const NumericTypeInfo& source : NUMERIC_TYPES) {
        for (const NumericTypeInfo& target : NUMERIC_TYPES) {
            explicit_matrix += "function from" + std::string(source.canonical_name) + "To" +
                std::string(target.canonical_name) + "(value: " + std::string(source.canonical_name) +
                "): " + std::string(target.canonical_name) + " { return " +
                std::string(target.canonical_name) + "(value); }\n";
        }
    }
    explicit_matrix += R"(
        function aliases(score: Int, count: UInt, speed: Float): Void {
            let a: Int = Int(score);
            let b: UInt = UInt(count);
            let c: Float = Float(speed);
            let d: Float = Float(score);
        }
    )";
    DiagnosticEngine matrix_diagnostics;
    assert(check_source("Source/NumericMatrix.e2s", explicit_matrix, matrix_diagnostics));
    assert(!matrix_diagnostics.has_errors());

    std::string accepted_implicit;
    std::string rejected_implicit;
    std::size_t rejected_count = 0;
    for (std::size_t source = 0; source < NUMERIC_TYPES.size(); ++source) {
        for (std::size_t target = 0; target < NUMERIC_TYPES.size(); ++target) {
            const NumericTypeInfo& source_info = NUMERIC_TYPES[source];
            const NumericTypeInfo& target_info = NUMERIC_TYPES[target];
            std::string& program = expected_implicit[source][target] ? accepted_implicit : rejected_implicit;
            program += "function implicit" + std::to_string(source) + "To" + std::to_string(target) +
                "(value: " + std::string(source_info.canonical_name) + "): Void { let converted: " +
                std::string(target_info.canonical_name) + " = value; }\n";
            rejected_count += !expected_implicit[source][target];
        }
    }
    DiagnosticEngine accepted_implicit_diagnostics;
    assert(check_source("Source/AcceptedImplicitMatrix.e2s", accepted_implicit, accepted_implicit_diagnostics));
    assert(!accepted_implicit_diagnostics.has_errors());
    DiagnosticEngine rejected_implicit_diagnostics;
    assert(!check_source("Source/RejectedImplicitMatrix.e2s", rejected_implicit, rejected_implicit_diagnostics));
    assert(rejected_implicit_diagnostics.error_count() == rejected_count);
    for (const Diagnostic& diagnostic : rejected_implicit_diagnostics.get_diagnostics()) {
        if (diagnostic.code != "E3010") {
            std::cerr << "Unexpected diagnostic " << diagnostic.code << ": " << diagnostic.message << '\n';
        }
        assert(diagnostic.code == "E3010");
    }

    DiagnosticEngine operator_diagnostics;
    const std::string operator_source = R"(
        function accepted(a: Int16, b: UInt8, c: Float, d: UInt16): Void {
            let integerResult: Int16 = a + b;
            let floatResult: Float = c + d;
        }
        function rejected(a: Int, b: UInt, c: Float64, d: Int64): Void {
            let mixedSign = a + b;
            let wideFloat = c + d;
        }
    )";
    assert(!check_source("Source/NumericOperators.e2s", operator_source, operator_diagnostics));
    std::size_t promotion_errors = 0;
    for (const Diagnostic& diagnostic : operator_diagnostics.get_diagnostics()) {
        promotion_errors += diagnostic.code == "E3062";
    }
    assert(promotion_errors == 2);

    DiagnosticEngine invalid_diagnostics;
    const std::string invalid_sources = R"(
        function invalid(entity: Entity, component: RigidBody2D): Void {
            let arity0: Int = Int();
            let arity2: Int = Int(1, 2);
            let boolean: Int = Int(true);
            let stringValue: Int = Int("12");
            let duration: Int = Int(1.ms);
            let vector: Int = Int(Vec2(1.0, 2.0));
            let entityValue: Int = Int(entity);
            let componentValue: Int = Int(component);
        }
    )";
    assert(!check_source("Source/InvalidNumericConversions.e2s", invalid_sources, invalid_diagnostics));
    std::size_t arity_errors = 0;
    std::size_t source_errors = 0;
    for (const Diagnostic& diagnostic : invalid_diagnostics.get_diagnostics()) {
        arity_errors += diagnostic.code == "E3060";
        source_errors += diagnostic.code == "E3061";
    }
    assert(arity_errors == 2);
    assert(source_errors == 6);

    assert(numericConvert<int8_t>(int64_t{500}) == std::numeric_limits<int8_t>::max());
    assert(numericConvert<int8_t>(int64_t{-500}) == std::numeric_limits<int8_t>::lowest());
    assert(numericConvert<uint8_t>(int64_t{-1}) == 0);
    assert(numericConvert<int32_t>(3.99) == 3);
    assert(numericConvert<int32_t>(-3.99) == -3);
    assert(numericConvert<int32_t>(std::numeric_limits<double>::quiet_NaN()) == 0);
    assert(numericConvert<int32_t>(std::numeric_limits<double>::infinity()) ==
        std::numeric_limits<int32_t>::max());
    assert(numericConvert<int32_t>(-std::numeric_limits<double>::infinity()) ==
        std::numeric_limits<int32_t>::lowest());
    assert(numericConvert<uint32_t>(-std::numeric_limits<double>::infinity()) == 0u);
    assert(numericConvert<uint32_t>(-100.25) == 0u);
    assert(numericConvert<uint32_t>(1.0e100) == std::numeric_limits<uint32_t>::max());
    assert(numericConvert<float>(int32_t{16'777'217}) == static_cast<float>(int32_t{16'777'217}));
    assert(std::isnan(numericConvert<float>(std::numeric_limits<double>::quiet_NaN())));
    assert(std::isinf(numericConvert<float>(std::numeric_limits<double>::infinity())));
    assert(numericConvert<double>(1.25f) == 1.25);

    std::cout << "[PASS] EngineScriptNumericConversionTests passed!\n";
    return 0;
}
