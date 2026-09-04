#include "NativeModuleBuilder.hpp"

#include "../cache/CompilerCache.hpp"
#include "../codegen/CppEmitter.hpp"
#include "../editor/EngineScriptWorkspace.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
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
constexpr int NATIVE_OBJECT_CACHE_VERSION = 1;

std::filesystem::path cache_manifest_path(const std::filesystem::path& project_root) {
    return project_root / "Library" / "EngineScript" / "NativeModuleCache.json";
}

std::filesystem::path object_cache_directory(const std::filesystem::path& project_root) {
    return project_root / "Library" / "EngineScript" / "ObjectCache";
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void collect_generated_header_dependencies(
    const std::string& content,
    const std::unordered_map<std::string, std::string>& headers,
    std::unordered_set<std::string>& dependencies
) {
    constexpr std::string_view include_prefix = "#include \"";
    std::size_t cursor = 0;
    while ((cursor = content.find(include_prefix, cursor)) != std::string::npos) {
        const std::size_t name_start = cursor + include_prefix.size();
        const std::size_t name_end = content.find('"', name_start);
        if (name_end == std::string::npos) break;
        const std::string name = content.substr(name_start, name_end - name_start);
        cursor = name_end + 1;
        const auto header = headers.find(name);
        if (header == headers.end() || !dependencies.emplace(name).second) continue;
        collect_generated_header_dependencies(header->second, headers, dependencies);
    }
}

std::string build_object_hash(
    const std::filesystem::path& source,
    const std::string& source_content,
    const std::unordered_map<std::string, std::string>& headers
) {
    std::unordered_set<std::string> dependency_set;
    collect_generated_header_dependencies(source_content, headers, dependency_set);
    std::vector<std::string> dependencies(dependency_set.begin(), dependency_set.end());
    std::sort(dependencies.begin(), dependencies.end());

    std::string fingerprint;
    fingerprint += "version=" + std::to_string(NATIVE_OBJECT_CACHE_VERSION);
    fingerprint += "\ncompiler=" + std::string(ENGINE_SCRIPT_NATIVE_CXX_COMPILER);
    fingerprint += "\ncompiler_id=" + std::string(ENGINE_SCRIPT_NATIVE_CXX_ID);
    fingerprint += "\ncompiler_version=" + std::string(ENGINE_SCRIPT_NATIVE_CXX_VERSION);
    fingerprint += "\ncache=" + std::string(ENGINE_SCRIPT_COMPILER_CACHE_ID);
    fingerprint += "\nsdk=" + std::string(ENGINE_SCRIPT_SDK_HASH);
    fingerprint += "\nsource=" + source.filename().string() + ":" +
        CompilerCache::hash_content(source_content);
    for (const std::string& dependency : dependencies) {
        fingerprint += "\nheader=" + dependency + ":" +
            CompilerCache::hash_content(headers.at(dependency));
    }
    return CompilerCache::hash_content(fingerprint);
}

std::string object_extension() {
#if defined(_WIN32)
    return ".obj";
#else
    return ".o";
#endif
}

std::string compile_object_command(
    const std::filesystem::path& source,
    const std::filesystem::path& object,
    const std::filesystem::path& generated_directory
) {
    std::ostringstream command;
#if defined(_WIN32)
    command << quote(ENGINE_SCRIPT_NATIVE_CXX_COMPILER) << " /nologo /std:c++20 /c"
            << " /I" << quote(ENGINE_SCRIPT_NATIVE_SOURCE_DIR)
            << " /I" << quote(ENGINE_SCRIPT_NATIVE_VENDOR_DIR)
            << " /I" << quote(ENGINE_SCRIPT_NATIVE_GLM_INCLUDE_DIR)
            << " /I" << quote(generated_directory)
            << " /Fo" << quote(object) << " " << quote(source);
#else
    command << quote(ENGINE_SCRIPT_NATIVE_CXX_COMPILER) << " -std=c++20 -fPIC -c"
            << " -I" << quote(ENGINE_SCRIPT_NATIVE_SOURCE_DIR)
            << " -I" << quote(ENGINE_SCRIPT_NATIVE_VENDOR_DIR)
            << " -I" << quote(ENGINE_SCRIPT_NATIVE_GLM_INCLUDE_DIR)
            << " -I" << quote(generated_directory)
            << " -o " << quote(object) << " " << quote(source);
#endif
    return command.str();
}

std::string link_library_command(
    const std::vector<std::filesystem::path>& objects,
    const std::filesystem::path& library
) {
    std::ostringstream command;
#if defined(_WIN32)
    command << quote(ENGINE_SCRIPT_NATIVE_CXX_COMPILER) << " /nologo /LD /Fe" << quote(library);
#elif defined(__APPLE__)
    command << quote(ENGINE_SCRIPT_NATIVE_CXX_COMPILER) << " -dynamiclib -o " << quote(library);
#else
    command << quote(ENGINE_SCRIPT_NATIVE_CXX_COMPILER) << " -shared -o " << quote(library);
#endif
    for (const std::filesystem::path& object : objects) command << " " << quote(object);
    return command.str();
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

    std::unordered_map<std::string, std::string> generated_headers;
    for (fs::recursive_directory_iterator iterator(output, error), end;
         !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_regular_file() || iterator->path().extension() != ".hpp") continue;
        const std::string relative = iterator->path().lexically_relative(output).generic_string();
        generated_headers.emplace(relative, read_text_file(iterator->path()));
    }
    if (error) {
        result.error = "Could not inspect EngineScript generated headers";
        return result;
    }

    const fs::path object_cache = object_cache_directory(root);
    fs::create_directories(object_cache, error);
    if (error) {
        result.error = "Could not create EngineScript native object cache";
        return result;
    }

    struct CompileTask {
        fs::path source;
        fs::path object;
        fs::path temporary_object;
    };
    std::vector<CompileTask> compile_tasks;
    std::vector<fs::path> objects;
    objects.reserve(sources.size());
    const std::string extension = object_extension();
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    for (std::size_t index = 0; index < sources.size(); ++index) {
        const fs::path& source = sources[index];
        const std::string source_content = read_text_file(source);
        if (source_content.empty()) {
            result.error = "Could not read EngineScript generated source: " + source.filename().string();
            return result;
        }
        const std::string object_hash = build_object_hash(source, source_content, generated_headers);
        const fs::path object = object_cache / (object_hash + extension);
        const bool reusable = fs::is_regular_file(object, error) && fs::file_size(object, error) > 0;
        if (error) error.clear();
        if (reusable) {
            ++result.reused_objects;
        } else {
            compile_tasks.push_back({
                source,
                object,
                object_cache / (object_hash + "." + std::to_string(nonce) + "." +
                    std::to_string(index) + ".tmp" + extension)
            });
        }
        objects.push_back(object);
    }

    const fs::path library = library_path(output, build_number);
    if (progress) progress(NativeModuleBuildPhase::CompilingNative);

    std::atomic<std::size_t> next_task{0};
    std::atomic<bool> compile_failed{false};
    std::mutex error_mutex;
    std::string compile_error;
    const unsigned hardware_threads = std::max(1u, std::thread::hardware_concurrency());
    const std::size_t worker_count = std::min<std::size_t>(
        compile_tasks.size(), std::min<std::size_t>(hardware_threads, 8)
    );
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&]() {
            while (true) {
                const std::size_t task_index = next_task.fetch_add(1);
                if (task_index >= compile_tasks.size()) return;
                const CompileTask& task = compile_tasks[task_index];
                const std::string command = compile_object_command(task.source, task.temporary_object, output);
                if (std::system(command.c_str()) != 0 || !fs::is_regular_file(task.temporary_object)) {
                    compile_failed.store(true);
                    std::scoped_lock lock(error_mutex);
                    if (compile_error.empty()) {
                        compile_error = "EngineScript native C++ compilation failed for " +
                            task.source.filename().string();
                    }
                    continue;
                }
                std::error_code rename_error;
                fs::rename(task.temporary_object, task.object, rename_error);
                if (rename_error) {
                    std::error_code inspect_error;
                    if (fs::is_regular_file(task.object, inspect_error) &&
                        fs::file_size(task.object, inspect_error) > 0) {
                        fs::remove(task.temporary_object, inspect_error);
                    } else {
                        compile_failed.store(true);
                        std::scoped_lock lock(error_mutex);
                        if (compile_error.empty()) {
                            compile_error = "Could not store EngineScript native object cache entry";
                        }
                    }
                }
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
    if (compile_failed.load()) {
        for (const CompileTask& task : compile_tasks) fs::remove(task.temporary_object, error);
        result.error = compile_error.empty() ? "EngineScript native C++ compilation failed" : compile_error;
        return result;
    }
    result.compiled_objects = static_cast<std::uint32_t>(compile_tasks.size());

    if (std::system(link_library_command(objects, library).c_str()) != 0 || !fs::exists(library)) {
        result.error = "EngineScript native object linking failed";
        return result;
    }
    result.success = true;
    result.library_path = library.string();
    store_cached_library(root, result.source_hash, library);
    return result;
}

} // namespace EngineScript
