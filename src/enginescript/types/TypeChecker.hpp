#pragma once

#include "Type.hpp"
#include "../ast/AST.hpp"
#include "../symbols/SymbolResolver.hpp"
#include "../core/DiagnosticEngine.hpp"
#include <unordered_map>
#include <unordered_set>
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
    const std::unordered_map<const MemberAccessExpr*, std::shared_ptr<Symbol>>& resolved_member_symbols() const {
        return m_member_symbols;
    }

private:
    void check_declaration(const std::shared_ptr<Declaration>& decl, const std::shared_ptr<Scope>& scope);
    void check_class_decl(const ClassDecl& cls, const std::shared_ptr<Scope>& scope);
    void check_struct_decl(const StructDecl& st, const std::shared_ptr<Scope>& scope);
    void check_function_decl(const FunctionDecl& fn, const std::shared_ptr<Scope>& scope);
    void check_statement(const std::shared_ptr<Statement>& stmt, const std::shared_ptr<Scope>& scope, const std::shared_ptr<Type>& expected_return, bool is_async);
    std::shared_ptr<Type> check_expression(const std::shared_ptr<Expression>& expr, const std::shared_ptr<Scope>& scope);
    std::shared_ptr<Type> check_expression_expected(
        const std::shared_ptr<Expression>& expr,
        const std::shared_ptr<Scope>& scope,
        const std::shared_ptr<Type>& expected
    );
    std::shared_ptr<Type> check_object_literal(
        const ObjectLiteralExpr& literal,
        const std::shared_ptr<Scope>& scope,
        const std::shared_ptr<Type>& expected
    );
    void check_interface_implementation(
        const std::string& owner_name,
        const std::vector<TypeAnnotation>& interfaces,
        const std::vector<std::shared_ptr<PropertyDecl>>& properties,
        const std::vector<std::shared_ptr<MethodDecl>>& methods,
        const std::shared_ptr<Scope>& scope
    );
    void validate_parameter_defaults(
        const std::vector<std::shared_ptr<ParamDecl>>& parameters,
        const std::shared_ptr<Scope>& scope,
        std::string_view callable_name
    );
    void normalize_call_arguments(
        CallExpr& call,
        const std::vector<std::string>& parameter_names,
        const std::vector<std::shared_ptr<Expression>>& default_values,
        const std::vector<bool>& optional_parameters,
        const std::shared_ptr<Scope>& scope,
        std::string_view callable_name
    );

    bool check_statement_returns(const std::shared_ptr<Statement>& stmt);

    const SymbolResolver& m_resolver;
    DiagnosticEngine& m_diagnostics;
    std::unordered_map<const ASTNode*, std::shared_ptr<Type>> m_node_types;
    std::unordered_map<const MemberAccessExpr*, std::shared_ptr<Symbol>> m_member_symbols;
    mutable std::unordered_set<std::uint64_t> m_reported_type_contracts;
    mutable std::unordered_set<std::uint64_t> m_reported_constraint_contracts;
    std::shared_ptr<Type> m_current_self_type;
    std::string m_current_class_name;
    bool m_in_static_method{false};
    std::unordered_set<std::string> m_narrowed_nonnull;
    std::size_t m_loop_depth = 0;

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
