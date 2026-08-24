#pragma once

#include "../core/SourceManager.hpp"
#include "../core/DiagnosticEngine.hpp"
#include "../symbols/SymbolResolver.hpp"
#include "../types/TypeChecker.hpp"
#include "../ir/IRLowering.hpp"
#include "../codegen/CppEmitter.hpp"
#include "../codegen/MetadataEmitter.hpp"
#include "../cache/CompilerCache.hpp"
#include "../depgraph/DependencyGraph.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace EngineScript {

struct CompilationResult {
    bool success = false;
    size_t error_count = 0;
    size_t warning_count = 0;
    std::string metadata_json;
    std::vector<CppOutput> generated_cpp;
    std::vector<Diagnostic> diagnostics;
};

class EngineScriptWorkspace {
public:
    explicit EngineScriptWorkspace(std::string project_root);

    void set_project_root(std::string project_root);
    const std::string& project_root() const { return m_source_manager.project_root(); }

    void add_source_file(const std::string& relative_path, const std::string& content);
    void update_source_file(const std::string& relative_path, const std::string& content);
    void remove_source_file(const std::string& relative_path);

    CompilationResult compile(bool force_rebuild = false);

    const SourceManager& source_manager() const { return m_source_manager; }
    const DiagnosticEngine& diagnostic_engine() const { return m_diagnostics; }
    const DependencyGraph& dependency_graph() const { return m_dep_graph; }
    const CompilerCache& compiler_cache() const { return m_cache; }
    const ProjectMetadataManifest& metadata_manifest() const { return m_manifest; }
    const std::vector<IRModule>& ir_modules() const { return m_ir_modules; }

    // Serialization helper for scene files
    std::string serialize_behaviour_state(const std::string& behaviour_type, const std::unordered_map<std::string, std::string>& properties) const;
    std::unordered_map<std::string, std::string> deserialize_behaviour_state(const std::string& json_str) const;

private:
    SourceManager m_source_manager;
    DiagnosticEngine m_diagnostics;
    DependencyGraph m_dep_graph;
    CompilerCache m_cache;
    ProjectMetadataManifest m_manifest;
    std::vector<IRModule> m_ir_modules;
    std::vector<std::string> m_registered_files;
    std::string m_last_workspace_hash;
    std::optional<CompilationResult> m_last_result;
};

} // namespace EngineScript
