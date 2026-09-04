#pragma once

#include "../lexer/Token.hpp"
#include "../ast/AST.hpp"
#include "../core/DiagnosticEngine.hpp"
#include <vector>
#include <memory>

namespace EngineScript {

class Parser {
public:
    Parser(FileId file_id, std::string module_path, std::vector<Token> tokens, DiagnosticEngine& diagnostics);

    std::shared_ptr<ModuleNode> parse_module();

    // Specific sub-parsers (also useful for unit testing isolated constructs)
    std::shared_ptr<Expression> parse_expression(int min_precedence = 0);
    std::shared_ptr<Statement> parse_statement();
    std::shared_ptr<Declaration> parse_declaration();
    TypeAnnotation parse_type_annotation();

private:
    const Token& peek() const;
    const Token& previous() const;
    bool is_at_end() const;
    Token advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    Token consume(TokenType type, const std::string& error_code, const std::string& message);
    void synchronize();

    // Declarations
    std::vector<DecoratorNode> parse_decorators();
    DecoratorNode parse_decorator();
    std::shared_ptr<ImportDecl> parse_import();
    std::shared_ptr<ClassDecl> parse_class(bool is_exported, std::vector<DecoratorNode> decorators);
    std::shared_ptr<StructDecl> parse_struct(bool is_exported, std::vector<DecoratorNode> decorators);
    std::shared_ptr<InterfaceDecl> parse_interface(bool is_exported, std::vector<DecoratorNode> decorators);
    std::shared_ptr<EnumDecl> parse_enum(bool is_exported, std::vector<DecoratorNode> decorators);
    std::shared_ptr<FunctionDecl> parse_function(bool is_exported, bool is_async, std::vector<DecoratorNode> decorators);
    std::shared_ptr<ConstDecl> parse_const_decl(bool is_exported);

    std::shared_ptr<ConstructorDecl> parse_constructor();
    std::shared_ptr<MethodDecl> parse_method(AccessModifier access, bool is_override, bool is_async, std::vector<DecoratorNode> decorators);
    std::shared_ptr<PropertyDecl> parse_property(AccessModifier access, bool is_override, std::vector<DecoratorNode> decorators);

    std::vector<std::shared_ptr<ParamDecl>> parse_param_list(bool allow_access_and_decorators = false);
    std::vector<GenericParam> parse_generic_param_list();

    // Statements
    std::shared_ptr<BlockStmt> parse_block();
    std::shared_ptr<VarDeclStmt> parse_var_decl();
    std::shared_ptr<Statement> parse_if();
    std::shared_ptr<GuardStmt> parse_guard();
    std::shared_ptr<MatchStmt> parse_match_statement();
    std::shared_ptr<WhileStmt> parse_while();
    std::shared_ptr<Statement> parse_for();
    std::shared_ptr<ReturnStmt> parse_return();
    std::shared_ptr<ExprStmt> parse_expr_stmt();

    // Expressions (Pratt Parsing)
    int get_token_precedence(TokenType type) const;
    std::shared_ptr<Expression> parse_primary();
    std::shared_ptr<Expression> parse_prefix();
    std::shared_ptr<Expression> parse_postfix(std::shared_ptr<Expression> expr);
    void parse_call_arguments(CallExpr& call);
    std::shared_ptr<Expression> parse_infix(std::shared_ptr<Expression> left, const Token& op);
    MatchPattern parse_match_pattern();
    std::shared_ptr<MatchExpr> parse_match_expression();
    [[nodiscard]] bool looks_like_generic_call() const;

    FileId m_file_id;
    std::string m_module_path;
    std::vector<Token> m_tokens;
    size_t m_current = 0;
    DiagnosticEngine& m_diagnostics;
};

} // namespace EngineScript
