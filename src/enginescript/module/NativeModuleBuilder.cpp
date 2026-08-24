#include "NativeModuleBuilder.hpp"

#include "../codegen/CppEmitter.hpp"
#include "../editor/EngineScriptWorkspace.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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
    uint32_t build_number
) {
    namespace fs = std::filesystem;
    NativeModuleBuildResult result;
    const fs::path output = fs::path(project_root) / "Library" / "EngineScript" / "Generated" /
        std::to_string(build_number);
    std::error_code error;
    fs::create_directories(output, error);
    if (error) {
        result.error = "Could not create EngineScript native build directory";
        return result;
    }

    std::vector<fs::path> sources;
    for (const CppOutput& generated : workspace.compile().generated_cpp) {
        const fs::path header = output / generated.header_filename;
        const fs::path source = output / generated.source_filename;
        fs::create_directories(header.parent_path(), error);
        std::ofstream(header) << generated.header_content;
        std::ofstream(source) << generated.source_content;
        if (!header.parent_path().empty() && error) {
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
    std::ofstream(entrypoint) << emitter.emit_game_module_entrypoint(
        workspace.ir_modules(), fs::path(project_root).filename().string(), build_number
    );
    sources.push_back(entrypoint);

    const fs::path library = library_path(output, build_number);
    std::ostringstream command;
    command << quote(ENGINE_SCRIPT_NATIVE_CXX_COMPILER) << " -std=c++20 ";
#if defined(_WIN32)
    command << " /LD ";
#elif defined(__APPLE__)
    command << " -dynamiclib -fPIC ";
#else
    command << " -shared -fPIC ";
#endif
    command << " -I" << quote(ENGINE_SCRIPT_NATIVE_SOURCE_DIR)
            << " -I" << quote(ENGINE_SCRIPT_NATIVE_VENDOR_DIR)
            << " -I" << quote(ENGINE_SCRIPT_NATIVE_GLM_INCLUDE_DIR)
            << " -I" << quote(output) << " -o " << quote(library);
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
