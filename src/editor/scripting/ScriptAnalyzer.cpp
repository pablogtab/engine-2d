#include "ScriptAnalyzer.hpp"

#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include "enginescript/EngineScript.hpp"

#include <Luau/BuiltinDefinitions.h>
#include <Luau/ConfigResolver.h>
#include <Luau/Error.h>
#include <Luau/FileResolver.h>
#include <Luau/Frontend.h>

#include <string_view>
#include <unordered_map>

namespace Engine {
namespace {

bool has_strict_directive(std::string_view source) {
    constexpr std::string_view strict = "--!strict";
    if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
    return source.starts_with(strict) &&
        (source.size() == strict.size() || source[strict.size()] == '\n' || source[strict.size()] == '\r');
}

struct MemoryFileResolver final : Luau::FileResolver {
    std::unordered_map<Luau::ModuleName, std::string> sources;

    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override {
        const auto found = sources.find(name);
        if (found == sources.end()) return std::nullopt;
        return Luau::SourceCode{found->second, Luau::SourceCode::Module};
    }

    std::string getHumanReadableModuleName(const Luau::ModuleName& name) const override {
        return name;
    }
};

struct StrictConfigResolver final : Luau::ConfigResolver {
    StrictConfigResolver() {
        config.mode = Luau::Mode::Strict;
    }

    const Luau::Config& getConfig(const Luau::ModuleName&, const Luau::TypeCheckLimits&) const override {
        return config;
    }

    Luau::Config config;
};

} // namespace

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

    if (module_name.ends_with(".e2s") || source.find("@behaviour") != std::string::npos || source.find("class ") != std::string::npos) {
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

    if (!has_strict_directive(source)) {
        result.diagnostics.push_back({1, 1, "Script must start with --!strict"});
        return result;
    }

    MemoryFileResolver files;
    files.sources.emplace(module_name, std::move(source));
    StrictConfigResolver configs;
    Luau::FrontendOptions options;
    options.retainFullTypeGraphs = false;
    options.runLintChecks = true;
    Luau::Frontend frontend(&files, &configs, options);
    Luau::registerBuiltinGlobals(frontend, frontend.globals);
    const Luau::CheckResult checked = frontend.check(module_name);
    for (const Luau::TypeError& error : checked.errors) {
        result.diagnostics.push_back({
            static_cast<int>(error.location.begin.line + 1),
            static_cast<int>(error.location.begin.column + 1),
            Luau::toString(error, Luau::TypeErrorToStringOptions{&files})
        });
    }
    result.success = result.diagnostics.empty();
    return result;
}

} // namespace Engine
