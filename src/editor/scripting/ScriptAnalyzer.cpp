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
    EngineScript::EngineScriptWorkspace workspace(asset_database.project_root().string());
    for (const AssetRecord& script : asset_database.records()) {
        if (script.type != AssetType::Script || script.status != AssetStatus::Ready) continue;
        const std::string source = FileSystem::read_text(asset_database.resolve(script.handle));
        if (!source.empty()) workspace.add_source_file(script.source_path, source);
    }

    const EngineScript::SourceFile* target = workspace.source_manager().get_file(record->source_path);
    if (!target) return {false, {{1, 1, "Script source is empty or unreadable"}}};

    const EngineScript::CompilationResult compiled = workspace.compile();
    ScriptAnalysisResult result;
    result.success = true;
    for (const EngineScript::Diagnostic& diagnostic : compiled.diagnostics) {
        if (diagnostic.span.file_id != EngineScript::INVALID_FILE_ID &&
            diagnostic.span.file_id != target->id) {
            continue;
        }
        result.diagnostics.push_back({
            static_cast<int>(diagnostic.span.start_line),
            static_cast<int>(diagnostic.span.start_column),
            diagnostic.message
        });
        if (diagnostic.is_error()) result.success = false;
    }
    return result;
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
