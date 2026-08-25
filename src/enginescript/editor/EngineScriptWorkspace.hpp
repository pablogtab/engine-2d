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
#include "../generics/Generics.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace EngineScript {

struct GenericCodeGrowthReport {
    size_t unique_instantiations = 0;
    size_t generated_cpp_bytes = 0;
    std::vector<GenericInstantiation> instantiations;
};

struct CompilationResult {
    bool success = false;
    size_t error_count = 0;
    size_t warning_count = 0;
    std::string metadata_json;
    std::vector<CppOutput> generated_cpp;
    std::vector<Diagnostic> diagnostics;
    GenericCodeGrowthReport generic_code_growth;
};

struct WorkspaceInferredType {
    SourceSpan span;
    std::string type_name;
};

enum class WorkspaceDocumentSymbolKind {
    Variable,
    Parameter,
    Field,
    Import
};

struct WorkspaceDocumentSymbol {
    FileId file_id{INVALID_FILE_ID};
    std::string name;
    std::string type_name;
    SourceSpan declaration_span;
    SourceSpan scope_span;
    WorkspaceDocumentSymbolKind kind{WorkspaceDocumentSymbolKind::Variable};
    bool is_inferred{false};
};

enum class WorkspaceExportedSymbolKind {
    Class,
    Struct,
    Interface,
    Enum,
    Function,
    Constant
};

struct WorkspaceExportedSymbol {
    FileId file_id{INVALID_FILE_ID};
    std::string module_path;
    std::string name;
    SourceSpan declaration_span;
    WorkspaceExportedSymbolKind kind{WorkspaceExportedSymbolKind::Class};
    bool is_exported{false};
};

struct WorkspaceSymbolOccurrence {
    uint64_t symbol_id{0};
    std::string name;
    SourceSpan span;
    bool is_declaration{false};
    SymbolKind kind{SymbolKind::Variable};
};

class EngineScriptWorkspace {
public:
    explicit EngineScriptWorkspace(std::string project_root);

    void set_project_root(std::string project_root);
    const std::string& project_root() const { return m_source_manager.project_root(); }

    void add_source_file(const std::string& relative_path, const std::string& content);
    void update_source_file(const std::string& relative_path, const std::string& content);
    void remove_source_file(const std::string& relative_path);
    // The compiler and the language server deliberately share this discovery
    // entrypoint, so closed documents participate in the same module graph.
    size_t discover_project_sources();
    bool restore_source_file_from_disk(const std::string& relative_path);

    CompilationResult compile(bool force_rebuild = false);

    const SourceManager& source_manager() const { return m_source_manager; }
    const DiagnosticEngine& diagnostic_engine() const { return m_diagnostics; }
    const DependencyGraph& dependency_graph() const { return m_dep_graph; }
    const CompilerCache& compiler_cache() const { return m_cache; }
    const ProjectMetadataManifest& metadata_manifest() const { return m_manifest; }
    const std::vector<IRModule>& ir_modules() const { return m_ir_modules; }
    const std::vector<std::string>& registered_files() const { return m_registered_files; }
    std::optional<std::string> inferred_type_at(FileId file_id, uint32_t byte_offset) const;
    std::optional<std::string> symbol_type_at(FileId file_id, std::string_view name, uint32_t byte_offset) const;
    std::vector<WorkspaceDocumentSymbol> symbols_in_scope(FileId file_id, uint32_t byte_offset) const;
    std::vector<WorkspaceDocumentSymbol> document_symbols(FileId file_id) const;
    std::vector<WorkspaceExportedSymbol> exports_from(std::string_view module_path) const;
    const std::vector<WorkspaceExportedSymbol>& workspace_symbols() const { return m_exported_symbols; }
    std::optional<uint64_t> symbol_id_at(FileId file_id, uint32_t byte_offset) const;
    std::optional<WorkspaceSymbolOccurrence> symbol_occurrence_at(FileId file_id, uint32_t byte_offset) const;
    std::vector<WorkspaceSymbolOccurrence> occurrences_for_symbol(uint64_t symbol_id) const;

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
    std::vector<WorkspaceInferredType> m_inferred_types;
    std::vector<WorkspaceDocumentSymbol> m_document_symbols;
    std::vector<WorkspaceExportedSymbol> m_exported_symbols;
    std::vector<WorkspaceSymbolOccurrence> m_symbol_occurrences;
    std::vector<std::string> m_registered_files;
    std::string m_last_workspace_hash;
    std::optional<CompilationResult> m_last_result;
};

} // namespace EngineScript
