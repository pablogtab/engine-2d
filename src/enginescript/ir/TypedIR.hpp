#pragma once

#include "../types/Type.hpp"
#include "../lexer/TokenType.hpp"
#include "../core/SourceLocation.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace EngineScript {

enum class IROpKind {
    Constant,
    VarRef,
    VarDecl,
    Assign,
    Binary,
    Unary,
    NumericConvert,
    ValueConstruct,
    Call,
    MethodCall,
    MemberGet,
    MemberSet,
    OptionalChain,
    IndexGet,
    IndexSet,
    Return,
    If,
    Branch,
    Jump,
    Await
};

struct IROperation {
    IROpKind kind = IROpKind::Constant;
    std::shared_ptr<Type> result_type;
    SourceSpan span;
    virtual ~IROperation() = default;
};

struct IRConstant : public IROperation {
    int64_t int_val = 0;
    double float_val = 0.0;
    std::string string_val;
    bool bool_val = false;
    bool is_null = false;
};

struct IRVarRef : public IROperation {
    std::string name;
};

struct IRAssign : public IROperation {
    std::string target_name;
    TokenType assignment_op = TokenType::Equals;
    std::shared_ptr<IROperation> value;
};

struct IRVarDecl : public IROperation {
    std::string name;
    std::shared_ptr<Type> declared_type;
    bool is_const = false;
    std::shared_ptr<IROperation> initializer;
};

struct IRBinary : public IROperation {
    TokenType op = TokenType::Invalid;
    std::shared_ptr<IROperation> left;
    std::shared_ptr<IROperation> right;
};

struct IRUnary : public IROperation {
    TokenType op = TokenType::Invalid;
    std::shared_ptr<IROperation> operand;
};

struct IRNumericConvert : public IROperation {
    TypeKind target_kind = TypeKind::Error;
    std::shared_ptr<IROperation> operand;
};

struct IRValueConstruct : public IROperation {
    TypeKind target_kind = TypeKind::Error;
    std::vector<std::shared_ptr<IROperation>> args;
};

struct IRCall : public IROperation {
    std::string callee_name;
    std::vector<std::shared_ptr<IROperation>> args;
};

struct IRMethodCall : public IROperation {
    std::shared_ptr<IROperation> target;
    std::string method_name;
    std::vector<std::string> type_arguments;
    std::vector<std::shared_ptr<IROperation>> args;
};

struct IRMemberGet : public IROperation {
    std::shared_ptr<IROperation> target;
    std::string member_name;
};

struct IRMemberSet : public IROperation {
    std::shared_ptr<IROperation> target;
    std::string member_name;
    TokenType assignment_op = TokenType::Equals;
    std::shared_ptr<IROperation> value;
};

struct IROptionalChain : public IROperation {
    std::shared_ptr<IROperation> target;
    std::string member_name;
    std::vector<std::shared_ptr<IROperation>> method_args;
    bool is_method_call = false;
};

struct IRIndexGet : public IROperation {
    std::shared_ptr<IROperation> target;
    std::shared_ptr<IROperation> index;
};

struct IRIndexSet : public IROperation {
    std::shared_ptr<IROperation> target;
    std::shared_ptr<IROperation> index;
    std::shared_ptr<IROperation> value;
};

struct IRReturn : public IROperation {
    std::shared_ptr<IROperation> value;
};

struct IRIf : public IROperation {
    std::shared_ptr<IROperation> condition;
    std::vector<std::shared_ptr<IROperation>> then_operations;
    std::vector<std::shared_ptr<IROperation>> else_operations;
};

struct IRAwait : public IROperation {
    std::shared_ptr<IROperation> operand;
};

struct IRStatement {
    std::shared_ptr<IROperation> op;
    SourceSpan span;
};

struct IRBlock {
    std::string label;
    std::vector<IRStatement> statements;
};

struct IRParam {
    std::string name;
    std::shared_ptr<Type> type;
    std::vector<std::string> decorators;
};

struct IRMethod {
    std::string name;
    std::shared_ptr<Type> return_type;
    std::vector<IRParam> params;
    std::vector<IRBlock> blocks;
    bool is_async = false;
    bool is_override = false;
    bool is_public = true;
    std::vector<std::string> decorators;
};

struct IRProperty {
    std::string name;
    std::shared_ptr<Type> type;
    std::shared_ptr<IROperation> initializer;
    std::vector<std::string> decorators;
    bool is_public = true;
    std::unordered_map<std::string, double> numeric_attributes; // e.g. min, max
};

struct IRClass {
    std::string name;
    std::string base_class;
    std::vector<std::string> interfaces;
    std::vector<IRParam> constructor_params;
    std::vector<IRProperty> properties;
    std::vector<IRMethod> methods;
    bool is_behaviour = false;
    std::vector<std::string> decorators;
};

struct IRStruct {
    std::string name;
    std::vector<IRProperty> properties;
    std::vector<IRMethod> methods;
};

struct IRFunction {
    std::string name;
    std::shared_ptr<Type> return_type;
    std::vector<IRParam> params;
    std::vector<IRBlock> blocks;
    bool is_async = false;
};

struct IRModule {
    std::string module_path;
    FileId file_id = INVALID_FILE_ID;
    std::vector<std::string> imports;
    std::unordered_map<std::string, std::string> import_aliases;
    std::vector<IRClass> classes;
    std::vector<IRStruct> structs;
    std::vector<IRFunction> functions;
};

} // namespace EngineScript
