#include "NativeModuleBuilder.hpp"

#include "../cache/CompilerCache.hpp"
#include "../codegen/CppEmitter.hpp"
#include "../editor/EngineScriptWorkspace.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
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

constexpr int NATIVE_CACHE_VERSION = 1;

std::filesystem::path cache_manifest_path(const std::filesystem::path& project_root) {
    return project_root / "Library" / "EngineScript" / "NativeModuleCache.json";
}

std::string build_source_hash(
    const EngineScriptWorkspace& workspace,
    const CompilationResult& compilation,
    const std::string& module_name
) {
    std::vector<const CppOutput*> outputs;
    outputs.reserve(compilation.generated_cpp.size());
    for (const CppOutput& output : compilation.generated_cpp) outputs.push_back(&output);
    std::sort(outputs.begin(), outputs.end(), [](const CppOutput* lhs, const CppOutput* rhs) {
        if (lhs->header_filename != rhs->header_filename) {
            return lhs->header_filename < rhs->header_filename;
        }
        return lhs->source_filename < rhs->source_filename;
    });

    std::string fingerprint;
    fingerprint += "language=" + std::to_string(ENGINE_SCRIPT_LANGUAGE_VERSION);
    fingerprint += "\ncompiler=" + std::string(ENGINE_SCRIPT_COMPILER_CACHE_ID);
    fingerprint += "\nsdk=" + std::string(ENGINE_SCRIPT_SDK_HASH);
    fingerprint += "\nmodule=" + module_name;
    for (const CppOutput* output : outputs) {
        fingerprint += "\nheader=" + output->header_filename + ":" +
            CompilerCache::hash_content(output->header_content);
        fingerprint += "\nsource=" + output->source_filename + ":" +
            CompilerCache::hash_content(output->source_content);
    }

    CppEmitter emitter(workspace.source_manager());
    fingerprint += "\nentrypoint=" + CompilerCache::hash_content(
        emitter.emit_game_module_entrypoint(workspace.ir_modules(), module_name, 0)
    );
    return CompilerCache::hash_content(fingerprint);
}

std::optional<std::filesystem::path> cached_library(
    const std::filesystem::path& project_root,
    const std::string& source_hash
) {
    namespace fs = std::filesystem;
    std::ifstream stream(cache_manifest_path(project_root));
    if (!stream) return std::nullopt;
    const nlohmann::json manifest = nlohmann::json::parse(stream, nullptr, false);
    if (manifest.is_discarded() || manifest.value("version", 0) != NATIVE_CACHE_VERSION ||
        manifest.value("source_hash", std::string{}) != source_hash) {
        return std::nullopt;
    }
    const fs::path relative = manifest.value("library", std::string{});
    if (relative.empty() || relative.is_absolute() ||
        (!relative.empty() && relative.begin()->string() == "..")) {
        return std::nullopt;
    }
    const fs::path library = (project_root / relative).lexically_normal();
    std::error_code error;
    return fs::is_regular_file(library, error) ? std::optional<fs::path>{library} : std::nullopt;
}

void store_cached_library(
    const std::filesystem::path& project_root,
    const std::string& source_hash,
    const std::filesystem::path& library
) {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path manifest_path = cache_manifest_path(project_root);
    fs::create_directories(manifest_path.parent_path(), error);
    if (error) return;
    const fs::path relative = library.lexically_relative(project_root);
    if (relative.empty() || relative.begin()->string() == "..") return;
    std::ofstream stream(manifest_path, std::ios::trunc);
    if (!stream) return;
    stream << nlohmann::json{
        {"version", NATIVE_CACHE_VERSION},
        {"source_hash", source_hash},
        {"library", relative.generic_string()}
    }.dump(2) << '\n';
}

} // namespace

NativeModuleBuildResult NativeModuleBuilder::build(
    EngineScriptWorkspace& workspace,
    const std::string& project_root,
    uint32_t build_number,
    NativeModuleBuildMode mode,
    NativeModuleBuildProgress progress
) {
    namespace fs = std::filesystem;
    NativeModuleBuildResult result;
    if (progress) progress(NativeModuleBuildPhase::CheckingCache);
    const CompilationResult compilation = workspace.compile();
    if (!compilation.success) {
        result.error = "EngineScript project compilation failed before native build";
        return result;
    }
    const fs::path root = fs::absolute(project_root).lexically_normal();
    const std::string module_name = root.filename().string();
    result.source_hash = build_source_hash(workspace, compilation, module_name);
    if (mode != NativeModuleBuildMode::StaticSources) {
        if (const auto library = cached_library(root, result.source_hash)) {
            result.success = true;
            result.cache_hit = true;
            result.library_path = library->string();
            result.output_directory = library->parent_path().string();
            return result;
        }
    }

    if (progress) progress(NativeModuleBuildPhase::GeneratingSources);
    const fs::path output = root / "Library" / "EngineScript" / "Generated" /
        std::to_string(build_number);
    result.output_directory = output.string();
    std::error_code error;
    // Build numbers restart with each editor process. Clear this owned output
    // directory so removed scripts cannot leave stale .cpp files behind.
    fs::remove_all(output, error);
    if (error) {
        result.error = "Could not reset EngineScript native build directory";
        return result;
    }
    fs::create_directories(output, error);
    if (error) {
        result.error = "Could not create EngineScript native build directory";
        return result;
    }

    std::vector<fs::path> sources;
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
        workspace.ir_modules(), module_name, build_number
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

    if (mode == NativeModuleBuildMode::PrepareDynamicLibrary) {
        result.success = true;
        return result;
    }
    return compile_prepared(project_root, build_number, result.source_hash, std::move(progress));
}

NativeModuleBuildResult NativeModuleBuilder::compile_prepared(
    const std::string& project_root,
    uint32_t build_number,
    const std::string& source_hash,
    NativeModuleBuildProgress progress
) {
    namespace fs = std::filesystem;
    NativeModuleBuildResult result;
    const fs::path root = fs::absolute(project_root).lexically_normal();
    const fs::path output = root / "Library" / "EngineScript" / "Generated" /
        std::to_string(build_number);
    result.output_directory = output.string();
    result.source_hash = source_hash;
    std::vector<fs::path> sources;
    std::error_code error;
    if (!fs::is_directory(output, error)) {
        result.error = "EngineScript prepared source directory is unavailable";
        return result;
    }
    for (fs::recursive_directory_iterator iterator(output, error), end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_regular_file() && iterator->path().extension() == ".cpp") {
            sources.push_back(iterator->path());
        }
    }
    if (error || sources.empty()) {
        result.error = "EngineScript prepared sources are unavailable";
        return result;
    }
    std::sort(sources.begin(), sources.end());

    const fs::path library = library_path(output, build_number);
    if (progress) progress(NativeModuleBuildPhase::CompilingNative);
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
    store_cached_library(root, result.source_hash, library);
    return result;
}

} // namespace EngineScript
