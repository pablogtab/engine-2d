#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "enginescript/core/SourceManager.hpp"
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/parser/Parser.hpp"
#include "enginescript/symbols/SymbolResolver.hpp"
#include "enginescript/types/TypeChecker.hpp"
#include "enginescript/ir/IRLowering.hpp"
#include "enginescript/codegen/CppEmitter.hpp"

using namespace EngineScript;

namespace {

std::string quote_argument(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

void require_generated_cpp_compiles(const CppOutput& output) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_codegen_native_compile";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    const fs::path header = root / output.header_filename;
    const fs::path source = root / output.source_filename;
    std::ofstream(header) << output.header_content;
    std::ofstream(source) << output.source_content;

#if defined(_MSC_VER)
    const std::string command = quote_argument(ENGINE_TEST_CXX_COMPILER) +
        " /nologo /std:c++20 /Zs /I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "src") +
        " /I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "vendor") +
        " /I" + quote_argument(root) + " " + quote_argument(source);
#else
    const std::string command = quote_argument(ENGINE_TEST_CXX_COMPILER) +
        " -std=c++20 -fsyntax-only -I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "src") +
        " -I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "vendor") +
        " -I" + quote_argument(ENGINE_TEST_GLM_INCLUDE_DIR) +
        " -I" + quote_argument(root) + " " + quote_argument(source);
#endif

    assert(std::system(command.c_str()) == 0);
    fs::remove_all(root, error);
}

} // namespace

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
    require_generated_cpp_compiles(output);

    std::cout << "[PASS] EngineScriptCodegenTests passed!\n";
    return 0;
}
