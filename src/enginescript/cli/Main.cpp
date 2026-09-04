#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "enginescript/EngineScript.hpp"
#include "enginescript/module/NativeModuleBuilder.hpp"

namespace fs = std::filesystem;
using namespace EngineScript;

void print_usage() {
    std::cout << "EngineScript Compiler (e2sc) v2.0.0\n"
              << "Usage: e2sc <project_dir> [options]\n\n"
              << "Options:\n"
              << "  -o, --output <dir>    Output directory for generated C++ and metadata (default: <project_dir>/.e2s_build)\n"
              << "  -c, --check           Type-check and verify without generating C++ files\n"
              << "  --native-module        Build the native gameplay module under Library/EngineScript\n"
              << "  --json, --json-diagnostics\n"
              << "                         Emit a machine-readable result with detailed diagnostics\n"
              << "  -v, --version         Print version information\n"
              << "  -h, --help            Show this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string project_dir;
    std::string output_dir;
    bool check_only = false;
    bool json_diagnostics = false;
    bool native_module = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "e2sc version 2.0.0 (language " << ENGINE_SCRIPT_LANGUAGE_VERSION
                      << ", ABI " << ENGINE_SCRIPT_ABI_VERSION << ")\n";
            return 0;
        } else if (arg == "-c" || arg == "--check") {
            check_only = true;
        } else if (arg == "--json" || arg == "--json-diagnostics") {
            json_diagnostics = true;
        } else if (arg == "--native-module") {
            native_module = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                output_dir = argv[++i];
            } else {
                std::cerr << "Error: Missing argument for " << arg << "\n";
                return 1;
            }
        } else if (arg[0] != '-') {
            project_dir = arg;
        } else {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            return 1;
        }
    }

    if (project_dir.empty()) {
        std::cerr << "Error: No project directory specified.\n";
        print_usage();
        return 1;
    }

    if (!fs::exists(project_dir)) {
        std::cerr << "Error: Project directory '" << project_dir << "' does not exist.\n";
        return 1;
    }

    if (output_dir.empty()) {
        output_dir = (fs::path(project_dir) / ".e2s_build").string();
    }

    EngineScriptWorkspace workspace(project_dir);

    const size_t file_count = workspace.discover_project_sources();

    if (file_count == 0) {
        if (json_diagnostics) {
            std::cout << "{\"success\":true,\"error_count\":0,\"warning_count\":0,"
                         "\"generic_instantiations\":0,\"generated_cpp_bytes\":0,\"diagnostics\":[]}\n";
        } else {
            std::cout << "No .e2s source files found in " << project_dir << "\n";
        }
        return 0;
    }

    CompilationResult res = workspace.compile();

    if (json_diagnostics) {
        std::cout << "{\"error_count\":" << res.error_count
                  << ",\"warning_count\":" << res.warning_count 
                  << ",\"generic_instantiations\":" << res.generic_code_growth.unique_instantiations
                  << ",\"generated_cpp_bytes\":" << res.generic_code_growth.generated_cpp_bytes
                  << ",\"success\":" << (res.success ? "true" : "false")
                  << ",\"diagnostics\":"
                  << workspace.diagnostic_engine().render_json(workspace.source_manager())
                  << "}\n";
    }

    if (!res.success) {
        if (!json_diagnostics) {
            for (const Diagnostic& diagnostic : res.diagnostics) {
                const SourceLocation location = workspace.source_manager().get_location(
                    diagnostic.span.file_id, diagnostic.span.start_offset
                );
                std::cerr << (location.file_id != INVALID_FILE_ID
                    ? workspace.source_manager().get_file(location.file_id)->path + ":" + std::to_string(location.line) + ":" + std::to_string(location.column) + ": "
                    : std::string{})
                    << diagnostic.code << ": " << diagnostic.message << "\n";
            }
        }
        return 1;
    }

    if (check_only) {
        if (!json_diagnostics) {
            std::cout << "Verification successful. " << file_count << " files checked with 0 errors; "
                      << res.generic_code_growth.unique_instantiations << " concrete generic instantiations.\n";
        }
        return 0;
    }

    if (native_module) {
        const NativeModuleBuildResult native = NativeModuleBuilder::build(workspace, project_dir, 1);
        if (!native.success) {
            std::cerr << "Native module build failed: " << native.error << "\n";
            return 1;
        }
        if (!json_diagnostics) {
            std::cout << "Built native EngineScript module: " << native.library_path;
            if (native.cache_hit) {
                std::cout << " (module cache hit)";
            } else {
                std::cout << " (" << native.compiled_objects
                          << (native.compiled_objects == 1 ? " object compiled, " : " objects compiled, ")
                          << native.reused_objects << " reused)";
            }
            std::cout << "\n";
        }
        return 0;
    }

    // Write output files
    try {
        fs::create_directories(output_dir);

        // Write metadata manifest
        std::ofstream manifest_out(fs::path(output_dir) / "manifest.json");
        manifest_out << res.metadata_json;

        // Write generated C++ files
        for (const auto& cpp : res.generated_cpp) {
            fs::path h_path = fs::path(output_dir) / cpp.header_filename;
            fs::path s_path = fs::path(output_dir) / cpp.source_filename;

            fs::create_directories(h_path.parent_path());
            fs::create_directories(s_path.parent_path());

            std::ofstream h_out(h_path);
            h_out << cpp.header_content;

            std::ofstream s_out(s_path);
            s_out << cpp.source_content;
        }

        CppEmitter emitter(workspace.source_manager());
        std::ofstream entrypoint_out(fs::path(output_dir) / "GameModule.generated.cpp");
        entrypoint_out << emitter.emit_game_module_entrypoint(
            workspace.ir_modules(),
            fs::path(project_dir).filename().string()
        );

        if (!json_diagnostics) {
            std::cout << "Successfully compiled " << file_count << " EngineScript files into " << output_dir << "\n"
                      << "Generic code growth: " << res.generic_code_growth.unique_instantiations
                      << " concrete instantiations across " << res.generic_code_growth.generated_cpp_bytes
                      << " generated C++ bytes.\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error writing build output: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
