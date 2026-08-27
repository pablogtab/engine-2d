#include "core/AssetDatabase.hpp"
#include "editor/scripting/ScriptAnalyzer.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void write_source(const std::filesystem::path& path, const std::string& source) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << source;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace Engine;

    const fs::path root = fs::temp_directory_path() / "engine2d_script_analyzer_import_tests";
    std::error_code error;
    fs::remove_all(root, error);

    write_source(root / "Assets/Scripts/Dependency.e2s", R"(
@behaviour
export class Dependency extends Behaviour {
    value(): Float { return 7.0; }
}
)");
    write_source(root / "Assets/Scripts/Consumer.e2s", R"(
import { Dependency } from "./Dependency";

@behaviour
export class Consumer extends Behaviour {
    inspect(other: Entity): Float {
        let dependency: Dependency? = other.getComponent<Dependency>();
        if (dependency == null) return 0.0;
        return dependency.value();
    }
}
)");

    AssetDatabase database(root);
    const AssetScanResult scan = database.scan("Assets");
    assert(scan.ready == 2);
    const AssetRecord* consumer = database.find_by_path(root / "Assets/Scripts/Consumer.e2s");
    assert(consumer);

    const ScriptAnalysisResult result = ScriptAnalyzer::analyze_asset(consumer->handle, database);
    if (!result.success) {
        for (const ScriptDiagnostic& diagnostic : result.diagnostics) {
            std::cerr << diagnostic.line << ':' << diagnostic.column << ' '
                      << diagnostic.message << '\n';
        }
    }
    assert(result.success);
    assert(result.diagnostics.empty());

    fs::remove_all(root, error);
    std::cout << "ScriptAnalyzerTests passed\n";
    return 0;
}
