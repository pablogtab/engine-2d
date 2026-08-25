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

void require_generated_cpp_compiles(const CppOutput& output, const std::string& entrypoint) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_codegen_native_compile";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    const fs::path header = root / output.header_filename;
    const fs::path source = root / output.source_filename;
    const fs::path entrypoint_source = root / "GameModule.generated.cpp";
    std::ofstream(header) << output.header_content;
    std::ofstream(source) << output.source_content;
    std::ofstream(entrypoint_source) << entrypoint;

#if defined(_MSC_VER)
    const std::string command = quote_argument(ENGINE_TEST_CXX_COMPILER) +
        " /nologo /std:c++20 /Zs /I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "src") +
        " /I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "vendor") +
        " /I" + quote_argument(root) + " " + quote_argument(source) + " " + quote_argument(entrypoint_source);
#else
    const std::string command = quote_argument(ENGINE_TEST_CXX_COMPILER) +
        " -std=c++20 -fsyntax-only -I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "src") +
        " -I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "vendor") +
        " -I" + quote_argument(ENGINE_TEST_GLM_INCLUDE_DIR) +
        " -I" + quote_argument(root) + " " + quote_argument(source) + " " + quote_argument(entrypoint_source);
#endif

    assert(std::system(command.c_str()) == 0);
    fs::remove_all(root, error);
}

void require_generated_cpp_runs(const CppOutput& output, const std::string& driver_content) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_codegen_numeric_runtime";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    const fs::path header = root / output.header_filename;
    const fs::path source = root / output.source_filename;
    const fs::path driver = root / "NumericDriver.cpp";
#if defined(_WIN32)
    const fs::path executable = root / "NumericDriver.exe";
#else
    const fs::path executable = root / "NumericDriver";
#endif
    std::ofstream(header) << output.header_content;
    std::ofstream(source) << output.source_content;
    std::ofstream(driver) << driver_content;

#if defined(_MSC_VER)
    const std::string command = quote_argument(ENGINE_TEST_CXX_COMPILER) +
        " /nologo /EHsc /std:c++20 /I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "src") +
        " /I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "vendor") +
        " /I" + quote_argument(root) + " " + quote_argument(source) + " " + quote_argument(driver) +
        " /Fe" + quote_argument(executable);
#else
    const std::string command = quote_argument(ENGINE_TEST_CXX_COMPILER) +
        " -std=c++20 -I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "src") +
        " -I" + quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "vendor") +
        " -I" + quote_argument(ENGINE_TEST_GLM_INCLUDE_DIR) +
        " -I" + quote_argument(root) + " " + quote_argument(source) + " " + quote_argument(driver) +
        " -o " + quote_argument(executable);
