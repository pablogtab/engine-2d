#pragma once

#include "../core/SourceLocation.hpp"
#include "../lexer/TokenType.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace EngineScript {

enum class AccessModifier {
    None,
    Public,
    Protected,
    Private
};

struct TypeAnnotation {
    std::string name;
    std::vector<TypeAnnotation> generic_args;
    std::optional<int64_t> constant_value;
    bool is_nullable = false;
    SourceSpan span;

    std::string to_string() const {
        std::string s = constant_value ? std::to_string(*constant_value) : name;
        if (!generic_args.empty()) {
            s += "<";
            for (size_t i = 0; i < generic_args.size(); ++i) {
                s += generic_args[i].to_string();
                if (i + 1 < generic_args.size()) s += ", ";
            }
            s += ">";
        }
        if (is_nullable) s += "?";
        return s;
    }
};

struct ASTNode {
    SourceSpan span;
    virtual ~ASTNode() = default;
};

// Expressions
struct Expression : public ASTNode {};

struct LiteralExpr : public Expression {
    enum class Kind {
        Integer,
        Float,
        Duration,
        String,
        Boolean,
        Null
    };

    Kind kind = Kind::Null;
    int64_t int_val = 0;
    double float_val = 0.0;
    std::string string_val;
    std::string duration_unit;
    double duration_seconds = 0.0;
    bool bool_val = false;
};

struct InterpolatedStringSegment {
    std::string text;
    std::shared_ptr<Expression> expression;
    std::string format;
    SourceSpan span;
};

struct InterpolatedStringExpr : public Expression {
    std::vector<InterpolatedStringSegment> segments;
};

struct IdentifierExpr : public Expression {
    std::string name;
};

struct ThisExpr : public Expression {};

struct BinaryExpr : public Expression {
    TokenType op = TokenType::Invalid;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

struct UnaryExpr : public Expression {
    TokenType op = TokenType::Invalid;
    std::shared_ptr<Expression> operand;
    bool is_prefix = true;
};

struct AssignmentExpr : public Expression {
    TokenType op = TokenType::Equals; // =, +=, -=, *=, /=
    std::shared_ptr<Expression> target;
    std::shared_ptr<Expression> value;
};

struct ConditionalExpr : public Expression {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> then_value;
    std::shared_ptr<Expression> else_value;
};

struct MatchPattern {
    enum class Kind { Wildcard, Null, Boolean, Integer, Float, String, EnumMember };
    Kind kind = Kind::Wildcard;
    bool bool_value = false;
    int64_t int_value = 0;
    double float_value = 0.0;
    std::string string_value;
    std::string enum_name;
    std::string member_name;
    SourceSpan span;
};

struct MatchExpressionArm {
    MatchPattern pattern;
    std::shared_ptr<Expression> value;
};

struct MatchExpr : public Expression {
    std::shared_ptr<Expression> subject;
    std::vector<MatchExpressionArm> arms;
};

struct CallExpr : public Expression {
    std::shared_ptr<Expression> callee;
    std::vector<TypeAnnotation> type_arguments;
    std::vector<std::shared_ptr<Expression>> arguments;
    // Parallel to arguments. Empty optionals are positional arguments; names
    // are resolved and cleared by the type checker before IR lowering.
    std::vector<std::optional<std::string>> argument_names;
};

struct MemberAccessExpr : public Expression {
    std::shared_ptr<Expression> object;
    std::string member_name;
    bool is_optional = false; // true for ?.
};

struct IndexExpr : public Expression {
    std::shared_ptr<Expression> object;
    std::shared_ptr<Expression> index;
};

struct ArrayLiteralExpr : public Expression {
    std::vector<std::shared_ptr<Expression>> elements;
};

struct CollectionLiteralExpr : public Expression {
    enum class Kind { Map, Set, FixedArray };
    Kind kind = Kind::Set;
    std::vector<std::pair<std::shared_ptr<Expression>, std::shared_ptr<Expression>>> map_entries;
    std::vector<std::shared_ptr<Expression>> elements;
};

struct ObjectEntry {
    std::string key;
    std::shared_ptr<Expression> value;
    SourceSpan span;
};

struct ObjectLiteralExpr : public Expression {
    std::vector<ObjectEntry> entries;
};

struct AwaitExpr : public Expression {
    std::shared_ptr<Expression> operand;
};

struct ParamDecl;
struct Statement;

struct GenericParam {
    std::string name;
    std::optional<TypeAnnotation> constraint;
    SourceSpan span;
};

struct ArrowFunctionExpr : public Expression {
    std::vector<std::shared_ptr<ParamDecl>> params;
    std::optional<TypeAnnotation> return_type;
    std::shared_ptr<Statement> body_stmt;
    std::shared_ptr<Expression> body_expr;
};

// Decorators
struct DecoratorNode : public ASTNode {
    std::string name;
    std::vector<std::shared_ptr<Expression>> positional_args;
    std::vector<ObjectEntry> named_args;
};

// Statements
struct Statement : public ASTNode {};

struct BlockStmt : public Statement {
    std::vector<std::shared_ptr<Statement>> statements;
};

struct VarDeclStmt : public Statement {
    bool is_const = false;
    std::string name;
    std::optional<TypeAnnotation> type_annotation;
    std::shared_ptr<Expression> initializer;
};

struct ExprStmt : public Statement {
    std::shared_ptr<Expression> expr;
};

struct IfStmt : public Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Statement> then_branch;
    std::shared_ptr<Statement> else_branch;
};

