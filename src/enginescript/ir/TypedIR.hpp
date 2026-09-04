#pragma once

#include "../types/Type.hpp"
#include "../lexer/TokenType.hpp"
#include "../core/SourceLocation.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace EngineScript {

enum class IROpKind {
    Constant,
    VarRef,
    VarDecl,
    Assign,
    Binary,
    Unary,
    Conditional,
    MatchExpression,
    NumericConvert,
    StringConvert,
    InterpolatedString,
    ValueConstruct,
    ClassConstruct,
    WeakConstruct,
    ArrayLiteral,
    CollectionLiteral,
    StructLiteral,
    ThisRef,
    Closure,
    TypedEventEmit,
    TypedEventNext,
    AssetLoad,
    JobRun,
    Call,
    MethodCall,
    MemberGet,
    MemberSet,
    OptionalChain,
    IndexGet,
    IndexSet,
    Return,
    If,
    OptionalBindingIf,
    Guard,
    MatchStatement,
    While,
    For,
    ForIn,
    Break,
    Continue,
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
    // A direct mutable field access keeps an alias to the field instead of copying its value.
    bool is_reference = false;
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

struct IRConditional : public IROperation {
    std::shared_ptr<IROperation> condition;
    std::shared_ptr<IROperation> then_value;
    std::shared_ptr<IROperation> else_value;
};

struct IRMatchPattern {
    enum class Kind { Wildcard, Null, Boolean, Integer, Float, String, EnumMember };
    Kind kind = Kind::Wildcard;
    bool bool_value = false;
    int64_t int_value = 0;
    double float_value = 0.0;
    std::string string_value;
    std::string enum_name;
    std::string member_name;
};

struct IRMatchExpressionArm {
    IRMatchPattern pattern;
    std::shared_ptr<IROperation> value;
};

struct IRMatchExpression : public IROperation {
    std::shared_ptr<IROperation> subject;
    std::vector<IRMatchExpressionArm> arms;
};

struct IRNumericConvert : public IROperation {
    TypeKind target_kind = TypeKind::Error;
    std::shared_ptr<IROperation> operand;
};

struct IRStringConvert : public IROperation {
    std::shared_ptr<IROperation> operand;
};

struct IRInterpolatedStringSegment {
    std::string text;
    std::shared_ptr<IROperation> value;
    std::string format;
};

struct IRInterpolatedString : public IROperation {
    std::vector<IRInterpolatedStringSegment> segments;
};

struct IRValueConstruct : public IROperation {
    TypeKind target_kind = TypeKind::Error;
    std::vector<std::shared_ptr<IROperation>> args;
};

struct IRClassConstruct : public IROperation {
    std::string class_name;
    std::vector<std::shared_ptr<IROperation>> args;
};

struct IRWeakConstruct : public IROperation {
    std::shared_ptr<IROperation> value;
};

struct IRArrayLiteral : public IROperation {
    std::vector<std::shared_ptr<IROperation>> elements;
};

struct IRCollectionLiteral : public IROperation {
    TypeKind collection_kind = TypeKind::Error;
    std::vector<std::pair<std::shared_ptr<IROperation>, std::shared_ptr<IROperation>>> map_entries;
    std::vector<std::shared_ptr<IROperation>> elements;
};

struct IRStructLiteral : public IROperation {
    std::string struct_name;
    std::vector<std::pair<std::string, std::shared_ptr<IROperation>>> fields;
};

struct IRThisRef : public IROperation {};

struct IRClosureParam {
    std::string name;
    std::shared_ptr<Type> type;
};

struct IRClosure : public IROperation {
    std::vector<IRClosureParam> params;
    std::vector<std::shared_ptr<IROperation>> body_operations;
    std::shared_ptr<IROperation> body_expression;
};

struct IRTypedEventEmit : public IROperation {
    std::shared_ptr<IROperation> target;
    std::shared_ptr<IROperation> event;
};

struct IRTypedEventNext : public IROperation {
    std::shared_ptr<IROperation> target;
    std::string event_type;
};

struct IRAssetLoad : public IROperation {
    std::shared_ptr<IROperation> target;
    std::string asset_type;
    std::shared_ptr<IROperation> path;
};

struct IRJobRun : public IROperation {
    std::shared_ptr<IROperation> target;
    std::shared_ptr<IROperation> work;
};

struct IRCall : public IROperation {
    std::string callee_name;
    std::vector<std::shared_ptr<Type>> type_arguments;
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

struct IROptionalBindingIf : public IROperation {
    std::string name;
    std::shared_ptr<IROperation> optional_value;
    std::vector<std::shared_ptr<IROperation>> then_operations;
    std::vector<std::shared_ptr<IROperation>> else_operations;
};

struct IRGuard : public IROperation {
    std::string name;
    std::shared_ptr<IROperation> optional_value;
    std::vector<std::shared_ptr<IROperation>> else_operations;
};

struct IRMatchStatementArm {
    IRMatchPattern pattern;
    std::vector<std::shared_ptr<IROperation>> operations;
};

struct IRMatchStatement : public IROperation {
    std::shared_ptr<IROperation> subject;
    std::vector<IRMatchStatementArm> arms;
};

struct IRWhile : public IROperation {
    std::shared_ptr<IROperation> condition;
    std::vector<std::shared_ptr<IROperation>> body;
};

struct IRFor : public IROperation {
    std::shared_ptr<IROperation> initializer;
    std::shared_ptr<IROperation> condition;
    std::shared_ptr<IROperation> update;
    std::vector<std::shared_ptr<IROperation>> body;
};

struct IRForIn : public IROperation {
    std::string variable_name;
    std::shared_ptr<IROperation> collection;
    std::vector<std::shared_ptr<IROperation>> body;
};

struct IRBreak : public IROperation {};
struct IRContinue : public IROperation {};

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

struct IRGenericParam {
    std::string name;
    std::string constraint;
};

struct IRMethod {
    std::string name;
    std::shared_ptr<Type> return_type;
    std::vector<IRParam> params;
    std::vector<IRGenericParam> generic_params;
    std::vector<IRBlock> blocks;
    bool is_async = false;
    bool is_override = false;
    bool is_static = false;
    bool is_public = true;
    std::vector<std::string> decorators;
};

struct IRProperty {
    std::string name;
    std::shared_ptr<Type> type;
    std::shared_ptr<IROperation> initializer;
    std::vector<std::string> decorators;
    bool is_public = true;
    bool is_static = false;
    std::unordered_map<std::string, double> numeric_attributes; // e.g. min, max
};

struct IRClass {
    std::string name;
    std::vector<IRGenericParam> generic_params;
    std::string base_class;
    std::vector<std::string> interfaces;
    std::vector<IRParam> constructor_params;
    std::vector<IRBlock> constructor_blocks;
    std::vector<IRProperty> properties;
    std::vector<IRMethod> methods;
    bool is_behaviour = false;
    std::vector<std::string> decorators;
};

struct IRStruct {
    std::string name;
    std::vector<IRGenericParam> generic_params;
    std::vector<std::string> interfaces;
    std::vector<IRProperty> properties;
    std::vector<IRMethod> methods;
};

struct IRInterfaceMember {
    std::string name;
    std::vector<IRGenericParam> generic_params;
    std::shared_ptr<Type> type;
    std::vector<IRParam> params;
    bool is_method = false;
};

struct IRInterface {
    std::string name;
    std::vector<IRGenericParam> generic_params;
    std::vector<std::string> extended_interfaces;
    std::vector<IRInterfaceMember> members;
};

struct IREnumEntry {
    std::string name;
    std::optional<int64_t> int_value;
    std::optional<std::string> string_value;
};

struct IREnum {
    std::string name;
    bool string_backed = false;
    std::vector<IREnumEntry> entries;
};

struct IRFunction {
    std::string name;
    std::vector<IRGenericParam> generic_params;
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
    std::vector<IRInterface> interfaces;
    std::vector<IREnum> enums;
    std::vector<IRFunction> functions;
};

} // namespace EngineScript