#endif

    assert(std::system(command.c_str()) == 0);
    assert(std::system(quote_argument(executable).c_str()) == 0);
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
            @inject private log: Log,
            @inject private audio: Audio,
            @inject private scene: Scene,
                @component private body: RigidBody2D,
                @component private sprite: SpriteRenderer2D
            ) {}

            @expose({ min: 0.0, max: 1000.0 })
            walkSpeed: Float = 220.0;

            @expose
            offset: Vec2 = Vec2(-58.0, 28.0);

            override update(deltaTime: Float): Void {
                let axis: Float = input.axis("Horizontal");
                body.velocity.x = axis * walkSpeed;
                sprite.flipX = axis < 0.0;
                log("Updated");
                audio.playSound("Assets/Audio/step.wav");
                scene.load("Assets/Scenes/Next.scene");
            }

            override on_collision_enter(other: Entity): Void {
                let lowerName: String = other.name.toLowerCase();
                let coinIndex: Int = lowerName.indexOf("coin");
                if (lowerName.includes("coin") && other.tag.startsWith("Collectible")) {
                    other.destroy();
                }
            }

            async respawn(): Task<Void> {
                await delay(500.ms);
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
    const std::string entrypoint = emitter.emit_game_module_entrypoint({ir_mod}, "Gameplay");

    assert(!output.header_content.empty());
    assert(!output.source_content.empty());
    assert(output.header_content.find("class PlatformerPlayer") != std::string::npos);
    assert(output.header_content.find("float walkSpeed") != std::string::npos);
    assert(output.header_content.find("glm::vec2 offset") != std::string::npos);
    assert(output.source_content.find("glm::vec2((-58.0f), 28.0f)") != std::string::npos);
    assert(output.source_content.find("create_PlatformerPlayer") != std::string::npos);
    assert(output.source_content.find("EngineScript::NativeBehaviour* create_PlatformerPlayer") != std::string::npos);
    assert(entrypoint.find("engine_script_module_exports") != std::string::npos);
    assert(entrypoint.find("{\"PlatformerPlayer\", create_PlatformerPlayer, dependencies_PlatformerPlayer") != std::string::npos);
    assert(entrypoint.find("NativeDependencyKind::Service") != std::string::npos);
    assert(entrypoint.find("NativeDependencyKind::RequiredComponent") != std::string::npos);
    assert(output.source_content.find("EngineScript::StringOps::to_lower") != std::string::npos);
    assert(output.source_content.find("EngineScript::StringOps::includes") != std::string::npos);
    assert(output.header_content.find("EngineScript::SpriteRenderer2D* sprite") != std::string::npos);
    assert(output.header_content.find("EngineScript::Audio* audio") != std::string::npos);
    assert(output.header_content.find("EngineScript::Scene* scene") != std::string::npos);
    assert(output.header_content.find("set_exposed_property") != std::string::npos);
    assert(output.source_content.find("(*m_log)(\"Updated\")") != std::string::npos);
    assert(output.source_content.find("other.destroy()") != std::string::npos);
    assert(output.source_content.find("m_audio->playSound(\"Assets/Audio/step.wav\")") != std::string::npos);
    assert(output.source_content.find("m_scene->load(\"Assets/Scenes/Next.scene\")") != std::string::npos);
    assert(output.source_content.find("PlatformerPlayer::set_exposed_property") != std::string::npos);
    assert(output.source_content.find("PlatformerPlayer::get_exposed_property") != std::string::npos);
    assert(output.source_content.find("co_await this->delay(0.5f)") != std::string::npos);
    assert(output.source_content.find("#line") != std::string::npos);
    require_generated_cpp_compiles(output, entrypoint);

    DiagnosticEngine numeric_diags;
    const std::string numeric_code = R"(
        export class NumericRuntime {
            convertFloatToInt(value: Float64): Int { return Int(value); }
            convertToByte(value: Int64): UInt8 { return UInt8(value); }
            convertToUnsigned(value: Float64): UInt { return UInt(value); }
            intToFloat(value: Int): Float { return Float(value); }
            narrowFloat(value: Float64): Float { return Float(value); }
            identity(value: Float): Float { return Float(value); }
        }
    )";
    const FileId numeric_file = sm.register_source("Source/NumericRuntime.e2s", numeric_code);
    Lexer numeric_lexer(numeric_file, numeric_code, numeric_diags);
    Parser numeric_parser(numeric_file, "Source/NumericRuntime.e2s", numeric_lexer.tokenize_all(), numeric_diags);
    const auto numeric_module = numeric_parser.parse_module();
    SymbolResolver numeric_resolver(sm, numeric_diags);
    numeric_resolver.resolve_module(numeric_module);
    TypeChecker numeric_checker(numeric_resolver, numeric_diags);
    assert(numeric_checker.check_module(numeric_module));
    IRLowering numeric_lowering(numeric_checker);
    const IRModule numeric_ir = numeric_lowering.lower_module(*numeric_module);
    CppOutput numeric_output = emitter.emit_module(numeric_ir);
    assert(numeric_output.source_content.find("EngineScript::numericConvert<int32_t>(value)") != std::string::npos);
    assert(numeric_output.source_content.find("EngineScript::numericConvert<uint8_t>(value)") != std::string::npos);
    assert(numeric_output.source_content.find("EngineScript::numericConvert<float>(value)") != std::string::npos);
    assert(numeric_output.source_content.find("NumericRuntime::identity(float value)") != std::string::npos);
    require_generated_cpp_runs(numeric_output, R"(
        #include "NumericRuntime.generated.hpp"
        #include <cassert>
        #include <cmath>
        #include <cstdint>
        #include <limits>

        int main() {
            NumericRuntime conversions;
            assert(conversions.convertFloatToInt(-3.9) == -3);
            assert(conversions.convertFloatToInt(std::numeric_limits<double>::quiet_NaN()) == 0);
            assert(conversions.convertFloatToInt(std::numeric_limits<double>::infinity()) ==
                std::numeric_limits<int32_t>::max());
            assert(conversions.convertToByte(1000) == std::numeric_limits<uint8_t>::max());
            assert(conversions.convertToUnsigned(-10.5) == 0u);
            assert(conversions.intToFloat(3) == 3.0f);
            assert(std::isinf(conversions.narrowFloat(std::numeric_limits<double>::infinity())));
            assert(conversions.identity(1.25f) == 1.25f);
            return 0;
        }
    )");

    std::cout << "[PASS] EngineScriptCodegenTests passed!\n";
    return 0;
}