struct OptionalBindingIfStmt : public Statement {
    std::string name;
    std::shared_ptr<Expression> optional_value;
    std::shared_ptr<Statement> then_branch;
    std::shared_ptr<Statement> else_branch;
};

struct GuardStmt : public Statement {
    std::string name;
    std::shared_ptr<Expression> optional_value;
    std::shared_ptr<Statement> else_branch;
};

struct MatchStatementArm {
    MatchPattern pattern;
    std::shared_ptr<Statement> body;
};

struct MatchStmt : public Statement {
    std::shared_ptr<Expression> subject;
    std::vector<MatchStatementArm> arms;
};

struct WhileStmt : public Statement {
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Statement> body;
};

struct ForStmt : public Statement {
    std::shared_ptr<Statement> initializer;
    std::shared_ptr<Expression> condition;
    std::shared_ptr<Expression> update;
    std::shared_ptr<Statement> body;
};

struct ForInStmt : public Statement {
    std::string var_name;
    std::shared_ptr<Expression> collection;
    std::shared_ptr<Statement> body;
};

struct ReturnStmt : public Statement {
    std::shared_ptr<Expression> value;
};

struct BreakStmt : public Statement {};
struct ContinueStmt : public Statement {};

// Declarations
struct Declaration : public ASTNode {
    std::vector<DecoratorNode> decorators;
    bool is_exported = false;
};

struct ImportDecl : public Declaration {
    struct ImportItem {
        std::string name;
        std::optional<std::string> alias;
        SourceSpan span;
    };

    std::vector<ImportItem> items;
    bool is_wildcard = false;
    std::string wildcard_alias;
    SourceSpan specifier_span;
    std::string module_path;
    SourceSpan module_path_span;
};

struct ParamDecl : public ASTNode {
    std::vector<DecoratorNode> decorators;
    AccessModifier access = AccessModifier::None; // For constructor parameter properties
    std::string name;
    TypeAnnotation type_annotation;
    std::shared_ptr<Expression> default_value;
};

struct PropertyDecl : public Declaration {
    AccessModifier access = AccessModifier::Public;
    bool is_override = false;
    std::string name;
    std::optional<TypeAnnotation> type_annotation;
    std::shared_ptr<Expression> initializer;
};

struct MethodDecl : public Declaration {
    AccessModifier access = AccessModifier::Public;
    bool is_override = false;
    bool is_async = false;
    std::string name;
    std::vector<GenericParam> generic_params;
    std::vector<std::shared_ptr<ParamDecl>> params;
    std::optional<TypeAnnotation> return_type;
    std::shared_ptr<BlockStmt> body;
};

struct ConstructorDecl : public Declaration {
    std::vector<std::shared_ptr<ParamDecl>> params;
    std::shared_ptr<BlockStmt> body;
};

struct ClassDecl : public Declaration {
    std::string name;
    std::vector<GenericParam> generic_params;
    std::optional<TypeAnnotation> base_class;
    std::vector<TypeAnnotation> interfaces;

    std::vector<std::shared_ptr<PropertyDecl>> properties;
    std::shared_ptr<ConstructorDecl> constructor_decl;
    std::vector<std::shared_ptr<MethodDecl>> methods;

    bool is_behaviour() const {
        for (const auto& dec : decorators) {
            if (dec.name == "behaviour") return true;
        }
        return false;
    }
};

struct StructDecl : public Declaration {
    std::string name;
    std::vector<GenericParam> generic_params;
    std::vector<TypeAnnotation> interfaces;
    std::vector<std::shared_ptr<PropertyDecl>> properties;
    std::vector<std::shared_ptr<MethodDecl>> methods;
};

struct InterfaceMember : public ASTNode {
    std::string name;
    std::vector<GenericParam> generic_params;
    std::vector<std::shared_ptr<ParamDecl>> params;
    TypeAnnotation return_type;
    bool is_method = false;
};

struct InterfaceDecl : public Declaration {
    std::string name;
    std::vector<GenericParam> generic_params;
    std::vector<TypeAnnotation> extended_interfaces;
    std::vector<InterfaceMember> members;
};

struct EnumEntry {
    std::string name;
    std::optional<int64_t> int_value;
    std::optional<std::string> string_value;
    SourceSpan span;
};

struct EnumDecl : public Declaration {
    std::string name;
    std::vector<EnumEntry> entries;
};

struct FunctionDecl : public Declaration {
    bool is_async = false;
    std::string name;
    std::vector<GenericParam> generic_params;
    std::vector<std::shared_ptr<ParamDecl>> params;
    std::optional<TypeAnnotation> return_type;
    std::shared_ptr<BlockStmt> body;
};

struct ConstDecl : public Declaration {
    std::string name;
    std::optional<TypeAnnotation> type_annotation;
    std::shared_ptr<Expression> initializer;
};

struct ModuleNode : public ASTNode {
    FileId file_id = INVALID_FILE_ID;
    std::string module_path;
    std::vector<std::shared_ptr<Declaration>> declarations;
};

} // namespace EngineScript
