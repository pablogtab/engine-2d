#include "ScriptAnalyzer.hpp"

#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include "enginescript/EngineScript.hpp"

namespace Engine {

ScriptAnalysisResult ScriptAnalyzer::analyze_asset(
    AssetHandle handle,
    const AssetDatabase& asset_database
) {
    const AssetRecord* record = asset_database.find(handle);
    if (!record || record->type != AssetType::Script || record->status != AssetStatus::Ready) {
        return {false, {{1, 1, "Script asset is missing or unavailable"}}};
    }
    const std::string source = FileSystem::read_text(asset_database.resolve(handle));
    if (source.empty()) return {false, {{1, 1, "Script source is empty or unreadable"}}};
    return analyze_source(source, record->source_path);
}

ScriptAnalysisResult ScriptAnalyzer::analyze_source(
    std::string source,
    std::string module_name
) {
    ScriptAnalysisResult result;

    EngineScript::DiagnosticEngine diags;
    EngineScript::SourceManager sm(".");
    EngineScript::FileId fid = sm.register_source(module_name, source);
    EngineScript::Lexer lexer(fid, source, diags);
    auto tokens = lexer.tokenize_all();
    EngineScript::Parser parser(fid, module_name, std::move(tokens), diags);
    auto module = parser.parse_module();
    if (module) {
        EngineScript::SymbolResolver resolver(sm, diags);
        resolver.resolve_module(module);
        EngineScript::TypeChecker checker(resolver, diags);
        checker.check_module(module);
    }
    for (const auto& diag : diags.get_diagnostics()) {
        result.diagnostics.push_back({
            static_cast<int>(diag.span.start_line),
            static_cast<int>(diag.span.start_column),
            diag.message
        });
    }
    result.success = !diags.has_errors();
    return result;
}

} // namespace Engine

