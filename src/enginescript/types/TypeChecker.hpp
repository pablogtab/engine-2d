#pragma once

#include "Type.hpp"
#include "../ast/AST.hpp"
#include "../symbols/SymbolResolver.hpp"
#include "../core/DiagnosticEngine.hpp"
#include <unordered_map>
#include <memory>

namespace EngineScript {

struct InferredTypeInfo {
    SourceSpan span;
    std::string type_name;
};

class TypeChecker {
public:
    TypeChecker(const SymbolResolver& resolver, DiagnosticEngine& diagnostics);

    bool check_module(const std::shared_ptr<ModuleNode>& module);
    bool check_all(const std::vector<std::shared_ptr<ModuleNode>>& modules);

    std::shared_ptr<Type> get_type_of_node(const ASTNode* node) const;
    std::shared_ptr<Type> resolve_type_annotation(const TypeAnnotation& annot, const Scope* scope = nullptr) const;
    std::vector<InferredTypeInfo> inferred_types() const;

private:
    void check_declaration(const std::shared_ptr<Declaration>& decl, const std::shared_ptr<Scope>& scope);
    void check_class_decl(const ClassDecl& cls, const std::shared_ptr<Scope>& scope);
    void check_struct_decl(const StructDecl& st, const std::shared_ptr<Scope>& scope);
    void check_function_decl(const FunctionDecl& fn, const std::shared_ptr<Scope>& scope);
    void check_statement(const std::shared_ptr<Statement>& stmt, const std::shared_ptr<Scope>& scope, const std::shared_ptr<Type>& expected_return, bool is_async);
    std::shared_ptr<Type> check_expression(const std::shared_ptr<Expression>& expr, const std::shared_ptr<Scope>& scope);

    bool check_statement_returns(const std::shared_ptr<Statement>& stmt);

    const SymbolResolver& m_resolver;
    DiagnosticEngine& m_diagnostics;
    std::unordered_map<const ASTNode*, std::shared_ptr<Type>> m_node_types;

    // Cached primitive types
    std::shared_ptr<Type> m_type_void;
    std::shared_ptr<Type> m_type_bool;
    std::shared_ptr<Type> m_type_int;
    std::shared_ptr<Type> m_type_float;
    std::shared_ptr<Type> m_type_string;
    std::shared_ptr<Type> m_type_duration;
    std::shared_ptr<Type> m_type_null;
    std::shared_ptr<Type> m_type_entity;
    std::shared_ptr<Type> m_type_error;
};

} // namespace EngineScript
