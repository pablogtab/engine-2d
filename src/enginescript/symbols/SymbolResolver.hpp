#pragma once

#include "Symbol.hpp"
#include "Scope.hpp"
#include "../ast/AST.hpp"
#include "../core/DiagnosticEngine.hpp"
#include "../core/SourceManager.hpp"
#include <unordered_map>
#include <vector>
#include <memory>

namespace EngineScript {

class SymbolResolver {
public:
    SymbolResolver(const SourceManager& source_manager, DiagnosticEngine& diagnostics);

    void register_builtin_types();
    bool resolve_module(const std::shared_ptr<ModuleNode>& module_node);
    bool resolve_all(const std::vector<std::shared_ptr<ModuleNode>>& modules);

    std::shared_ptr<Scope> global_scope() const { return m_global_scope; }
    std::shared_ptr<Scope> get_module_scope(const std::string& module_path) const;
    std::shared_ptr<Symbol> find_symbol_for_node(const ASTNode* node) const;
    std::shared_ptr<Scope> find_scope_for_node(const ASTNode* node) const;

    std::string compute_public_signature_hash(const ModuleNode& module) const;

private:
    void declare_symbols_in_module(const std::shared_ptr<ModuleNode>& module);
    void resolve_imports(const std::shared_ptr<ModuleNode>& module);
    void resolve_members_and_types(const std::shared_ptr<ModuleNode>& module);
    void resolve_statement_and_expr_symbols(const std::shared_ptr<Statement>& stmt, const std::shared_ptr<Scope>& scope);
    void resolve_expression_symbols(const std::shared_ptr<Expression>& expr, const std::shared_ptr<Scope>& scope);

    uint64_t next_symbol_id() { return ++m_symbol_id_counter; }

    const SourceManager& m_source_manager;
    DiagnosticEngine& m_diagnostics;
    std::shared_ptr<Scope> m_global_scope;
    std::unordered_map<std::string, std::shared_ptr<Scope>> m_module_scopes;
    std::unordered_map<const ASTNode*, std::shared_ptr<Symbol>> m_node_to_symbol;
    std::unordered_map<const ASTNode*, std::shared_ptr<Scope>> m_node_to_scope;
    uint64_t m_symbol_id_counter = 0;
};

} // namespace EngineScript
