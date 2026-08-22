#pragma once

#include "../../core/AssetHandle.hpp"

#include <string>
#include <vector>

namespace Engine {

class AssetDatabase;

struct ScriptDiagnostic {
    int line{1};
    int column{1};
    std::string message;
};

struct ScriptAnalysisResult {
    bool success{false};
    std::vector<ScriptDiagnostic> diagnostics;
};

class ScriptAnalyzer {
public:
    [[nodiscard]] static ScriptAnalysisResult analyze_asset(
        AssetHandle handle,
        const AssetDatabase& asset_database
    );
    [[nodiscard]] static ScriptAnalysisResult analyze_source(
        std::string source,
        std::string module_name = "Script"
    );
};

} // namespace Engine
