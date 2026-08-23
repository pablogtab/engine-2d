#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "enginescript/EngineScript.hpp"

namespace fs = std::filesystem;
using namespace EngineScript;

void print_usage() {
    std::cout << "EngineScript Compiler (e2sc) v1.0.0\n"
              << "Usage: e2sc <project_dir> [options]\n\n"
              << "Options:\n"
              << "  -o, --output <dir>    Output directory for generated C++ and metadata (default: <project_dir>/.e2s_build)\n"
              << "  -c, --check           Type-check and verify without generating C++ files\n"
              << "  --json-diagnostics    Emit diagnostics as JSON formatted strings\n"
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

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "e2sc version 1.0.0 (EngineScript C++20 Backend)\n";
            return 0;
        } else if (arg == "-c" || arg == "--check") {
            check_only = true;
        } else if (arg == "--json-diagnostics") {
            json_diagnostics = true;
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

    // Recursively discover .e2s files
    size_t file_count = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(project_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".e2s") {
                std::string rel_path = fs::relative(entry.path(), project_dir).string();
                std::ifstream in(entry.path(), std::ios::in | std::ios::binary);
                if (in.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    workspace.add_source_file(rel_path, content);
                    ++file_count;
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error scanning directory: " << ex.what() << "\n";
        return 1;
    }

    if (file_count == 0) {
        std::cout << "No .e2s source files found in " << project_dir << "\n";
        return 0;
    }

    CompilationResult res = workspace.compile();

    if (json_diagnostics) {
        // Output JSON format
        std::cout << "{\"error_count\":" << res.error_count 
                  << ",\"warning_count\":" << res.warning_count 
                  << ",\"success\":" << (res.success ? "true" : "false") << "}\n";
    }

    if (!res.success) {
        return 1;
    }

    if (check_only) {
        std::cout << "Verification successful. " << file_count << " files checked with 0 errors.\n";
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

        std::cout << "Successfully compiled " << file_count << " EngineScript files into " << output_dir << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error writing build output: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
