#include "NativeModuleBuilder.hpp"

#include "../codegen/CppEmitter.hpp"
#include "../editor/EngineScriptWorkspace.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace EngineScript {
namespace {

std::string quote(const std::filesystem::path& value) {
    return "\"" + value.string() + "\"";
}

std::filesystem::path library_path(const std::filesystem::path& directory, uint32_t build_number) {
#if defined(_WIN32)
    return directory / ("EngineScript_" + std::to_string(build_number) + ".dll");
#elif defined(__APPLE__)
    return directory / ("libEngineScript_" + std::to_string(build_number) + ".dylib");
#else
    return directory / ("libEngineScript_" + std::to_string(build_number) + ".so");
#endif
}

} // namespace

NativeModuleBuildResult NativeModuleBuilder::build(
    EngineScriptWorkspace& workspace,
    const std::string& project_root,
    uint32_t build_number,
    NativeModuleBuildMode mode
) {
    namespace fs = std::filesystem;
    NativeModuleBuildResult result;
    const fs::path output = fs::path(project_root) / "Library" / "EngineScript" / "Generated" /
        std::to_string(build_number);
    result.output_directory = output.string();
    std::error_code error;
    fs::create_directories(output, error);
    if (error) {
        result.error = "Could not create EngineScript native build directory";
        return result;
    }

    std::vector<fs::path> sources;
    const CompilationResult compilation = workspace.compile();
    if (!compilation.success) {
        result.error = "EngineScript project compilation failed before native build";
        return result;
    }
    std::unordered_set<std::string> generated_names;
    for (const CppOutput& generated : compilation.generated_cpp) {
        if (!generated_names.emplace(generated.header_filename).second ||
            !generated_names.emplace(generated.source_filename).second) {
            result.error = "EngineScript native build has colliding generated filenames";
            return result;
        }
        const fs::path header = output / generated.header_filename;
        const fs::path source = output / generated.source_filename;
        fs::create_directories(header.parent_path(), error);
        std::ofstream header_stream(header);
        std::ofstream source_stream(source);
        header_stream << generated.header_content;
        source_stream << generated.source_content;
        if (error || !header_stream || !source_stream) {
            result.error = "Could not write EngineScript generated source";
            return result;
        }
        sources.push_back(source);
    }
    if (sources.empty()) {
        result.error = "EngineScript project has no generated sources";
        return result;
    }

    CppEmitter emitter(workspace.source_manager());
    const fs::path entrypoint = output / "GameModule.generated.cpp";
    std::ofstream entrypoint_stream(entrypoint);
    entrypoint_stream << emitter.emit_game_module_entrypoint(
        workspace.ir_modules(), fs::path(project_root).filename().string(), build_number
    );
    entrypoint_stream.close();
    if (!entrypoint_stream) {
        result.error = "Could not write EngineScript module entrypoint";
        return result;
    }
    sources.push_back(entrypoint);

    if (mode == NativeModuleBuildMode::StaticSources) {
        result.success = true;
        return result;
    }

    const fs::path library = library_path(output, build_number);
    std::ostringstream command;
#if defined(_WIN32)
    command << quote(ENGINE_SCRIPT_NATIVE_CXX_COMPILER) << " /nologo /std:c++20 /LD"
            << " /I" << quote(ENGINE_SCRIPT_NATIVE_SOURCE_DIR)
            << " /I" << quote(ENGINE_SCRIPT_NATIVE_VENDOR_DIR)
            << " /I" << quote(ENGINE_SCRIPT_NATIVE_GLM_INCLUDE_DIR)
            << " /I" << quote(output) << " /Fe" << quote(library);
#elif defined(__APPLE__)
    command << quote(ENGINE_SCRIPT_NATIVE_CXX_COMPILER) << " -std=c++20 -dynamiclib -fPIC ";
#else
    command << quote(ENGINE_SCRIPT_NATIVE_CXX_COMPILER) << " -std=c++20 -shared -fPIC ";
#endif
#if !defined(_WIN32)
    command << " -I" << quote(ENGINE_SCRIPT_NATIVE_SOURCE_DIR)
            << " -I" << quote(ENGINE_SCRIPT_NATIVE_VENDOR_DIR)
            << " -I" << quote(ENGINE_SCRIPT_NATIVE_GLM_INCLUDE_DIR)
            << " -I" << quote(output) << " -o " << quote(library);
#endif
    for (const fs::path& source : sources) command << " " << quote(source);
    if (std::system(command.str().c_str()) != 0 || !fs::exists(library)) {
        result.error = "EngineScript native C++ compilation failed";
        return result;
    }
    result.success = true;
    result.library_path = library.string();
    return result;
}

} // namespace EngineScript
