#include "TypeChecker.hpp"
#include "../api/ApiCatalog.hpp"
#include <algorithm>
#include <functional>
#include <format>
#include <charconv>

namespace EngineScript {

namespace {

std::shared_ptr<Type> unwrap_optional_type(const std::shared_ptr<Type>& type) {
    if (!type) return Type::make_primitive(TypeKind::Error);
    if (type->kind == TypeKind::Optional && !type->type_args.empty()) {
        auto result = std::make_shared<Type>(*type->type_args.front());
        result->is_nullable = false;
        return result;
    }
    auto result = std::make_shared<Type>(*type);
    result->is_nullable = false;
    return result;
}

bool statement_terminates_flow(const std::shared_ptr<Statement>& statement) {
    if (!statement) return false;
    if (dynamic_cast<ReturnStmt*>(statement.get()) || dynamic_cast<BreakStmt*>(statement.get()) ||
        dynamic_cast<ContinueStmt*>(statement.get())) return true;
    if (const auto* block = dynamic_cast<BlockStmt*>(statement.get())) {
        return !block->statements.empty() && statement_terminates_flow(block->statements.back());
    }
    if (const auto* branch = dynamic_cast<IfStmt*>(statement.get())) {
        return branch->else_branch && statement_terminates_flow(branch->then_branch) &&
            statement_terminates_flow(branch->else_branch);
    }
    return false;
}

struct RequiredAssignmentFlow {
    bool can_continue{true};
    bool assigned{false};
};

bool assigns_field(const std::shared_ptr<Expression>& expression, std::string_view field) {
    const auto* assignment = dynamic_cast<const AssignmentExpr*>(expression.get());
    if (!assignment || assignment->op != TokenType::Equals) return false;
    if (const auto* identifier = dynamic_cast<const IdentifierExpr*>(assignment->target.get())) {
        return identifier->name == field;
    }
    if (const auto* member = dynamic_cast<const MemberAccessExpr*>(assignment->target.get())) {
        return member->member_name == field && dynamic_cast<const ThisExpr*>(member->object.get());
    }
    return false;
}

RequiredAssignmentFlow required_assignment_flow(
    const std::shared_ptr<Statement>& statement,
    std::string_view field,
    bool assigned
) {
    if (!statement) return {true, assigned};
    if (dynamic_cast<const ReturnStmt*>(statement.get())) return {false, assigned};
    if (const auto* expression = dynamic_cast<const ExprStmt*>(statement.get())) {
        return {true, assigned || assigns_field(expression->expr, field)};
    }
    if (const auto* block = dynamic_cast<const BlockStmt*>(statement.get())) {
        RequiredAssignmentFlow result{true, assigned};
        for (const auto& child : block->statements) {
            if (!result.can_continue) break;
            result = required_assignment_flow(child, field, result.assigned);
        }
        return result;
    }
    const auto merge_branches = [](const RequiredAssignmentFlow& left, const RequiredAssignmentFlow& right) {
        if (!left.can_continue) return right;
        if (!right.can_continue) return left;
        return RequiredAssignmentFlow{true, left.assigned && right.assigned};
    };
    if (const auto* branch = dynamic_cast<const IfStmt*>(statement.get())) {
        return merge_branches(
            required_assignment_flow(branch->then_branch, field, assigned),
            branch->else_branch
                ? required_assignment_flow(branch->else_branch, field, assigned)
                : RequiredAssignmentFlow{true, assigned}
        );
    }
    if (const auto* branch = dynamic_cast<const OptionalBindingIfStmt*>(statement.get())) {
        return merge_branches(
            required_assignment_flow(branch->then_branch, field, assigned),
            branch->else_branch
                ? required_assignment_flow(branch->else_branch, field, assigned)
                : RequiredAssignmentFlow{true, assigned}
        );
    }
    return {true, assigned};
}

bool validate_match_patterns(
    const std::shared_ptr<Type>& subject_type,
    const std::vector<MatchPattern>& patterns,
    const std::shared_ptr<Scope>& scope,
    DiagnosticEngine& diagnostics,
    const SourceSpan& span
) {
    if (!subject_type || subject_type->is_error()) return false;
    const std::shared_ptr<Type> domain_type = unwrap_optional_type(subject_type);
    std::unordered_set<std::string> seen;
    bool wildcard = false;
    bool seen_true = false;
    bool seen_false = false;
    bool seen_null = false;
    std::unordered_set<std::string> enum_members;

    for (const MatchPattern& pattern : patterns) {
        bool compatible = true;
        std::string key;
        switch (pattern.kind) {
            case MatchPattern::Kind::Wildcard:
                key = "_";
                wildcard = true;
                break;
            case MatchPattern::Kind::Null:
                key = "null";
                seen_null = true;
                compatible = subject_type->is_nullable || subject_type->kind == TypeKind::Optional;
                break;
            case MatchPattern::Kind::Boolean:
                key = pattern.bool_value ? "true" : "false";
                seen_true = seen_true || pattern.bool_value;
                seen_false = seen_false || !pattern.bool_value;
                compatible = domain_type->is_bool();
                break;
            case MatchPattern::Kind::Integer:
                key = "i:" + std::to_string(pattern.int_value);
                compatible = domain_type->is_integer();
                break;
            case MatchPattern::Kind::Float:
                key = "f:" + std::to_string(pattern.float_value);
                compatible = domain_type->is_float();
                break;
            case MatchPattern::Kind::String:
                key = "s:" + pattern.string_value;
                compatible = domain_type->is_string();
                break;
            case MatchPattern::Kind::EnumMember: {
                key = "e:" + pattern.enum_name + "." + pattern.member_name;
                compatible = domain_type->kind == TypeKind::Enum && domain_type->name == pattern.enum_name;
                const auto symbol = scope->lookup(pattern.enum_name);
                const auto* declaration = symbol
                    ? dynamic_cast<const EnumDecl*>(symbol->ast_node.get()) : nullptr;
                compatible = compatible && declaration && std::any_of(
                    declaration->entries.begin(), declaration->entries.end(),
                    [&pattern](const EnumEntry& entry) { return entry.name == pattern.member_name; }
                );
                enum_members.insert(pattern.member_name);
                break;
            }
        }
        if (!compatible) {
            diagnostics.report_error(
                "E3106",
                std::format("Match pattern is incompatible with '{}'", subject_type->to_string()),
                pattern.span
            );
        }
        if (!seen.emplace(key).second) {
            diagnostics.report_error("E3107", "Duplicate match pattern", pattern.span);
        }
    }

    if (wildcard) return true;
    bool exhaustive = false;
    if (domain_type->kind == TypeKind::Enum) {
        const auto symbol = scope->lookup(domain_type->name);
        const auto* declaration = symbol
            ? dynamic_cast<const EnumDecl*>(symbol->ast_node.get()) : nullptr;
        exhaustive = declaration && std::all_of(
            declaration->entries.begin(), declaration->entries.end(),
            [&enum_members](const EnumEntry& entry) { return enum_members.contains(entry.name); }
        );
    } else if (domain_type->is_bool()) {
        exhaustive = seen_true && seen_false;
    }
    if ((subject_type->is_nullable || subject_type->kind == TypeKind::Optional) && !seen_null) {
        exhaustive = false;
    }
    if (!exhaustive) {
        diagnostics.report_error(
            "E3108",
            "Match is not exhaustive; cover every closed-domain value or add '_'",
            span
        );
    }
    return exhaustive;
}

TypeAnnotation parse_type_text(std::string text) {
    const auto trim = [](std::string value) {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        const std::size_t last = value.find_last_not_of(" \t\r\n");
        return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
    };
    text = trim(std::move(text));
    TypeAnnotation annotation;
    if (!text.empty() && text.back() == '?') {
        annotation.is_nullable = true;
        text.pop_back();
        text = trim(std::move(text));
    }
    const std::size_t generic_start = text.find('<');
    if (generic_start == std::string::npos || text.back() != '>') {
        annotation.name = text;
        return annotation;
    }
    annotation.name = trim(text.substr(0, generic_start));
    const std::string arguments = text.substr(generic_start + 1, text.size() - generic_start - 2);
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t index = 0; index <= arguments.size(); ++index) {
        const bool at_end = index == arguments.size();
        if (!at_end && arguments[index] == '<') ++depth;
        else if (!at_end && arguments[index] == '>') --depth;
        if (at_end || (arguments[index] == ',' && depth == 0)) {
            std::string argument = trim(arguments.substr(start, index - start));
            TypeAnnotation parsed;
            std::int64_t constant = 0;
            const auto [pointer, error] = std::from_chars(argument.data(), argument.data() + argument.size(), constant);
            if (error == std::errc{} && pointer == argument.data() + argument.size()) parsed.constant_value = constant;
            else parsed = parse_type_text(std::move(argument));
            annotation.generic_args.push_back(std::move(parsed));
            start = index + 1;
        }
    }
    return annotation;
}

bool is_constant_default_expression(const std::shared_ptr<Expression>& expression) {
    if (!expression) return false;
    if (dynamic_cast<const LiteralExpr*>(expression.get())) return true;
    if (const auto* unary = dynamic_cast<const UnaryExpr*>(expression.get())) {
        return (unary->op == TokenType::Minus || unary->op == TokenType::Plus) &&
            dynamic_cast<const LiteralExpr*>(unary->operand.get()) != nullptr;
    }
    if (const auto* member = dynamic_cast<const MemberAccessExpr*>(expression.get())) {
        return dynamic_cast<const IdentifierExpr*>(member->object.get()) != nullptr && !member->is_optional;
    }
    if (const auto* call = dynamic_cast<const CallExpr*>(expression.get())) {
        const auto* identifier = dynamic_cast<const IdentifierExpr*>(call->callee.get());
        if (!identifier || !value_constructor_width(identifier->name) ||
            call->arguments.size() != *value_constructor_width(identifier->name)) return false;
        return std::all_of(call->arguments.begin(), call->arguments.end(), [](const auto& argument) {
            const auto* literal = dynamic_cast<const LiteralExpr*>(argument.get());
            if (literal) return literal->kind == LiteralExpr::Kind::Integer || literal->kind == LiteralExpr::Kind::Float;
            const auto* unary = dynamic_cast<const UnaryExpr*>(argument.get());
            return unary && (unary->op == TokenType::Minus || unary->op == TokenType::Plus) &&
                dynamic_cast<const LiteralExpr*>(unary->operand.get()) != nullptr;
        });
    }
    return false;
}

std::shared_ptr<Expression> catalog_default_expression(
    const ApiParameterDescriptor& parameter,
    const SourceSpan& span
) {
    if (parameter.default_value.empty()) return nullptr;
    auto literal = std::make_shared<LiteralExpr>();
    literal->span = span;
    const std::string& value = parameter.default_value;
    if (value == "null") {
        literal->kind = LiteralExpr::Kind::Null;
    } else if (value == "true" || value == "false") {
        literal->kind = LiteralExpr::Kind::Boolean;
        literal->bool_val = value == "true";
    } else if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        literal->kind = LiteralExpr::Kind::String;
        literal->string_val = value.substr(1, value.size() - 2);
    } else if (parameter.type == "Float" || parameter.type == "Double" || parameter.type == "Duration" ||
               value.find_first_of(".eE") != std::string::npos) {
        literal->kind = parameter.type == "Duration" ? LiteralExpr::Kind::Duration : LiteralExpr::Kind::Float;
        literal->float_val = std::stod(value);
        literal->duration_seconds = literal->float_val;
        literal->duration_unit = "s";
    } else {
        literal->kind = LiteralExpr::Kind::Integer;
        literal->int_val = std::stoll(value);
    }
    return literal;
}

struct ClosureCaptureScan {
    std::unordered_set<std::string> local_names;
    std::vector<const IdentifierExpr*> identifiers;
    bool captures_this{false};
};

void scan_capture_expression(const std::shared_ptr<Expression>& expression, ClosureCaptureScan& scan);

void scan_capture_statement(const std::shared_ptr<Statement>& statement, ClosureCaptureScan& scan) {
    if (!statement) return;
    if (const auto block = dynamic_cast<const BlockStmt*>(statement.get())) {
        for (const auto& nested : block->statements) scan_capture_statement(nested, scan);
    } else if (const auto declaration = dynamic_cast<const VarDeclStmt*>(statement.get())) {
        scan.local_names.insert(declaration->name);
        scan_capture_expression(declaration->initializer, scan);
    } else if (const auto expression = dynamic_cast<const ExprStmt*>(statement.get())) {
        scan_capture_expression(expression->expr, scan);
    } else if (const auto branch = dynamic_cast<const IfStmt*>(statement.get())) {
        scan_capture_expression(branch->condition, scan);
        scan_capture_statement(branch->then_branch, scan);
        scan_capture_statement(branch->else_branch, scan);
    } else if (const auto loop = dynamic_cast<const WhileStmt*>(statement.get())) {
        scan_capture_expression(loop->condition, scan);
        scan_capture_statement(loop->body, scan);
    } else if (const auto loop = dynamic_cast<const ForStmt*>(statement.get())) {
        scan_capture_statement(loop->initializer, scan);
        scan_capture_expression(loop->condition, scan);
        scan_capture_expression(loop->update, scan);
        scan_capture_statement(loop->body, scan);
    } else if (const auto loop = dynamic_cast<const ForInStmt*>(statement.get())) {
        scan.local_names.insert(loop->var_name);
        scan_capture_expression(loop->collection, scan);
        scan_capture_statement(loop->body, scan);
    } else if (const auto returned = dynamic_cast<const ReturnStmt*>(statement.get())) {
        scan_capture_expression(returned->value, scan);
    }
}

void scan_capture_expression(const std::shared_ptr<Expression>& expression, ClosureCaptureScan& scan) {
    if (!expression) return;
    if (const auto identifier = dynamic_cast<const IdentifierExpr*>(expression.get())) {
        scan.identifiers.push_back(identifier);
    } else if (dynamic_cast<const ThisExpr*>(expression.get())) {
        scan.captures_this = true;
    } else if (const auto binary = dynamic_cast<const BinaryExpr*>(expression.get())) {
        scan_capture_expression(binary->left, scan); scan_capture_expression(binary->right, scan);
    } else if (const auto unary = dynamic_cast<const UnaryExpr*>(expression.get())) {
        scan_capture_expression(unary->operand, scan);
    } else if (const auto assignment = dynamic_cast<const AssignmentExpr*>(expression.get())) {
        scan_capture_expression(assignment->target, scan); scan_capture_expression(assignment->value, scan);
    } else if (const auto call = dynamic_cast<const CallExpr*>(expression.get())) {
        scan_capture_expression(call->callee, scan);
        for (const auto& argument : call->arguments) scan_capture_expression(argument, scan);
    } else if (const auto member = dynamic_cast<const MemberAccessExpr*>(expression.get())) {
        scan_capture_expression(member->object, scan);
    } else if (const auto index = dynamic_cast<const IndexExpr*>(expression.get())) {
        scan_capture_expression(index->object, scan); scan_capture_expression(index->index, scan);
    } else if (const auto array = dynamic_cast<const ArrayLiteralExpr*>(expression.get())) {
        for (const auto& element : array->elements) scan_capture_expression(element, scan);
    } else if (const auto collection = dynamic_cast<const CollectionLiteralExpr*>(expression.get())) {
        for (const auto& [key, value] : collection->map_entries) {
            scan_capture_expression(key, scan); scan_capture_expression(value, scan);
        }
        for (const auto& element : collection->elements) scan_capture_expression(element, scan);
    } else if (const auto object = dynamic_cast<const ObjectLiteralExpr*>(expression.get())) {
        for (const auto& entry : object->entries) scan_capture_expression(entry.value, scan);
    } else if (const auto awaited = dynamic_cast<const AwaitExpr*>(expression.get())) {
        scan_capture_expression(awaited->operand, scan);
    }
}

bool is_send_type(const std::shared_ptr<Type>& type) {
    if (!type || type->is_error() || type->is_nullable) return false;
    if (type->is_numeric() || type->is_bool() || type->is_string() || type->kind == TypeKind::Enum ||
        type->kind == TypeKind::Struct || type->kind == TypeKind::Duration || type->kind == TypeKind::Vec2 ||
        type->kind == TypeKind::Vec3 || type->kind == TypeKind::Vec4 || type->kind == TypeKind::Color || type->kind == TypeKind::Bounds2) return true;
    if (type->kind == TypeKind::Array || type->kind == TypeKind::Deque || type->kind == TypeKind::FixedArray || type->kind == TypeKind::Set ||
        type->kind == TypeKind::Optional || type->kind == TypeKind::Map || type->kind == TypeKind::Result) {
        return !type->type_args.empty() && std::all_of(type->type_args.begin(), type->type_args.end(), is_send_type);
    }
    return false;
}

bool is_truthy_compatible(const std::shared_ptr<Type>& type) {
    return type && (type->is_bool() || type->is_numeric() || type->is_string() ||
        type->is_nullable || type->kind == TypeKind::Class || type->kind == TypeKind::Interface);
}

} // namespace

TypeChecker::TypeChecker(const SymbolResolver& resolver, DiagnosticEngine& diagnostics)
    : m_resolver(resolver), m_diagnostics(diagnostics) {
    m_type_void = Type::make_primitive(TypeKind::Void);
    m_type_bool = Type::make_primitive(TypeKind::Bool);
    m_type_int = Type::make_primitive(TypeKind::Int32);
    m_type_float = Type::make_primitive(TypeKind::Float32);
    m_type_string = Type::make_primitive(TypeKind::String);
    m_type_duration = Type::make_primitive(TypeKind::Duration);
    m_type_null = Type::make_primitive(TypeKind::Null);
    m_type_entity = Type::make_primitive(TypeKind::Entity);
    m_type_error = Type::make_primitive(TypeKind::Error);
}

void TypeChecker::validate_parameter_defaults(
    const std::vector<std::shared_ptr<ParamDecl>>& parameters,
    const std::shared_ptr<Scope>& scope,
    std::string_view callable_name
) {
    bool saw_default = false;
    for (const auto& parameter : parameters) {
        if (!parameter) continue;
        if (!parameter->default_value) {
            if (saw_default) {
                m_diagnostics.report_error(
                    "E3111",
                    std::format("Required parameter '{}' cannot follow a default parameter in '{}'",
                        parameter->name, callable_name),
                    parameter->span
                );
            }
            continue;
        }
        saw_default = true;
        if (!is_constant_default_expression(parameter->default_value)) {
            m_diagnostics.report_error(
                "E3112",
                std::format("Default for '{}' must be a constant, enum member, null, or constant value constructor",
                    parameter->name),
                parameter->default_value->span
            );
        }
        const auto expected = resolve_type_annotation(parameter->type_annotation, scope.get());
        const auto actual = check_expression_expected(parameter->default_value, scope, expected);
        if (actual && !actual->is_error() && !actual->is_assignable_to(expected)) {
            m_diagnostics.report_error(
                "E3113",
                std::format("Default for '{}' expects '{}', got '{}'", parameter->name,
                    expected->to_string(), actual->to_string()),
                parameter->default_value->span
            );
        }
    }
}

void TypeChecker::normalize_call_arguments(
    CallExpr& call,
    const std::vector<std::string>& parameter_names,
    const std::vector<std::shared_ptr<Expression>>& default_values,
    const std::vector<bool>& optional_parameters,
    const std::shared_ptr<Scope>& scope,
    std::string_view callable_name
) {
    if (call.argument_names.size() < call.arguments.size()) {
        call.argument_names.resize(call.arguments.size());
    }
    const bool uses_names = std::any_of(call.argument_names.begin(), call.argument_names.end(),
        [](const auto& name) { return name.has_value(); });
    const bool has_defaults = std::any_of(default_values.begin(), default_values.end(),
        [](const auto& value) { return value != nullptr; });
    const bool has_optional = std::any_of(optional_parameters.begin(), optional_parameters.end(),
        [](bool value) { return value; });
    if (!uses_names && !has_defaults && !has_optional) return;

    std::vector<std::shared_ptr<Expression>> ordered(parameter_names.size());
    std::vector<std::shared_ptr<Expression>> extras;
    std::size_t next_positional = 0;
    bool saw_named = false;
    for (std::size_t index = 0; index < call.arguments.size(); ++index) {
        const auto& name = call.argument_names[index];
        if (!name) {
            if (saw_named) {
                m_diagnostics.report_error(
                    "E3114",
                    "Positional arguments must appear before named arguments",
                    call.arguments[index]->span
                );
            }
            while (next_positional < ordered.size() && ordered[next_positional]) ++next_positional;
            if (next_positional < ordered.size()) ordered[next_positional++] = call.arguments[index];
            else extras.push_back(call.arguments[index]);
            continue;
        }

        saw_named = true;
        const auto parameter = std::find(parameter_names.begin(), parameter_names.end(), *name);
        if (parameter == parameter_names.end()) {
            m_diagnostics.report_error(
                "E3115",
                std::format("'{}' has no parameter named '{}'", callable_name, *name),
                call.arguments[index]->span
            );
            extras.push_back(call.arguments[index]);
            continue;
        }
        const std::size_t target = static_cast<std::size_t>(parameter - parameter_names.begin());
        if (ordered[target]) {
            m_diagnostics.report_error(
                "E3116",
                std::format("Argument '{}' is provided more than once", *name),
                call.arguments[index]->span
            );
            extras.push_back(call.arguments[index]);
            continue;
        }
        ordered[target] = call.arguments[index];
    }

    for (std::size_t index = 0; index < ordered.size(); ++index) {
        if (ordered[index]) continue;
        if (index < default_values.size() && default_values[index]) {
            ordered[index] = default_values[index];
            if (m_node_types.find(ordered[index].get()) == m_node_types.end()) {
                check_expression(ordered[index], scope);
            }
            continue;
        }
        const bool optional = index < optional_parameters.size() && optional_parameters[index];
        const bool later_value = std::any_of(ordered.begin() + static_cast<std::ptrdiff_t>(index + 1), ordered.end(),
            [](const auto& value) { return value != nullptr; });
        if (optional && !later_value) {
            ordered.resize(index);
            break;
        }
        m_diagnostics.report_error(
            "E3117",
            std::format("Missing required argument '{}' for '{}'", parameter_names[index], callable_name),
            call.span
        );
    }

    ordered.insert(ordered.end(), extras.begin(), extras.end());
    call.arguments = std::move(ordered);
    call.argument_names.assign(call.arguments.size(), std::nullopt);
}

std::shared_ptr<Type> TypeChecker::get_type_of_node(const ASTNode* node) const {
    auto it = m_node_types.find(node);
    if (it != m_node_types.end()) return it->second;
    return m_type_error;
}

std::vector<InferredTypeInfo> TypeChecker::inferred_types() const {
    std::vector<InferredTypeInfo> inferred;
    inferred.reserve(m_node_types.size());
    for (const auto& [node, type] : m_node_types) {
        // Callable member accesses (for example `player.getComponent`) are
        // implementation details of the enclosing CallExpr. Publishing them
        // to the LSP makes their shorter span win over the call result, so a
        // hover reports `getComponent` instead of `Transform2D?`.
        if (!node || !type || !node->span.is_valid() || type->is_error() ||
            type->kind == TypeKind::Function) continue;
        inferred.push_back({node->span, type->to_string()});
    }
    return inferred;
}

std::shared_ptr<Type> TypeChecker::resolve_type_annotation(const TypeAnnotation& annot, const Scope* scope) const {
    if (annot.span.is_valid()) {
        const std::uint64_t diagnostic_key =
            (static_cast<std::uint64_t>(annot.span.file_id) << 32U) | annot.span.start_offset;
        if (m_reported_type_contracts.emplace(diagnostic_key).second) {
            const auto require_arity = [&](std::size_t expected) {
                if (annot.generic_args.size() != expected) {
                    m_diagnostics.report_error(
                        "E3089",
                        std::format("Type '{}' expects {} type argument{}, got {}", annot.name, expected,
                            expected == 1 ? "" : "s", annot.generic_args.size()),
                        annot.span
                    );
                    return false;
                }
                return true;
            };
            if (annot.name == "FixedArray") {
                if (require_arity(2) && (!annot.generic_args[1].constant_value ||
                    *annot.generic_args[1].constant_value <= 0)) {
                    m_diagnostics.report_error(
                        "E3090",
                        "FixedArray<T, N> requires N to be a positive integer constant",
                        annot.generic_args[1].span.is_valid() ? annot.generic_args[1].span : annot.span
                    );
                }
            } else if (annot.name == "Array" || annot.name == "Deque" || annot.name == "Set" || annot.name == "Optional" ||
                       annot.name == "Weak" || annot.name == "Task" || annot.name == "ComponentRef") {
                require_arity(1);
            } else if (annot.name == "Map" || annot.name == "Result") {
                require_arity(2);
            } else if (scope) {
                if (const auto symbol = scope->lookup(annot.name)) {
                    std::size_t expected = 0;
                    bool authored_generic = false;
                    if (const auto* declaration = dynamic_cast<const ClassDecl*>(symbol->ast_node.get())) {
                        expected = declaration->generic_params.size(); authored_generic = true;
                    } else if (const auto* declaration = dynamic_cast<const StructDecl*>(symbol->ast_node.get())) {
                        expected = declaration->generic_params.size(); authored_generic = true;
                    } else if (const auto* declaration = dynamic_cast<const InterfaceDecl*>(symbol->ast_node.get())) {
                        expected = declaration->generic_params.size(); authored_generic = true;
                    }
                    if (authored_generic) require_arity(expected);
                }
            }
        }
    }
    std::shared_ptr<Type> t;
    if (annot.name == "Void") t = m_type_void;
    else if (annot.name == "Bool") t = m_type_bool;
    else if (const auto numeric_kind = numeric_type_for_name(annot.name)) {
        t = Type::make_primitive(*numeric_kind);
    }
    else if (annot.name == "String") t = m_type_string;
    else if (annot.name == "Vec2") t = Type::make_primitive(TypeKind::Vec2);
    else if (annot.name == "Vec3") t = Type::make_primitive(TypeKind::Vec3);
    else if (annot.name == "Vec4") t = Type::make_primitive(TypeKind::Vec4);
    else if (annot.name == "Color") t = Type::make_primitive(TypeKind::Color);
    else if (annot.name == "Rect") t = Type::make_primitive(TypeKind::Rect);
    else if (annot.name == "Bounds2") t = Type::make_primitive(TypeKind::Bounds2);
    else if (annot.name == "Duration") t = m_type_duration;
    else if (annot.name == "Entity") t = m_type_entity;
    else if (annot.name == "FixedArray") {
        std::vector<std::shared_ptr<Type>> args;
        if (!annot.generic_args.empty() && !annot.generic_args[0].constant_value) {
            args.push_back(resolve_type_annotation(annot.generic_args[0], scope));
        }
        t = Type::make_generic(TypeKind::FixedArray, "FixedArray", std::move(args));
        if (annot.generic_args.size() >= 2 && annot.generic_args[1].constant_value &&
            *annot.generic_args[1].constant_value > 0) {
            t->fixed_array_size = static_cast<std::size_t>(*annot.generic_args[1].constant_value);
        }
    }
    else if (annot.name == "Array") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) {
            args.push_back(resolve_type_annotation(a, scope));
        }
        t = Type::make_generic(TypeKind::Array, "Array", args);
    } else if (annot.name == "Deque") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) {
            args.push_back(resolve_type_annotation(a, scope));
        }
        t = Type::make_generic(TypeKind::Deque, "Deque", args);
    } else if (annot.name == "Map") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) {
            args.push_back(resolve_type_annotation(a, scope));
        }
        t = Type::make_generic(TypeKind::Map, "Map", args);
    } else if (annot.name == "Set") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) {
            args.push_back(resolve_type_annotation(a, scope));
        }
        t = Type::make_generic(TypeKind::Set, "Set", args);
    } else if (annot.name == "Optional") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) args.push_back(resolve_type_annotation(a, scope));
        t = Type::make_generic(TypeKind::Optional, "Optional", std::move(args));
    } else if (annot.name == "Weak") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) args.push_back(resolve_type_annotation(a, scope));
        t = Type::make_generic(TypeKind::Weak, "Weak", std::move(args));
    } else if (annot.name == "TaskScope") {
        t = Type::make_custom(TypeKind::TaskScope, "TaskScope");
    } else if (annot.name == "Result") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) {
            args.push_back(resolve_type_annotation(a, scope));
        }
        t = Type::make_generic(TypeKind::Result, "Result", args);
    } else if (annot.name == "Task") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) {
            args.push_back(resolve_type_annotation(a, scope));
        }
        t = Type::make_generic(TypeKind::Task, "Task", args);
    } else if (annot.name == "ComponentRef") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) args.push_back(resolve_type_annotation(a, scope));
        if (args.size() == 1 && args.front()->kind != TypeKind::Class) {
            m_diagnostics.report_error("E3096", "ComponentRef<T> requires a component class", annot.span);
        }
        t = Type::make_generic(TypeKind::Class, "ComponentRef", std::move(args));
    } else {
        // User class / struct / interface / enum / type parameter.
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) {
            args.push_back(resolve_type_annotation(a, scope));
        }
        TypeKind kind = TypeKind::Class;
        bool is_behaviour = false;
        std::vector<std::string> interfaces;
        if (scope) {
            if (const auto symbol = scope->lookup(annot.name)) {
                if (symbol->kind == SymbolKind::Struct || dynamic_cast<StructDecl*>(symbol->ast_node.get())) kind = TypeKind::Struct;
                else if (symbol->kind == SymbolKind::Interface || dynamic_cast<InterfaceDecl*>(symbol->ast_node.get())) kind = TypeKind::Interface;
                else if (symbol->kind == SymbolKind::Enum || dynamic_cast<EnumDecl*>(symbol->ast_node.get())) kind = TypeKind::Enum;
                else if (symbol->kind == SymbolKind::TypeParameter) kind = TypeKind::TypeParameter;
                is_behaviour = symbol->is_behaviour;
                if (const auto* cls = dynamic_cast<ClassDecl*>(symbol->ast_node.get())) {
                    for (const auto& interface_type : cls->interfaces) interfaces.push_back(interface_type.name);
                } else if (const auto* st = dynamic_cast<StructDecl*>(symbol->ast_node.get())) {
                    for (const auto& interface_type : st->interfaces) interfaces.push_back(interface_type.name);
                }
            }
        }
        t = Type::make_generic(kind, annot.name, args);
        t->is_behaviour = is_behaviour;
        t->interfaces = std::move(interfaces);
    }

    if (scope && !t->type_args.empty()) {
        if (const auto symbol = scope->lookup(annot.name)) {
            const std::vector<GenericParam>* parameters = nullptr;
            if (const auto* declaration = dynamic_cast<const ClassDecl*>(symbol->ast_node.get())) {
                parameters = &declaration->generic_params;
            } else if (const auto* declaration = dynamic_cast<const StructDecl*>(symbol->ast_node.get())) {
                parameters = &declaration->generic_params;
            } else if (const auto* declaration = dynamic_cast<const InterfaceDecl*>(symbol->ast_node.get())) {
                parameters = &declaration->generic_params;
            }
            const std::uint64_t diagnostic_key = annot.span.is_valid()
                ? ((static_cast<std::uint64_t>(annot.span.file_id) << 32U) | annot.span.start_offset)
                : 0;
            if (parameters && parameters->size() == t->type_args.size() &&
                (!annot.span.is_valid() || m_reported_constraint_contracts.emplace(diagnostic_key).second)) {
                for (std::size_t index = 0; index < parameters->size(); ++index) {
                    const auto& parameter = (*parameters)[index];
                    if (!parameter.constraint) continue;
                    const auto& concrete = t->type_args[index];
                    const std::string& constraint = parameter.constraint->name;
                    bool satisfied = std::find(
                        concrete->interfaces.begin(), concrete->interfaces.end(), constraint
                    ) != concrete->interfaces.end();
                    if (constraint == "Send") satisfied = satisfied || is_send_type(concrete);
                    if (constraint == "Equatable") {
                        satisfied = satisfied || concrete->is_numeric() || concrete->is_bool() ||
                            concrete->is_string() || concrete->kind == TypeKind::Enum;
                    }
                    if (constraint == "Ordered") {
                        satisfied = satisfied || concrete->is_numeric() || concrete->is_string() ||
                            concrete->kind == TypeKind::Enum;
                    }
                    if (!satisfied) {
                        m_diagnostics.report_error(
                            "E3078",
                            std::format("Type '{}' does not satisfy constraint '{}'", concrete->to_string(), constraint),
                            annot.span
                        );
                    }
                }
            }
        }
    }

    if (annot.is_nullable) {
        // Create a nullable copy
        auto nullable_t = std::make_shared<Type>(*t);
        nullable_t->is_nullable = true;
        return nullable_t;
    }
    return t;
}

bool TypeChecker::check_module(const std::shared_ptr<ModuleNode>& module) {
    auto scope = m_resolver.get_module_scope(module->module_path);
    if (!scope) return false;

    for (const auto& decl : module->declarations) {
        check_declaration(decl, scope);
    }

    return !m_diagnostics.has_errors();
}

bool TypeChecker::check_all(const std::vector<std::shared_ptr<ModuleNode>>& modules) {
    for (const auto& m : modules) {
        check_module(m);
    }
    return !m_diagnostics.has_errors();
}

void TypeChecker::check_declaration(const std::shared_ptr<Declaration>& decl, const std::shared_ptr<Scope>& scope) {
    if (!decl) return;

    const auto check_constraints = [&](const std::vector<GenericParam>& parameters) {
        std::unordered_set<std::string> names;
        for (const auto& parameter : parameters) {
            if (!names.emplace(parameter.name).second) {
                m_diagnostics.report_error("E3080", std::format("Duplicate generic parameter '{}'", parameter.name), parameter.span);
            }
            if (parameter.constraint) {
                const auto constraint = scope->lookup(parameter.constraint->name);
                if (!constraint || constraint->kind != SymbolKind::Interface) {
                    m_diagnostics.report_error("E3081", std::format("Generic constraint '{}' must name an interface",
                        parameter.constraint->name), parameter.constraint->span);
                }
            }
        }
    };

    if (auto cls = dynamic_cast<const ClassDecl*>(decl.get())) {
        check_constraints(cls->generic_params);
        for (const auto& method : cls->methods) check_constraints(method->generic_params);
        check_class_decl(*cls, scope);
    } else if (auto st = dynamic_cast<const StructDecl*>(decl.get())) {
        check_constraints(st->generic_params);
        for (const auto& method : st->methods) check_constraints(method->generic_params);
        check_struct_decl(*st, scope);
    } else if (auto interface_decl = dynamic_cast<const InterfaceDecl*>(decl.get())) {
        check_constraints(interface_decl->generic_params);
        for (const auto& member : interface_decl->members) check_constraints(member.generic_params);
    } else if (auto enum_decl = dynamic_cast<const EnumDecl*>(decl.get())) {
        bool has_integer = false;
        bool has_string = false;
        std::unordered_set<std::string> names;
        for (const auto& entry : enum_decl->entries) {
            has_integer = has_integer || entry.int_value.has_value();
            has_string = has_string || entry.string_value.has_value();
            if (!names.emplace(entry.name).second) {
                m_diagnostics.report_error("E3082", std::format("Duplicate enum member '{}'", entry.name), entry.span);
            }
        }
        if (has_integer && has_string) {
            m_diagnostics.report_error("E3083", std::format("Enum '{}' mixes integer and String backing values", enum_decl->name), enum_decl->span);
        }
    } else if (auto fn = dynamic_cast<const FunctionDecl*>(decl.get())) {
        check_constraints(fn->generic_params);
        check_function_decl(*fn, scope);
    } else if (auto cn = dynamic_cast<const ConstDecl*>(decl.get())) {
        if (cn->initializer) {
            auto init_t = check_expression(cn->initializer, scope);
            if (cn->type_annotation) {
                auto decl_t = resolve_type_annotation(*cn->type_annotation, scope.get());
                if (!init_t->is_assignable_to(decl_t)) {
                    m_diagnostics.report_error("E3001", std::format("Cannot assign type '{}' to const of type '{}'", init_t->to_string(), decl_t->to_string()), cn->initializer->span);
                }
                m_node_types[cn] = decl_t;
            } else {
                m_node_types[cn] = init_t;
            }
        }
    }
}

void TypeChecker::check_class_decl(const ClassDecl& cls, const std::shared_ptr<Scope>& scope) {
    auto cls_scope = m_resolver.find_scope_for_node(&cls);
    if (!cls_scope) return;
    const auto previous_self = m_current_self_type;
    const std::string previous_class_name = m_current_class_name;
    const bool previous_static_method = m_in_static_method;
    m_current_class_name = cls.name;
    TypeAnnotation self_annotation{cls.name};
    m_current_self_type = resolve_type_annotation(self_annotation, cls_scope.get());

    if (cls.constructor_decl) {
        validate_parameter_defaults(cls.constructor_decl->params, cls_scope, cls.name + " constructor");
        for (const auto& parameter : cls.constructor_decl->params) {
            m_node_types[parameter.get()] = resolve_type_annotation(parameter->type_annotation, cls_scope.get());
        }
    }

    for (const auto& prop : cls.properties) {
        if (prop->is_static && prop->initializer &&
            !dynamic_cast<const LiteralExpr*>(prop->initializer.get())) {
            m_diagnostics.report_error("E3140", "Static fields currently accept only literal or null initializers", prop->initializer->span);
        }
        if (prop->type_annotation) {
            m_node_types[prop.get()] = resolve_type_annotation(*prop->type_annotation, cls_scope.get());
        }
        if (prop->initializer) {
            auto init_t = prop->type_annotation
                ? check_expression_expected(prop->initializer, cls_scope, resolve_type_annotation(*prop->type_annotation, cls_scope.get()))
                : check_expression(prop->initializer, cls_scope);
            if (prop->type_annotation) {
                auto decl_t = resolve_type_annotation(*prop->type_annotation, cls_scope.get());
                if (!init_t->is_assignable_to(decl_t)) {
                    m_diagnostics.report_error("E3002", std::format("Property initializer type '{}' does not match declared type '{}'", init_t->to_string(), decl_t->to_string()), prop->initializer->span);
                }
                m_node_types[prop.get()] = decl_t;
            } else {
                m_node_types[prop.get()] = init_t;
            }
        }
    }

    if (cls.constructor_decl && cls.constructor_decl->body) {
        auto ctor_scope = std::make_shared<Scope>(ScopeKind::Function, cls_scope);
        const auto previous_narrowing = m_narrowed_nonnull;
        m_narrowed_nonnull.clear();
        check_statement(cls.constructor_decl->body, ctor_scope, m_type_void, false);
        m_narrowed_nonnull = previous_narrowing;
    }

    for (const auto& m : cls.methods) {
        static const std::unordered_map<std::string, std::pair<std::vector<std::string>, std::string>> hooks{
            {"on_init", {{}, "Void"}}, {"on_start", {{}, "Void"}}, {"on_destroy", {{}, "Void"}},
            {"on_button_clicked", {{}, "Void"}}, {"update", {{"Float"}, "Void"}},
            {"on_collision_enter", {{"Entity"}, "Void"}},
            {"on_collision_exit", {{"Entity"}, "Void"}}
        };
        if (cls.is_behaviour()) {
            if (const auto hook = hooks.find(m->name); hook != hooks.end()) {
                const std::string return_name = m->return_type ? m->return_type->to_string() : "Void";
                bool valid = !m->is_static && !m->is_async && return_name == hook->second.second &&
                    m->params.size() == hook->second.first.size();
                for (std::size_t index = 0; valid && index < m->params.size(); ++index) {
                    valid = m->params[index]->type_annotation.to_string() == hook->second.first[index];
                }
                if (!valid) {
                    m_diagnostics.report_error("E3141", std::format("Lifecycle hook '{}' has an invalid signature", m->name), m->span);
                }
            }
        }
        validate_parameter_defaults(m->params, cls_scope, cls.name + "." + m->name);
        if (m->body) {
            auto m_scope = std::make_shared<Scope>(ScopeKind::Function, cls_scope);
            for (const auto& generic : m->generic_params) {
                auto symbol = std::make_shared<Symbol>();
                symbol->name = generic.name;
                symbol->kind = SymbolKind::TypeParameter;
                symbol->type_string = generic.name;
                m_scope->define(symbol);
            }
            for (const auto& parameter : m->params) {
                m_node_types[parameter.get()] = resolve_type_annotation(parameter->type_annotation, m_scope.get());
            }
            std::shared_ptr<Type> ret_t = m->return_type ? resolve_type_annotation(*m->return_type, m_scope.get()) : m_type_void;
            m_node_types[m.get()] = ret_t;
            const auto previous_narrowing = m_narrowed_nonnull;
            m_in_static_method = m->is_static;
            m_narrowed_nonnull.clear();
            check_statement(m->body, m_scope, ret_t, m->is_async);
            m_in_static_method = previous_static_method;
            m_narrowed_nonnull = previous_narrowing;

            if (!ret_t->is_void() && !m->is_async) {
                if (!check_statement_returns(m->body)) {
                    m_diagnostics.report_error("E3003", std::format("Method '{}' does not return a value on all code paths", m->name), m->span);
                }
            }
        }
    }
    if (cls.is_behaviour()) {
        const auto find_hook = [&cls](std::string_view name) -> const MethodDecl* {
            const auto found = std::find_if(cls.methods.begin(), cls.methods.end(), [name](const auto& method) {
                return method && method->name == name;
            });
            return found == cls.methods.end() ? nullptr : found->get();
        };
        const MethodDecl* init_hook = find_hook("on_init");
        const MethodDecl* start_hook = find_hook("on_start");
        for (const auto& property : cls.properties) {
            const auto type = get_type_of_node(property.get());
            if (!type || property->is_static || property->initializer || type->is_nullable) continue;
            const bool authored_behaviour = type->kind == TypeKind::Class && type->is_behaviour;
            const bool native_component = type->name == "Transform2D" || type->name == "RigidBody2D" ||
                type->name == "SpriteRenderer2D" || type->name == "Animator2D" ||
                type->name == "ParticleEmitter2D" || type->name == "Joint2D";
            if (!authored_behaviour && !native_component) continue;
            bool assigned = init_hook && init_hook->body &&
                required_assignment_flow(init_hook->body, property->name, false).assigned;
            if (!assigned && start_hook && start_hook->body) {
                assigned = required_assignment_flow(start_hook->body, property->name, false).assigned;
            }
            if (!assigned) {
                m_diagnostics.report_error(
                    "E3149",
                    std::format("Required reference '{}' must be assigned on every normal path of on_init or on_start", property->name),
                    property->span
                );
            }
        }
    }
    check_interface_implementation(cls.name, cls.interfaces, cls.properties, cls.methods, cls_scope);
    m_current_self_type = previous_self;
    m_current_class_name = previous_class_name;
    m_in_static_method = previous_static_method;
}

void TypeChecker::check_struct_decl(const StructDecl& st, const std::shared_ptr<Scope>& scope) {
    auto st_scope = m_resolver.find_scope_for_node(&st);
    if (!st_scope) return;
    const auto previous_self = m_current_self_type;
    TypeAnnotation self_annotation{st.name};
    m_current_self_type = resolve_type_annotation(self_annotation, st_scope.get());

    for (const auto& prop : st.properties) {
        if (prop->type_annotation) {
            m_node_types[prop.get()] = resolve_type_annotation(*prop->type_annotation, st_scope.get());
        }
        if (prop->initializer) {
            auto init_t = prop->type_annotation
                ? check_expression_expected(prop->initializer, st_scope, resolve_type_annotation(*prop->type_annotation, st_scope.get()))
                : check_expression(prop->initializer, st_scope);
            if (prop->type_annotation) {
                auto decl_t = resolve_type_annotation(*prop->type_annotation, st_scope.get());
                if (!init_t->is_assignable_to(decl_t)) {
                    m_diagnostics.report_error("E3002", std::format("Property initializer type '{}' does not match declared type '{}'", init_t->to_string(), decl_t->to_string()), prop->initializer->span);
                }
            }
        }
    }
    for (const auto& method : st.methods) {
        validate_parameter_defaults(method->params, st_scope, st.name + "." + method->name);
        auto method_scope = std::make_shared<Scope>(ScopeKind::Function, st_scope);
        for (const auto& generic : method->generic_params) {
            auto symbol = std::make_shared<Symbol>();
            symbol->name = generic.name;
            symbol->kind = SymbolKind::TypeParameter;
            symbol->type_string = generic.name;
            method_scope->define(symbol);
        }
        for (const auto& parameter : method->params) {
            m_node_types[parameter.get()] = resolve_type_annotation(parameter->type_annotation, method_scope.get());
        }
        const auto return_type = method->return_type
            ? resolve_type_annotation(*method->return_type, method_scope.get()) : m_type_void;
        m_node_types[method.get()] = return_type;
        const auto previous_narrowing = m_narrowed_nonnull;
        m_narrowed_nonnull.clear();
        check_statement(method->body, method_scope, return_type, method->is_async);
        m_narrowed_nonnull = previous_narrowing;
        if (!return_type->is_void() && !method->is_async && !check_statement_returns(method->body)) {
            m_diagnostics.report_error("E3003", std::format("Method '{}' does not return a value on all code paths", method->name), method->span);
        }
    }
    check_interface_implementation(st.name, st.interfaces, st.properties, st.methods, st_scope);
    m_current_self_type = previous_self;
}

void TypeChecker::check_function_decl(const FunctionDecl& fn, const std::shared_ptr<Scope>& scope) {
    auto fn_scope = std::make_shared<Scope>(ScopeKind::Function, scope);
    validate_parameter_defaults(fn.params, fn_scope, fn.name);
    if (fn.body) {
        for (const auto& generic : fn.generic_params) {
            auto symbol = std::make_shared<Symbol>();
            symbol->name = generic.name;
            symbol->kind = SymbolKind::TypeParameter;
            symbol->type_string = generic.name;
            fn_scope->define(symbol);
        }
        for (const auto& parameter : fn.params) {
            m_node_types[parameter.get()] = resolve_type_annotation(parameter->type_annotation, fn_scope.get());
        }
        std::shared_ptr<Type> ret_t = fn.return_type ? resolve_type_annotation(*fn.return_type, fn_scope.get()) : m_type_void;
        m_node_types[&fn] = ret_t;
        const auto previous_narrowing = m_narrowed_nonnull;
        m_narrowed_nonnull.clear();
        check_statement(fn.body, fn_scope, ret_t, fn.is_async);
        m_narrowed_nonnull = previous_narrowing;

        if (!ret_t->is_void() && !fn.is_async) {
            if (!check_statement_returns(fn.body)) {
                m_diagnostics.report_error("E3003", std::format("Function '{}' does not return a value on all code paths", fn.name), fn.span);
            }
        }
    }
}

bool TypeChecker::check_statement_returns(const std::shared_ptr<Statement>& stmt) {
    if (!stmt) return false;

    if (dynamic_cast<ReturnStmt*>(stmt.get())) {
        return true;
    }
    if (auto blk = dynamic_cast<BlockStmt*>(stmt.get())) {
        for (const auto& s : blk->statements) {
            if (check_statement_returns(s)) return true;
        }
        return false;
    }
    if (auto if_s = dynamic_cast<IfStmt*>(stmt.get())) {
        if (if_s->else_branch && check_statement_returns(if_s->then_branch) && check_statement_returns(if_s->else_branch)) {
            return true;
        }
    }
    if (auto binding = dynamic_cast<OptionalBindingIfStmt*>(stmt.get())) {
        return binding->else_branch && check_statement_returns(binding->then_branch) &&
            check_statement_returns(binding->else_branch);
    }
    if (auto match_statement = dynamic_cast<MatchStmt*>(stmt.get())) {
        return !match_statement->arms.empty() && std::all_of(
            match_statement->arms.begin(), match_statement->arms.end(),
            [this](const MatchStatementArm& arm) { return check_statement_returns(arm.body); }
        );
    }
    return false;
}

void TypeChecker::check_statement(const std::shared_ptr<Statement>& stmt, const std::shared_ptr<Scope>& scope, const std::shared_ptr<Type>& expected_return, bool is_async) {
    if (!stmt) return;

    if (auto blk = dynamic_cast<BlockStmt*>(stmt.get())) {
        auto blk_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        bool terminated = false;
        for (const auto& s : blk->statements) {
            if (terminated) {
                m_diagnostics.report_warning("W3001", "Unreachable statement", s->span);
            }
            check_statement(s, blk_scope, expected_return, is_async);
            terminated = terminated || dynamic_cast<ReturnStmt*>(s.get()) ||
                dynamic_cast<BreakStmt*>(s.get()) || dynamic_cast<ContinueStmt*>(s.get()) ||
                check_statement_returns(s);
        }
    } else if (auto v = dynamic_cast<VarDeclStmt*>(stmt.get())) {
        if (v->type_annotation) {
            m_node_types[v] = resolve_type_annotation(*v->type_annotation, scope.get());
        }
        if (v->initializer) {
            auto init_t = v->type_annotation
                ? check_expression_expected(v->initializer, scope, resolve_type_annotation(*v->type_annotation, scope.get()))
                : check_expression(v->initializer, scope);
            if (v->type_annotation) {
                auto decl_t = resolve_type_annotation(*v->type_annotation, scope.get());
                if (!init_t->is_assignable_to(decl_t)) {
                    m_diagnostics.report_error("E3010", std::format("Cannot initialize variable '{}' of type '{}' with value of type '{}'", v->name, decl_t->to_string(), init_t->to_string()), v->initializer->span);
                }
                m_node_types[v] = decl_t;
            } else {
                m_node_types[v] = init_t;
                // The resolver creates local symbols before type checking, so
                // an unannotated local initially carries `<inferred>`. Persist
                // its resolved initializer type for later expressions and flow
                // narrowing in the same scope.
                if (!init_t->is_error()) {
                    if (const auto symbol = m_resolver.find_symbol_for_node(v)) {
                        symbol->type_string = init_t->to_string();
                    }
                }
            }
        } else {
            m_diagnostics.report_error("E3017", std::format("Variable '{}' must be initialized", v->name), v->span);
        }
    } else if (auto e = dynamic_cast<ExprStmt*>(stmt.get())) {
        check_expression(e->expr, scope);
    } else if (auto if_s = dynamic_cast<IfStmt*>(stmt.get())) {
        auto cond_t = check_expression(if_s->condition, scope);
        if (!is_truthy_compatible(cond_t) && !cond_t->is_error()) {
            m_diagnostics.report_error("E3011", std::format("If condition must be truthy-compatible, got '{}'", cond_t->to_string()), if_s->condition->span);
        }
        const auto previous_narrowing = m_narrowed_nonnull;
        const auto* condition = dynamic_cast<const BinaryExpr*>(if_s->condition.get());
        const IdentifierExpr* nullable_identifier = nullptr;
        bool true_branch_nonnull = false;
        if (condition && (condition->op == TokenType::EqualsEquals || condition->op == TokenType::ExclamationEquals)) {
            if (dynamic_cast<const LiteralExpr*>(condition->right.get()) &&
                dynamic_cast<const LiteralExpr*>(condition->right.get())->kind == LiteralExpr::Kind::Null) {
                nullable_identifier = dynamic_cast<const IdentifierExpr*>(condition->left.get());
            } else if (dynamic_cast<const LiteralExpr*>(condition->left.get()) &&
                dynamic_cast<const LiteralExpr*>(condition->left.get())->kind == LiteralExpr::Kind::Null) {
                nullable_identifier = dynamic_cast<const IdentifierExpr*>(condition->right.get());
            }
            true_branch_nonnull = condition->op == TokenType::ExclamationEquals;
        } else if (const auto* unary = dynamic_cast<const UnaryExpr*>(if_s->condition.get());
                   unary && unary->op == TokenType::Exclamation) {
            nullable_identifier = dynamic_cast<const IdentifierExpr*>(unary->operand.get());
        } else {
            nullable_identifier = dynamic_cast<const IdentifierExpr*>(if_s->condition.get());
            true_branch_nonnull = nullable_identifier != nullptr;
        }
        if (nullable_identifier && !get_type_of_node(nullable_identifier)->is_nullable) {
            nullable_identifier = nullptr;
        }
        if (nullable_identifier && true_branch_nonnull) m_narrowed_nonnull.insert(nullable_identifier->name);
        check_statement(if_s->then_branch, scope, expected_return, is_async);
        m_narrowed_nonnull = previous_narrowing;
        if (if_s->else_branch) {
            if (nullable_identifier && !true_branch_nonnull) m_narrowed_nonnull.insert(nullable_identifier->name);
            check_statement(if_s->else_branch, scope, expected_return, is_async);
            m_narrowed_nonnull = previous_narrowing;
        }
        if (nullable_identifier && !true_branch_nonnull && !if_s->else_branch &&
            check_statement_returns(if_s->then_branch)) {
            m_narrowed_nonnull.insert(nullable_identifier->name);
        }
    } else if (auto binding = dynamic_cast<OptionalBindingIfStmt*>(stmt.get())) {
        const auto optional_type = check_expression(binding->optional_value, scope);
        if (optional_type->kind != TypeKind::Optional && !optional_type->is_nullable && !optional_type->is_error()) {
            m_diagnostics.report_error(
                "E3102",
                std::format("Optional binding requires a nullable value, got '{}'", optional_type->to_string()),
                binding->optional_value->span
            );
        }
        const auto value_type = unwrap_optional_type(optional_type);
        if (const auto symbol = m_resolver.find_symbol_for_node(binding)) {
            symbol->type_string = value_type->to_string();
        }
        auto then_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        auto local = std::make_shared<Symbol>();
        local->name = binding->name;
        local->kind = SymbolKind::Variable;
        local->type_string = value_type->to_string();
        then_scope->define(local);
        check_statement(binding->then_branch, then_scope, expected_return, is_async);
        if (binding->else_branch) check_statement(binding->else_branch, scope, expected_return, is_async);
    } else if (auto guard = dynamic_cast<GuardStmt*>(stmt.get())) {
        const auto optional_type = check_expression(guard->optional_value, scope);
        if (optional_type->kind != TypeKind::Optional && !optional_type->is_nullable && !optional_type->is_error()) {
            m_diagnostics.report_error(
                "E3102",
                std::format("Guard binding requires a nullable value, got '{}'", optional_type->to_string()),
                guard->optional_value->span
            );
        }
        if (!statement_terminates_flow(guard->else_branch)) {
            m_diagnostics.report_error(
                "E3103",
                "Guard else branch must end with return, break, or continue",
                guard->else_branch ? guard->else_branch->span : guard->span
            );
        }
        check_statement(guard->else_branch, scope, expected_return, is_async);
        const auto value_type = unwrap_optional_type(optional_type);
        if (const auto symbol = m_resolver.find_symbol_for_node(guard)) {
            symbol->type_string = value_type->to_string();
        }
        auto local = std::make_shared<Symbol>();
        local->name = guard->name;
        local->kind = SymbolKind::Variable;
        local->type_string = value_type->to_string();
        scope->define(local);
    } else if (auto match_statement = dynamic_cast<MatchStmt*>(stmt.get())) {
        const auto subject_type = check_expression(match_statement->subject, scope);
        std::vector<MatchPattern> patterns;
        patterns.reserve(match_statement->arms.size());
        for (const auto& arm : match_statement->arms) {
            patterns.push_back(arm.pattern);
            check_statement(arm.body, scope, expected_return, is_async);
        }
        validate_match_patterns(subject_type, patterns, scope, m_diagnostics, match_statement->span);
    } else if (auto w = dynamic_cast<WhileStmt*>(stmt.get())) {
        auto cond_t = check_expression(w->condition, scope);
        if (!is_truthy_compatible(cond_t) && !cond_t->is_error()) {
            m_diagnostics.report_error("E3012", std::format("While condition must be truthy-compatible, got '{}'", cond_t->to_string()), w->condition->span);
        }
        ++m_loop_depth;
        check_statement(w->body, scope, expected_return, is_async);
        --m_loop_depth;
    } else if (auto f = dynamic_cast<ForStmt*>(stmt.get())) {
        auto for_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        check_statement(f->initializer, for_scope, expected_return, is_async);
        if (f->condition) {
            auto cond_t = check_expression(f->condition, for_scope);
            if (!is_truthy_compatible(cond_t) && !cond_t->is_error()) {
                m_diagnostics.report_error("E3013", std::format("For condition must be truthy-compatible, got '{}'", cond_t->to_string()), f->condition->span);
            }
        }
        if (f->update) check_expression(f->update, for_scope);
        ++m_loop_depth;
        check_statement(f->body, for_scope, expected_return, is_async);
        --m_loop_depth;
    } else if (auto for_in = dynamic_cast<ForInStmt*>(stmt.get())) {
        auto for_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        auto coll_t = check_expression(for_in->collection, for_scope);
        if (coll_t->kind != TypeKind::Array && coll_t->kind != TypeKind::Deque && coll_t->kind != TypeKind::FixedArray &&
            coll_t->kind != TypeKind::Map && coll_t->kind != TypeKind::Set && !coll_t->is_error()) {
            m_diagnostics.report_error("E3014", std::format("Type '{}' is not iterable in for-in", coll_t->to_string()), for_in->collection->span);
        }
        ++m_loop_depth;
        check_statement(for_in->body, for_scope, expected_return, is_async);
        --m_loop_depth;
    } else if (auto ret = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (ret->value) {
            std::shared_ptr<Type> target_return = expected_return;
            if (is_async && expected_return && expected_return->kind == TypeKind::Task &&
                !expected_return->type_args.empty()) {
                target_return = expected_return->type_args[0];
            }
            auto val_t = target_return
                ? check_expression_expected(ret->value, scope, target_return)
                : check_expression(ret->value, scope);
            if (expected_return) {
                if (!val_t->is_assignable_to(target_return)) {
                    m_diagnostics.report_error("E3015", std::format("Cannot return value of type '{}', expected '{}'", val_t->to_string(), target_return->to_string()), ret->value->span);
                }
            }
        } else {
            if (expected_return && !expected_return->is_void()) {
                if (is_async && expected_return->kind == TypeKind::Task && !expected_return->type_args.empty() && expected_return->type_args[0]->is_void()) {
                    // Task<Void> returning nothing is fine
                } else {
                    m_diagnostics.report_error("E3016", std::format("Return statement must return a value of type '{}'", expected_return->to_string()), ret->span);
                }
            }
        }
    } else if (dynamic_cast<BreakStmt*>(stmt.get()) || dynamic_cast<ContinueStmt*>(stmt.get())) {
        if (m_loop_depth == 0) {
            m_diagnostics.report_error("E3019", dynamic_cast<BreakStmt*>(stmt.get())
                ? "'break' is only valid inside a loop"
                : "'continue' is only valid inside a loop", stmt->span);
        }
    }
}

std::shared_ptr<Type> TypeChecker::check_expression_expected(
    const std::shared_ptr<Expression>& expr,
    const std::shared_ptr<Scope>& scope,
    const std::shared_ptr<Type>& expected
) {
    if (const auto* literal = dynamic_cast<const LiteralExpr*>(expr.get());
        literal && literal->kind == LiteralExpr::Kind::Null && expected && expected->is_nullable) {
        m_node_types[literal] = expected;
        return expected;
    }
    if (const auto* object = dynamic_cast<const ObjectLiteralExpr*>(expr.get())) {
        return check_object_literal(*object, scope, expected);
    }
    if (const auto* array = dynamic_cast<const ArrayLiteralExpr*>(expr.get());
        array && expected && expected->kind == TypeKind::Array && expected->type_args.size() == 1) {
        for (const auto& element : array->elements) {
            const auto element_type = check_expression_expected(element, scope, expected->type_args[0]);
            if (!element_type->is_assignable_to(expected->type_args[0])) {
                m_diagnostics.report_error("E3072", std::format("Array element type '{}' is not assignable to '{}'",
                    element_type->to_string(), expected->type_args[0]->to_string()), element->span);
            }
        }
        m_node_types[array] = expected;
        return expected;
    }
    if (const auto* collection = dynamic_cast<const CollectionLiteralExpr*>(expr.get()); collection && expected) {
        const auto actual = check_expression(expr, scope);
        if (!actual->is_assignable_to(expected)) {
            m_diagnostics.report_error("E3073", std::format("Collection literal of type '{}' is not assignable to '{}'",
                actual->to_string(), expected->to_string()), expr->span);
        }
        m_node_types[collection] = expected;
        return expected;
    }
    if (dynamic_cast<const CallExpr*>(expr.get()) && expected &&
        (expected->kind == TypeKind::Class || expected->kind == TypeKind::Struct) && !expected->type_args.empty()) {
        const auto actual = check_expression(expr, scope);
        if (actual->kind == expected->kind && actual->name == expected->name && actual->type_args.empty()) {
            m_node_types[expr.get()] = expected;
            return expected;
        }
        return actual;
    }
    return check_expression(expr, scope);
}

std::shared_ptr<Type> TypeChecker::check_object_literal(
    const ObjectLiteralExpr& literal,
    const std::shared_ptr<Scope>& scope,
    const std::shared_ptr<Type>& expected
) {
    const ApiTypeDescriptor* api_value = expected && expected->kind == TypeKind::Class
        ? ApiCatalog::instance().find_available_type(expected->name) : nullptr;
    if (api_value && api_value->value_type) {
        std::unordered_set<std::string> present;
        for (const auto& entry : literal.entries) {
            if (!present.emplace(entry.key).second) {
                m_diagnostics.report_error("E3071", std::format("Duplicate value field '{}'", entry.key), entry.span);
                continue;
            }
            const ApiPropertyDescriptor* property = ApiCatalog::instance().find_available_property(expected->name, entry.key);
            if (!property || !property->writable) {
                m_diagnostics.report_error("E3071", std::format("Value type '{}' has no writable field '{}'", expected->name, entry.key), entry.span);
                check_expression(entry.value, scope);
                continue;
            }
            const auto field_type = resolve_type_annotation(parse_type_text(property->type), scope.get());
            const auto value_type = check_expression_expected(entry.value, scope, field_type);
            if (!value_type->is_assignable_to(field_type)) {
                m_diagnostics.report_error("E3071", std::format("Field '{}' expects '{}', got '{}'", entry.key,
                    field_type->to_string(), value_type->to_string()), entry.value->span);
            }
        }
        m_node_types[&literal] = expected;
        return expected;
    }
    if (!expected || expected->kind != TypeKind::Struct) {
        m_diagnostics.report_error("E3070", "Object literals require a contextually known struct type", literal.span);
        m_node_types[&literal] = m_type_error;
        for (const auto& entry : literal.entries) check_expression(entry.value, scope);
        return m_type_error;
    }

    const auto symbol = scope->lookup(expected->name);
    const auto* declaration = symbol ? dynamic_cast<const StructDecl*>(symbol->ast_node.get()) : nullptr;
    if (!declaration) {
        m_diagnostics.report_error("E3070", std::format("'{}' is not an authored struct", expected->name), literal.span);
        m_node_types[&literal] = m_type_error;
        return m_type_error;
    }

    std::unordered_map<std::string, std::shared_ptr<Type>> struct_bindings;
    for (std::size_t index = 0;
         index < declaration->generic_params.size() && index < expected->type_args.size(); ++index) {
        struct_bindings[declaration->generic_params[index].name] = expected->type_args[index];
    }
    std::function<std::shared_ptr<Type>(const TypeAnnotation&)> resolve_struct_type =
        [&](const TypeAnnotation& annotation) {
            if (const auto bound = struct_bindings.find(annotation.name);
                bound != struct_bindings.end() && annotation.generic_args.empty()) {
                auto result = std::make_shared<Type>(*bound->second);
                result->is_nullable = annotation.is_nullable;
                return result;
            }
            auto result = resolve_type_annotation(annotation, scope.get());
            if (!annotation.generic_args.empty()) {
                result = std::make_shared<Type>(*result);
                result->type_args.clear();
                for (const auto& argument : annotation.generic_args) {
                    if (!argument.constant_value) result->type_args.push_back(resolve_struct_type(argument));
                }
            }
            return result;
        };

    std::unordered_set<std::string> present;
    for (const auto& entry : literal.entries) {
        if (!present.emplace(entry.key).second) {
            m_diagnostics.report_error("E3071", std::format("Duplicate struct field '{}'", entry.key), entry.span);
            continue;
        }
        const auto property = std::find_if(declaration->properties.begin(), declaration->properties.end(),
            [&entry](const auto& candidate) { return candidate->name == entry.key; });
        if (property == declaration->properties.end()) {
            m_diagnostics.report_error("E3071", std::format("Struct '{}' has no field '{}'", expected->name, entry.key), entry.span);
            check_expression(entry.value, scope);
            continue;
        }
        const auto field_type = (*property)->type_annotation
            ? resolve_struct_type(*(*property)->type_annotation)
            : get_type_of_node(property->get());
        const auto value_type = check_expression_expected(entry.value, scope, field_type);
        if (!value_type->is_assignable_to(field_type)) {
            m_diagnostics.report_error("E3071", std::format("Field '{}' expects '{}', got '{}'", entry.key,
                field_type->to_string(), value_type->to_string()), entry.value->span);
        }
    }
    for (const auto& property : declaration->properties) {
        if (!property->initializer && !present.contains(property->name)) {
            m_diagnostics.report_error("E3071", std::format("Struct literal is missing required field '{}'", property->name), literal.span);
        }
    }
    m_node_types[&literal] = expected;
    return expected;
}

void TypeChecker::check_interface_implementation(
    const std::string& owner_name,
    const std::vector<TypeAnnotation>& interfaces,
    const std::vector<std::shared_ptr<PropertyDecl>>& properties,
    const std::vector<std::shared_ptr<MethodDecl>>& methods,
    const std::shared_ptr<Scope>& scope
) {
    for (const auto& interface_type : interfaces) {
        const auto symbol = scope->lookup(interface_type.name);
        const auto* declaration = symbol ? dynamic_cast<const InterfaceDecl*>(symbol->ast_node.get()) : nullptr;
        if (!declaration) continue;
        for (const auto& required : declaration->members) {
            if (required.is_method) {
                const auto implementation = std::find_if(methods.begin(), methods.end(),
                    [&required](const auto& candidate) { return candidate->name == required.name; });
                if (implementation == methods.end() || (*implementation)->params.size() != required.params.size() ||
                    !(*implementation)->return_type ||
                    (*implementation)->return_type->to_string() != required.return_type.to_string()) {
                    m_diagnostics.report_error("E3074", std::format("'{}' does not implement method '{}.{}'",
                        owner_name, interface_type.name, required.name), interface_type.span);
                }
            } else {
                const auto implementation = std::find_if(properties.begin(), properties.end(),
                    [&required](const auto& candidate) { return candidate->name == required.name; });
                if (implementation == properties.end() || !(*implementation)->type_annotation ||
                    (*implementation)->type_annotation->to_string() != required.return_type.to_string()) {
                    m_diagnostics.report_error("E3074", std::format("'{}' does not implement property '{}.{}'",
                        owner_name, interface_type.name, required.name), interface_type.span);
                }
            }
        }
    }
}

std::shared_ptr<Type> TypeChecker::check_expression(const std::shared_ptr<Expression>& expr, const std::shared_ptr<Scope>& scope) {
    if (!expr) return m_type_error;

    if (auto lit = dynamic_cast<LiteralExpr*>(expr.get())) {
        std::shared_ptr<Type> t;
        switch (lit->kind) {
            case LiteralExpr::Kind::Integer:  t = m_type_int; break;
            case LiteralExpr::Kind::Float:    t = m_type_float; break;
            case LiteralExpr::Kind::Duration: t = m_type_duration; break;
            case LiteralExpr::Kind::String:   t = m_type_string; break;
            case LiteralExpr::Kind::Boolean:  t = m_type_bool; break;
            case LiteralExpr::Kind::Null:     t = m_type_null; break;
        }
        m_node_types[lit] = t;
        return t;
    }

    if (auto interpolated = dynamic_cast<InterpolatedStringExpr*>(expr.get())) {
        const auto has_authored_to_string = [&scope](const std::shared_ptr<Type>& type) {
            if (!type || type->name.empty()) return false;
            const auto symbol = scope->lookup(type->name);
            if (!symbol) return false;

            const auto valid_method = [](const std::shared_ptr<MethodDecl>& method) {
                return method && method->name == "toString" &&
                    method->access == AccessModifier::Public && !method->is_async &&
                    method->params.empty() && method->return_type &&
                    method->return_type->name == "String" && !method->return_type->is_nullable;
            };
            if (const auto* declaration = dynamic_cast<const ClassDecl*>(symbol->ast_node.get())) {
                return std::any_of(declaration->methods.begin(), declaration->methods.end(), valid_method);
            }
            if (const auto* declaration = dynamic_cast<const StructDecl*>(symbol->ast_node.get())) {
                return std::any_of(declaration->methods.begin(), declaration->methods.end(), valid_method);
            }
            if (const auto* declaration = dynamic_cast<const InterfaceDecl*>(symbol->ast_node.get())) {
                return std::any_of(
                    declaration->members.begin(),
                    declaration->members.end(),
                    [](const InterfaceMember& member) {
                        return member.is_method && member.name == "toString" &&
                            member.params.empty() && member.return_type.name == "String" &&
                            !member.return_type.is_nullable;
                    }
                );
            }
            return false;
        };

        for (const auto& segment : interpolated->segments) {
            if (!segment.expression) continue;
            const auto value_type = check_expression(segment.expression, scope);
            const bool built_in = value_type && (
                value_type->is_string() || value_type->is_numeric() || value_type->is_bool() ||
                value_type->kind == TypeKind::Duration || value_type->kind == TypeKind::Enum ||
                value_type->kind == TypeKind::Vec2 || value_type->kind == TypeKind::Vec3 ||
                value_type->kind == TypeKind::Vec4 || value_type->kind == TypeKind::Color ||
                value_type->kind == TypeKind::Rect || value_type->kind == TypeKind::Bounds2
            );
            if (!built_in && !has_authored_to_string(value_type) && value_type && !value_type->is_error()) {
                m_diagnostics.report_error(
                    "E3100",
                    std::format(
                        "Type '{}' cannot be interpolated; add public toString(): String",
                        value_type->to_string()
                    ),
                    segment.span
                );
            }

            if (!segment.format.empty()) {
                bool valid_format = value_type && value_type->is_numeric();
                if (valid_format && segment.format.front() == '.') {
                    valid_format = segment.format.size() >= 3 && segment.format.back() == 'f';
                    for (std::size_t index = 1; valid_format && index + 1 < segment.format.size(); ++index) {
                        valid_format = std::isdigit(static_cast<unsigned char>(segment.format[index]));
                    }
                } else if (valid_format) {
                    for (const char character : segment.format) {
                        if (!std::isdigit(static_cast<unsigned char>(character))) {
                            valid_format = false;
                            break;
                        }
                    }
                }
                if (!valid_format) {
                    m_diagnostics.report_error(
                        "E3101",
                        std::format("Unsupported interpolation format '{}'", segment.format),
                        segment.span
                    );
                }
            }
        }

        m_node_types[interpolated] = m_type_string;
        return m_type_string;
    }

    if (auto id = dynamic_cast<IdentifierExpr*>(expr.get())) {
        auto sym = m_resolver.find_symbol_for_node(id);
        if (!sym) {
            sym = scope->lookup(id->name);
        }

        if (sym) {
            if (m_in_static_method && (sym->kind == SymbolKind::Field || sym->kind == SymbolKind::Method) &&
                !sym->is_static) {
                m_diagnostics.report_error("E3142", std::format("Static method cannot access instance member '{}'", id->name), id->span);
            }
            // Built-in special objects
            if (id->name == "input") {
                auto t = Type::make_custom(TypeKind::Class, "Input");
                m_node_types[id] = t;
                return t;
            }
            if (id->name == "body") {
                auto t = Type::make_custom(TypeKind::Class, "RigidBody2D");
                m_node_types[id] = t;
                return t;
            }
            if (id->name == "animator") {
                auto t = Type::make_custom(TypeKind::Class, "Animator2D");
                t->is_nullable = true;
                m_node_types[id] = t;
                return t;
            }
            if (id->name == "Result") {
                auto t = Type::make_custom(TypeKind::Class, "Result");
                m_node_types[id] = t;
                return t;
            }

            if (sym->kind == SymbolKind::Class || sym->kind == SymbolKind::Struct ||
                sym->kind == SymbolKind::Interface || sym->kind == SymbolKind::Enum ||
                sym->kind == SymbolKind::TypeParameter || sym->kind == SymbolKind::Import) {
                TypeAnnotation annotation = parse_type_text(sym->type_string);
                auto type = resolve_type_annotation(annotation, scope.get());
                m_node_types[id] = type;
                return type;
            }

            if (sym->kind == SymbolKind::Function) {
                if (const ApiFunctionDescriptor* function =
                        ApiCatalog::instance().find_available_function(id->name)) {
                    auto t = Type::make_custom(TypeKind::Function, function->name);
                    TypeAnnotation return_annotation = parse_type_text(function->return_type);
                    t->return_type = resolve_type_annotation(return_annotation, scope.get());
                    for (const ApiParameterDescriptor& parameter : function->parameters) {
                        TypeAnnotation parameter_annotation = parse_type_text(parameter.type);
                        t->param_types.push_back(resolve_type_annotation(parameter_annotation, scope.get()));
                    }
                    m_node_types[id] = t;
                    return t;
                }
                if (const auto* function = dynamic_cast<const FunctionDecl*>(sym->ast_node.get())) {
                    auto type = Type::make_custom(TypeKind::Function, function->name);
                    type->return_type = function->return_type
                        ? resolve_type_annotation(*function->return_type, scope.get()) : m_type_void;
                    for (const auto& parameter : function->params) {
                        type->param_types.push_back(resolve_type_annotation(parameter->type_annotation, scope.get()));
                    }
                    m_node_types[id] = type;
                    return type;
                }
            }

            if (sym->type_string.starts_with("Result<") && sym->type_string.ends_with('>')) {
                const std::string inner = sym->type_string.substr(7, sym->type_string.size() - 8);
                const size_t separator = inner.find(',');
                if (separator != std::string::npos) {
                    const auto trim = [](std::string value) {
                        const size_t first = value.find_first_not_of(' ');
                        const size_t last = value.find_last_not_of(' ');
                        return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
                    };
                    TypeAnnotation value_annotation{trim(inner.substr(0, separator))};
                    TypeAnnotation error_annotation{trim(inner.substr(separator + 1))};
                    auto t = Type::make_generic(
                        TypeKind::Result,
                        "Result",
                        {resolve_type_annotation(value_annotation, scope.get()), resolve_type_annotation(error_annotation, scope.get())}
                    );
                    m_node_types[id] = t;
                    return t;
                }
            }

            if (sym->type_string.starts_with("Array<") && sym->type_string.ends_with('>')) {
                TypeAnnotation element_annotation{sym->type_string.substr(6, sym->type_string.size() - 7)};
                auto t = Type::make_generic(TypeKind::Array, "Array", {resolve_type_annotation(element_annotation, scope.get())});
                m_node_types[id] = t;
                return t;
            }

            TypeAnnotation annot = parse_type_text(sym->type_string);
            auto t = resolve_type_annotation(annot, scope.get());
            if (m_narrowed_nonnull.contains(id->name) && t->is_nullable) {
                t = std::make_shared<Type>(*t);
                t->is_nullable = false;
            }
            m_node_types[id] = t;
            return t;
        }
        return m_type_error;
    }

    if (auto conditional = dynamic_cast<ConditionalExpr*>(expr.get())) {
        const auto condition_type = check_expression(conditional->condition, scope);
        if (!is_truthy_compatible(condition_type) && !condition_type->is_error()) {
            m_diagnostics.report_error(
                "E3104",
                std::format("Conditional expression requires a truthy-compatible condition, got '{}'",
                    condition_type->to_string()),
                conditional->condition->span
            );
        }
        const auto then_type = check_expression(conditional->then_value, scope);
        const auto else_type = check_expression(conditional->else_value, scope);
        std::shared_ptr<Type> result = m_type_error;
        if (then_type->is_numeric() && else_type->is_numeric()) {
            if (const auto promoted = promote_numeric_types(then_type->kind, else_type->kind)) {
                result = Type::make_primitive(*promoted);
            }
        } else if (then_type->is_assignable_to(else_type)) {
            result = else_type;
        } else if (else_type->is_assignable_to(then_type)) {
            result = then_type;
        }
        if (result->is_error() && !then_type->is_error() && !else_type->is_error()) {
            m_diagnostics.report_error(
                "E3105",
                std::format("Conditional branches '{}' and '{}' are incompatible",
                    then_type->to_string(), else_type->to_string()),
                conditional->span
            );
        }
        m_node_types[conditional] = result;
        return result;
    }

    if (auto match_expression = dynamic_cast<MatchExpr*>(expr.get())) {
        const auto subject_type = check_expression(match_expression->subject, scope);
        std::vector<MatchPattern> patterns;
        patterns.reserve(match_expression->arms.size());
        std::shared_ptr<Type> result;
        for (const auto& arm : match_expression->arms) {
            patterns.push_back(arm.pattern);
            const auto arm_type = check_expression(arm.value, scope);
            if (!result) {
                result = arm_type;
            } else if (result->is_numeric() && arm_type->is_numeric()) {
                if (const auto promoted = promote_numeric_types(result->kind, arm_type->kind)) {
                    result = Type::make_primitive(*promoted);
                } else {
                    result = m_type_error;
                }
            } else if (arm_type->is_assignable_to(result)) {
                // Keep the current common type.
            } else if (result->is_assignable_to(arm_type)) {
                result = arm_type;
            } else {
                m_diagnostics.report_error(
                    "E3109",
                    std::format("Match arm type '{}' is incompatible with '{}'",
                        arm_type->to_string(), result->to_string()),
                    arm.value->span
                );
                result = m_type_error;
            }
        }
        validate_match_patterns(subject_type, patterns, scope, m_diagnostics, match_expression->span);
        if (!result) result = m_type_error;
        m_node_types[match_expression] = result;
        return result;
    }

    if (auto bin = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto left_t = check_expression(bin->left, scope);
        auto right_t = check_expression(bin->right, scope);

        // Arithmetic
        if (bin->op == TokenType::Plus || bin->op == TokenType::Minus || bin->op == TokenType::Star || bin->op == TokenType::Slash || bin->op == TokenType::Percent) {
            if ((left_t->is_string() || right_t->is_string()) && bin->op == TokenType::Plus) {
                if (!left_t->is_string() || !right_t->is_string()) {
                    m_diagnostics.report_error("E3063", "String concatenation requires String operands; EngineScript has no implicit text conversion", bin->span);
                    m_node_types[bin] = m_type_error;
                    return m_type_error;
                }
                m_node_types[bin] = m_type_string;
                return m_type_string;
            }
            if (left_t->is_numeric() && right_t->is_numeric()) {
                const auto promoted = promote_numeric_types(left_t->kind, right_t->kind);
                if (!promoted) {
                    m_diagnostics.report_error(
                        "E3062",
                        std::format("Operator operands '{}' and '{}' require an explicit numeric conversion",
                            left_t->to_string(), right_t->to_string()),
                        bin->span
                    );
                    m_node_types[bin] = m_type_error;
                    return m_type_error;
                }
                std::shared_ptr<Type> res_t = Type::make_primitive(*promoted);
                m_node_types[bin] = res_t;
                return res_t;
            }
            if (left_t->kind == TypeKind::Vec2 && right_t->is_numeric()) {
                m_node_types[bin] = left_t;
                return left_t;
            }
            if (left_t->is_numeric() && right_t->kind == TypeKind::Vec2) {
                m_node_types[bin] = right_t;
                return right_t;
            }
        }

        // Relational
        if (bin->op == TokenType::Less || bin->op == TokenType::LessEquals || bin->op == TokenType::Greater || bin->op == TokenType::GreaterEquals) {
            if (left_t->is_numeric() && right_t->is_numeric() &&
                !promote_numeric_types(left_t->kind, right_t->kind)) {
                m_diagnostics.report_error(
                    "E3062",
                    std::format("Operator operands '{}' and '{}' require an explicit numeric conversion",
                        left_t->to_string(), right_t->to_string()),
                    bin->span
                );
            }
            m_node_types[bin] = m_type_bool;
            return m_type_bool;
        }

        // Equality
        if (bin->op == TokenType::EqualsEquals || bin->op == TokenType::ExclamationEquals) {
            m_node_types[bin] = m_type_bool;
            return m_type_bool;
        }

        // Logical
        if (bin->op == TokenType::AmpAmp || bin->op == TokenType::PipePipe) {
            if ((!is_truthy_compatible(left_t) && !left_t->is_error()) ||
                (!is_truthy_compatible(right_t) && !right_t->is_error())) {
                m_diagnostics.report_error("E3064", "Logical operators require truthy-compatible operands", bin->span);
            }
            m_node_types[bin] = m_type_bool;
            return m_type_bool;
        }

        // Null coalescing (a ?? b)
        if (bin->op == TokenType::QuestionQuestion) {
            if (!left_t->is_nullable && !left_t->is_error()) {
                m_diagnostics.report_error("E3065", "Left operand of the null-coalescing operator must be nullable", bin->left->span);
            }
            auto unwrapped = std::make_shared<Type>(*left_t);
            unwrapped->is_nullable = false;
            if (!right_t->is_assignable_to(unwrapped)) {
                m_diagnostics.report_error("E3066", std::format("Fallback type '{}' is not assignable to '{}'",
                    right_t->to_string(), unwrapped->to_string()), bin->right->span);
            }
            m_node_types[bin] = unwrapped;
            return unwrapped;
        }

        m_node_types[bin] = left_t;
        return left_t;
    }

    if (auto un = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto op_t = check_expression(un->operand, scope);
        if (un->op == TokenType::Exclamation) {
            if (!is_truthy_compatible(op_t) && !op_t->is_error()) {
                m_diagnostics.report_error("E3095", std::format("'!' requires a truthy-compatible operand, got '{}'", op_t->to_string()), un->span);
            }
            m_node_types[un] = m_type_bool;
            return m_type_bool;
        }
        m_node_types[un] = op_t;
        return op_t;
    }

    if (auto assign = dynamic_cast<AssignmentExpr*>(expr.get())) {
        if (!dynamic_cast<IdentifierExpr*>(assign->target.get()) &&
            !dynamic_cast<MemberAccessExpr*>(assign->target.get()) &&
            !dynamic_cast<IndexExpr*>(assign->target.get())) {
            m_diagnostics.report_error("E3043", "Only variables, properties, and indexed collection elements are assignable", assign->target->span);
            m_node_types[assign] = m_type_error;
            return m_type_error;
        }
        auto target_t = check_expression(assign->target, scope);
        auto val_t = check_expression_expected(assign->value, scope, target_t);
        if (auto member = dynamic_cast<MemberAccessExpr*>(assign->target.get())) {
            const auto owner_t = get_type_of_node(member->object.get());
            std::string owner_name = owner_t ? owner_t->name : "";
            if (owner_t && owner_t->name == "ComponentRef" && owner_t->type_args.size() == 1) {
                owner_name = owner_t->type_args.front()->name;
            }
            if (owner_t && owner_t->kind == TypeKind::String) owner_name = "String";
            if (owner_t && owner_t->kind == TypeKind::Entity) owner_name = "Entity";
            if (const ApiPropertyDescriptor* property = ApiCatalog::instance().find_available_property(owner_name, member->member_name);
                property && !property->writable) {
                m_diagnostics.report_error("E3044", std::format("{}.{} is read-only", owner_name, member->member_name), member->span);
            }
        }
        const bool assigns_component_handle = target_t->name == "ComponentRef" &&
            target_t->type_args.size() == 1 && val_t->name == target_t->type_args.front()->name;
        if (!val_t->is_assignable_to(target_t) && !assigns_component_handle) {
            m_diagnostics.report_error("E3020", std::format("Cannot assign type '{}' to target of type '{}'", val_t->to_string(), target_t->to_string()), assign->value->span);
        }
        m_node_types[assign] = target_t;
        return target_t;
    }

    if (auto call = dynamic_cast<CallExpr*>(expr.get())) {
        auto callee_t = check_expression(call->callee, scope);
        for (const auto& a : call->arguments) {
            check_expression(a, scope);
        }
        if (m_in_static_method) {
            if (const auto* identifier = dynamic_cast<IdentifierExpr*>(call->callee.get());
                identifier && (identifier->name == "self" || identifier->name == "instantiate" ||
                    identifier->name == "findEntities" || identifier->name == "delay" ||
                    identifier->name == "realDelay" || identifier->name == "nextFrame" ||
                    identifier->name == "fixedUpdate")) {
                m_diagnostics.report_error("E3148", std::format("Static method cannot call instance function '{}'", identifier->name), identifier->span);
            }
        }
        if (const auto* identifier = dynamic_cast<IdentifierExpr*>(call->callee.get());
            identifier && identifier->name == "require") {
            if (call->arguments.size() != 2) {
                m_diagnostics.report_error("E3143", "require expects a nullable value and an error message", call->span);
                m_node_types[call] = m_type_error;
                return m_type_error;
            }
            const auto value_type = get_type_of_node(call->arguments[0].get());
            const auto message_type = get_type_of_node(call->arguments[1].get());
            if (!message_type || !message_type->is_string()) {
                m_diagnostics.report_error("E3144", "require message must be a String", call->arguments[1]->span);
            }
            if (!value_type || (!value_type->is_nullable && value_type->kind != TypeKind::Optional)) {
                m_diagnostics.report_error("E3145", "require value must be nullable", call->arguments[0]->span);
            }
            auto required_type = value_type ? std::make_shared<Type>(*value_type) : m_type_error;
            required_type->is_nullable = false;
            if (required_type->kind == TypeKind::Optional && !required_type->type_args.empty()) {
                required_type = required_type->type_args.front();
            }
            m_node_types[call] = required_type;
            return required_type;
        }
        const auto normalize_authored = [&](const std::vector<std::shared_ptr<ParamDecl>>& parameters,
                                             std::string_view callable_name) {
            std::vector<std::string> names;
            std::vector<std::shared_ptr<Expression>> defaults;
            std::vector<bool> optional;
            names.reserve(parameters.size());
            defaults.reserve(parameters.size());
            optional.reserve(parameters.size());
            for (const auto& parameter : parameters) {
                names.push_back(parameter->name);
                defaults.push_back(parameter->default_value);
                optional.push_back(false);
            }
            normalize_call_arguments(*call, names, defaults, optional, scope, callable_name);
        };
        const auto normalize_catalog = [&](const std::vector<ApiParameterDescriptor>& parameters,
                                            std::string_view callable_name) {
            std::vector<std::string> names;
            std::vector<std::shared_ptr<Expression>> defaults;
            std::vector<bool> optional;
            names.reserve(parameters.size());
            defaults.reserve(parameters.size());
            optional.reserve(parameters.size());
            for (const auto& parameter : parameters) {
                names.push_back(parameter.name);
                defaults.push_back(catalog_default_expression(parameter, call->span));
                optional.push_back(parameter.optional);
            }
            normalize_call_arguments(*call, names, defaults, optional, scope, callable_name);
        };
        if (const auto* identifier = dynamic_cast<IdentifierExpr*>(call->callee.get())) {
            if (const auto* function = ApiCatalog::instance().find_available_function(identifier->name)) {
                normalize_catalog(function->parameters, function->name);
            }
        }

        // Handle known engine builtin calls & methods
        if (auto mem = dynamic_cast<MemberAccessExpr*>(call->callee.get())) {
            auto target_t = get_type_of_node(mem->object.get());
            if (target_t) {
                if (const auto member_symbol = m_member_symbols.find(mem); member_symbol != m_member_symbols.end()) {
                    if (const auto* method = dynamic_cast<const MethodDecl*>(member_symbol->second->ast_node.get())) {
                        normalize_authored(method->params, target_t->name + "." + method->name);
                    }
                }
                std::string early_catalog_owner = target_t->name;
                if (target_t->kind == TypeKind::String) early_catalog_owner = "String";
                if (target_t->kind == TypeKind::Entity) early_catalog_owner = "Entity";
                if (target_t->kind == TypeKind::Array) early_catalog_owner = "Array";
                if (target_t->kind == TypeKind::Deque) early_catalog_owner = "Deque";
                if (target_t->kind == TypeKind::FixedArray) early_catalog_owner = "FixedArray";
                if (target_t->kind == TypeKind::Map) early_catalog_owner = "Map";
                if (target_t->kind == TypeKind::Set) early_catalog_owner = "Set";
                if (const auto* method = ApiCatalog::instance().find_available_method(
                        early_catalog_owner, mem->member_name)) {
                    normalize_catalog(method->parameters, early_catalog_owner + "." + mem->member_name);
                }
                const auto normalize_native_collection = [&](std::initializer_list<std::string_view> parameter_names) {
                    std::vector<std::string> names;
                    names.reserve(parameter_names.size());
                    for (const std::string_view name : parameter_names) names.emplace_back(name);
                    normalize_call_arguments(
                        *call,
                        names,
                        std::vector<std::shared_ptr<Expression>>(names.size()),
                        std::vector<bool>(names.size(), false),
                        scope,
                        early_catalog_owner + "." + mem->member_name
                    );
                };
                if (target_t->kind == TypeKind::Array) {
                    if (mem->member_name == "push" || mem->member_name == "contains" ||
                        mem->member_name == "remove" || mem->member_name == "indexOf" ||
                        mem->member_name == "lastIndexOf") {
                        normalize_native_collection({"value"});
                    } else if (mem->member_name == "removeAt") {
                        normalize_native_collection({"index"});
                    } else if (mem->member_name == "swap") {
                        normalize_native_collection({"left", "right"});
                    } else if (mem->member_name == "shuffle" || mem->member_name == "choose") {
                        normalize_native_collection({"seed"});
                    } else if (mem->member_name == "map" || mem->member_name == "filter" ||
                               mem->member_name == "sortBy") {
                        normalize_native_collection({"callback"});
                    } else if (mem->member_name == "reduce") {
                        normalize_native_collection({"initial", "reducer"});
                    } else if (mem->member_name == "pop" || mem->member_name == "reverse" ||
                               mem->member_name == "clear") {
                        normalize_native_collection({});
                    }
                } else if (target_t->kind == TypeKind::Set) {
                    if (mem->member_name == "add" || mem->member_name == "contains" ||
                        mem->member_name == "remove") {
                        normalize_native_collection({"value"});
                    } else if (mem->member_name == "clear") {
                        normalize_native_collection({});
                    }
                } else if (target_t->kind == TypeKind::Map) {
                    if (mem->member_name == "set") {
                        normalize_native_collection({"key", "value"});
                    } else if (mem->member_name == "get" || mem->member_name == "containsKey" ||
                               mem->member_name == "remove") {
                        normalize_native_collection({"key"});
                    } else if (mem->member_name == "keys" || mem->member_name == "values" ||
                               mem->member_name == "clear") {
                        normalize_native_collection({});
                    }
                }
                if (target_t->kind == TypeKind::Entity &&
                    (mem->member_name == "getComponent" || mem->member_name == "getComponents" ||
                     mem->member_name == "hasComponent" || mem->member_name == "addComponent" ||
                     mem->member_name == "requireComponent")) {
                    if (call->type_arguments.size() != 1) {
                        m_diagnostics.report_error(
                            "E3050",
                            std::format("Entity.{} requires exactly one component type argument", mem->member_name),
                            call->span
                        );
                        m_node_types[call] = m_type_error;
                        return m_type_error;
                    }
                    if (!call->arguments.empty()) {
                        m_diagnostics.report_error(
                            "E3051",
                            std::format("Entity.{} expects no value arguments", mem->member_name),
                            call->span
                        );
                    }
                    auto component_type = resolve_type_annotation(call->type_arguments.front(), scope.get());
                    const std::string& component_name = call->type_arguments.front().name;
                    const bool native_component = component_name == "Transform2D" || component_name == "RigidBody2D" ||
                        component_name == "SpriteRenderer2D" || component_name == "Animator2D" ||
                        component_name == "ParticleEmitter2D" || component_name == "Joint2D";
                    const auto authored_symbol = scope->lookup(component_name);
                    const bool authored_component = authored_symbol && authored_symbol->is_behaviour;
                    if (!native_component && !authored_component) {
                        m_diagnostics.report_error(
                            "E3052",
                            std::format("'{}' is not an attachable EngineScript component", component_name),
                            call->type_arguments.front().span
                        );
                    }
                    if (mem->member_name == "hasComponent") {
                        m_node_types[call] = m_type_bool;
                        return m_type_bool;
                    }
                    if (mem->member_name == "getComponents") {
                        auto array_type = Type::make_generic(TypeKind::Array, "Array", {component_type});
                        m_node_types[call] = array_type;
                        return array_type;
                    }
                    component_type = std::make_shared<Type>(*component_type);
                    component_type->is_nullable = mem->member_name != "requireComponent";
                    m_node_types[call] = component_type;
                    return component_type;
                }
                if (target_t->kind == TypeKind::Entity && mem->member_name == "removeComponent") {
                    if (call->arguments.size() != 1) {
                        m_diagnostics.report_error("E3053", "Entity.removeComponent expects one component reference", call->span);
                    }
                    m_node_types[call] = m_type_bool;
                    return m_type_bool;
                }
                if (target_t->name == "Assets" && mem->member_name == "load") {
                    const bool valid_type = call->type_arguments.size() == 1 &&
                        (call->type_arguments.front().name == "Texture" ||
                         call->type_arguments.front().name == "AudioClip" ||
                         call->type_arguments.front().name == "Font" ||
                         call->type_arguments.front().name == "DataAsset" ||
                         call->type_arguments.front().name == "ParticleEffect2D");
                    if (!valid_type) {
                        m_diagnostics.report_error("E3036", "Assets.load requires exactly one resource type: Texture, AudioClip, Font, DataAsset, or ParticleEffect2D", call->span);
                    }
                    if (call->arguments.size() != 1 || !get_type_of_node(call->arguments.front().get())->is_string()) {
                        m_diagnostics.report_error("E3037", "Assets.load requires one String path", call->span);
                    }
                    std::vector<std::shared_ptr<Type>> asset_arguments;
                    if (!call->type_arguments.empty()) {
                        asset_arguments.push_back(resolve_type_annotation(call->type_arguments.front(), scope.get()));
                    }
                    auto asset_type = Type::make_generic(TypeKind::Class, "Asset", std::move(asset_arguments));
                    auto result_type = Type::make_generic(TypeKind::Result, "Result", {asset_type, m_type_string});
                    auto task_type = Type::make_generic(TypeKind::Task, "Task", {result_type});
                    m_node_types[call] = task_type;
                    return task_type;
                }
                if (target_t->name == "Events" && mem->member_name == "emit") {
                    if (!call->type_arguments.empty() || call->arguments.size() != 1) {
                        m_diagnostics.report_error("E3060", "Events.emit expects exactly one typed event value", call->span);
                    } else {
                        const auto event_type = get_type_of_node(call->arguments.front().get());
                        if (!event_type || event_type->kind != TypeKind::Struct) {
                            m_diagnostics.report_error("E3061", "Events.emit only accepts an authored struct event", call->span);
                        }
                    }
                    m_node_types[call] = m_type_bool;
                    return m_type_bool;
                }
                if (target_t->name == "Events" && mem->member_name == "next") {
                    if (call->type_arguments.size() != 1 || !call->arguments.empty()) {
                        m_diagnostics.report_error("E3062", "Events.next<T>() requires one struct type and no value arguments", call->span);
                    }
                    auto event_type = call->type_arguments.empty()
                        ? m_type_error
                        : resolve_type_annotation(call->type_arguments.front(), scope.get());
                    if (!event_type->is_error() && event_type->kind != TypeKind::Struct) {
                        m_diagnostics.report_error("E3063", "Events.next<T>() requires T to be an authored struct", call->span);
                    }
                    auto task_type = Type::make_generic(TypeKind::Task, "Task", {event_type});
                    m_node_types[call] = task_type;
                    return task_type;
                }
                if (target_t->name == "Jobs" && mem->member_name == "run") {
                    if (call->type_arguments.size() > 1 || call->arguments.size() != 1) {
                        m_diagnostics.report_error("E3064", "Jobs.run expects exactly one value-producing closure", call->span);
                    }
                    auto work_type = call->arguments.empty()
                        ? m_type_error
                        : get_type_of_node(call->arguments.front().get());
                    if (!work_type || work_type->kind != TypeKind::Function || !work_type->return_type ||
                        work_type->return_type->kind == TypeKind::Void) {
                        m_diagnostics.report_error("E3065", "Jobs.run requires a closure that produces a Send value", call->span);
                        work_type = Type::make_custom(TypeKind::Function, "job");
                        work_type->return_type = m_type_error;
                    }
                    if (work_type->return_type && !work_type->return_type->is_error() &&
                        !is_send_type(work_type->return_type)) {
                        m_diagnostics.report_error(
                            "E3085",
                            std::format("Jobs.run result type '{}' does not implement Send", work_type->return_type->to_string()),
                            call->arguments.empty() ? call->span : call->arguments.front()->span
                        );
                    }
                    if (!call->arguments.empty()) {
                        if (const auto* closure = dynamic_cast<const ArrowFunctionExpr*>(call->arguments.front().get())) {
                            ClosureCaptureScan capture_scan;
                            for (const auto& parameter : closure->params) capture_scan.local_names.insert(parameter->name);
                            scan_capture_expression(closure->body_expr, capture_scan);
                            scan_capture_statement(closure->body_stmt, capture_scan);
                            if (capture_scan.captures_this) {
                                m_diagnostics.report_error(
                                    "E3086",
                                    "Jobs.run cannot capture 'this' or ECS-owned state",
                                    closure->span
                                );
                            }

                            std::unordered_set<std::uint64_t> reported_symbols;
                            for (const IdentifierExpr* identifier : capture_scan.identifiers) {
                                if (!identifier || capture_scan.local_names.contains(identifier->name)) continue;
                                const auto symbol = m_resolver.find_symbol_for_node(identifier);
                                if (!symbol || (symbol->kind != SymbolKind::Variable &&
                                                symbol->kind != SymbolKind::Parameter &&
                                                symbol->kind != SymbolKind::Field)) {
                                    continue;
                                }
                                if (!reported_symbols.emplace(symbol->id).second) continue;

                                const auto capture_type = get_type_of_node(identifier);
                                const bool immutable = symbol->kind == SymbolKind::Parameter || symbol->is_const;
                                if (symbol->kind == SymbolKind::Field || !immutable || !is_send_type(capture_type)) {
                                    m_diagnostics.report_error(
                                        "E3087",
                                        std::format(
                                            "Jobs.run capture '{}' must be an immutable Send value; '{}' is not worker-safe",
                                            identifier->name,
                                            capture_type ? capture_type->to_string() : std::string{"<unknown>"}
                                        ),
                                        identifier->span
                                    );
                                }
                            }
                        } else {
                            m_diagnostics.report_error(
                                "E3088",
                                "Jobs.run requires an inline closure so captures can be validated",
                                call->arguments.front()->span
                            );
                        }
                    }
                    auto result_type = Type::make_generic(
                        TypeKind::Result,
                        "Result",
                        {work_type->return_type, m_type_string}
                    );
                    auto task_type = Type::make_generic(TypeKind::Task, "Task", {result_type});
                    m_node_types[call] = task_type;
                    return task_type;
                }
                std::string catalog_owner = target_t->name;
                if (target_t->kind == TypeKind::String) catalog_owner = "String";
                if (target_t->kind == TypeKind::Entity) catalog_owner = "Entity";
                if (target_t->kind == TypeKind::Array) catalog_owner = "Array";
                if (target_t->kind == TypeKind::Deque) catalog_owner = "Deque";
                if (target_t->kind == TypeKind::FixedArray) catalog_owner = "FixedArray";
                if (target_t->kind == TypeKind::Map) catalog_owner = "Map";
                if (target_t->kind == TypeKind::Set) catalog_owner = "Set";
                if (target_t->kind == TypeKind::Rect) catalog_owner = "Rect";
                if (target_t->kind == TypeKind::Bounds2) catalog_owner = "Bounds2";
                if (const ApiMethodDescriptor* method = ApiCatalog::instance().find_available_method(
                        catalog_owner, mem->member_name)) {
                    normalize_catalog(method->parameters, catalog_owner + "." + mem->member_name);
                    const auto resolve_method_type = [&](const std::string& type_name) {
                        if (type_name == "T" && !target_t->type_args.empty()) return target_t->type_args.front();
                        return resolve_type_annotation(parse_type_text(type_name), scope.get());
                    };
                    if (call->arguments.size() < method->min_arguments ||
                        call->arguments.size() > method->max_arguments) {
                        m_diagnostics.report_error(
                            "E3035",
                            std::format(
                                "{}.{} expects {} to {} arguments, got {}",
                                catalog_owner,
                                mem->member_name,
                                method->min_arguments,
                                method->max_arguments,
                                call->arguments.size()
                            ),
                            call->span
                        );
                    }
                    for (std::size_t index = 0; index < call->arguments.size() && index < method->parameters.size(); ++index) {
                        const auto parameter_type = resolve_method_type(method->parameters[index].type);
                        const auto argument_type = get_type_of_node(call->arguments[index].get());
                        if (argument_type && !argument_type->is_assignable_to(parameter_type)) {
                            m_diagnostics.report_error(
                                "E3045",
                                std::format("{}.{} argument '{}' expects '{}', got '{}'", catalog_owner, mem->member_name,
                                    method->parameters[index].name, parameter_type->to_string(), argument_type->to_string()),
                                call->arguments[index]->span
                            );
                        }
                    }
                    auto return_type = resolve_method_type(method->return_type);
                    if (method->nullable_return) {
                        return_type = std::make_shared<Type>(*return_type);
                        return_type->is_nullable = true;
                    }
                    m_node_types[call] = return_type;
                    return return_type;
                }
            }
            if (target_t && target_t->kind == TypeKind::String) {
                const auto validate_argument_count = [&](std::size_t expected) {
                    if (call->arguments.size() != expected) {
                        m_diagnostics.report_error(
                            "E3030",
                            std::format(
                                "String.{} expects {} argument{}, got {}",
                                mem->member_name,
                                expected,
                                expected == 1 ? "" : "s",
                                call->arguments.size()
                            ),
                            call->span
                        );
                        return false;
                    }
                    return true;
                };
                const auto validate_string_argument = [&]() {
                    if (!validate_argument_count(1)) return false;
                    const auto argument_type = get_type_of_node(call->arguments.front().get());
                    if (!argument_type || !argument_type->is_string()) {
                        m_diagnostics.report_error(
                            "E3031",
                            std::format("String.{} expects a String argument", mem->member_name),
                            call->arguments.front()->span
                        );
                        return false;
                    }
                    return true;
                };

                if (mem->member_name == "includes" || mem->member_name == "contains" ||
                    mem->member_name == "startsWith" || mem->member_name == "endsWith") {
                    static_cast<void>(validate_string_argument());
                    m_node_types[call] = m_type_bool;
                    return m_type_bool;
                }
                if (mem->member_name == "indexOf") {
                    static_cast<void>(validate_string_argument());
                    m_node_types[call] = m_type_int;
                    return m_type_int;
                }
                if (mem->member_name == "length" || mem->member_name == "size") {
                    static_cast<void>(validate_argument_count(0));
                    m_node_types[call] = m_type_int;
                    return m_type_int;
                }
                if (mem->member_name == "toLowerCase" || mem->member_name == "toUpperCase") {
                    static_cast<void>(validate_argument_count(0));
                    m_node_types[call] = m_type_string;
                    return m_type_string;
                }
            }

            if (target_t && target_t->kind == TypeKind::Entity && mem->member_name == "destroy") {
                if (!call->arguments.empty()) {
                    m_diagnostics.report_error(
                        "E3032",
                        std::format("Entity.destroy expects 0 arguments, got {}", call->arguments.size()),
                        call->span
                    );
                }
                m_node_types[call] = m_type_void;
                return m_type_void;
            }

            if (target_t && (target_t->kind == TypeKind::Array || target_t->kind == TypeKind::Set) &&
                !target_t->type_args.empty()) {
                const auto& element_type = target_t->type_args[0];
                const bool inserts_element =
                    (target_t->kind == TypeKind::Array && mem->member_name == "push") ||
                    (target_t->kind == TypeKind::Set && mem->member_name == "add");
                if (inserts_element) {
                    if (call->arguments.size() != 1 ||
                        (call->arguments.size() == 1 && !get_type_of_node(call->arguments[0].get())->is_assignable_to(element_type))) {
                        m_diagnostics.report_error("E3080", std::format("{}.{} expects one '{}' value",
                            target_t->name, mem->member_name, element_type->to_string()), call->span);
                    }
                    m_node_types[call] = m_type_void;
                    return m_type_void;
                }
                if (mem->member_name == "contains" || mem->member_name == "remove") {
                    if (call->arguments.size() != 1 ||
                        (call->arguments.size() == 1 &&
                         !get_type_of_node(call->arguments[0].get())->is_assignable_to(element_type))) {
                        m_diagnostics.report_error("E3080", std::format("{}.{} expects one '{}' value",
                            target_t->name, mem->member_name, element_type->to_string()), call->span);
                    }
                    m_node_types[call] = m_type_bool;
                    return m_type_bool;
                }
                if (target_t->kind == TypeKind::Array &&
                    (mem->member_name == "indexOf" || mem->member_name == "lastIndexOf")) {
                    if (call->arguments.size() != 1 ||
                        (call->arguments.size() == 1 && !get_type_of_node(call->arguments[0].get())->is_assignable_to(element_type))) {
                        m_diagnostics.report_error("E3080", std::format("Array.{} expects one '{}' value",
                            mem->member_name, element_type->to_string()), call->span);
                    }
                    m_node_types[call] = m_type_int;
                    return m_type_int;
                }
                if (target_t->kind == TypeKind::Array && mem->member_name == "removeAt") {
                    if (call->arguments.size() != 1 ||
                        (call->arguments.size() == 1 && !get_type_of_node(call->arguments[0].get())->is_integer())) {
                        m_diagnostics.report_error("E3080", "Array.removeAt expects one integer index", call->span);
                    }
                    m_node_types[call] = m_type_bool;
                    return m_type_bool;
                }
                if (target_t->kind == TypeKind::Array && mem->member_name == "reverse") {
                    if (!call->arguments.empty()) m_diagnostics.report_error("E3080", "Array.reverse expects no arguments", call->span);
                    m_node_types[call] = m_type_void;
                    return m_type_void;
                }
                if (target_t->kind == TypeKind::Array && mem->member_name == "swap") {
                    const bool valid_indices = call->arguments.size() == 2 &&
                        get_type_of_node(call->arguments[0].get())->is_integer() &&
                        get_type_of_node(call->arguments[1].get())->is_integer();
                    if (!valid_indices) m_diagnostics.report_error("E3080", "Array.swap expects two integer indices", call->span);
                    m_node_types[call] = m_type_void;
                    return m_type_void;
                }
                if (target_t->kind == TypeKind::Array && mem->member_name == "shuffle") {
                    const bool valid_seed = call->arguments.size() == 1 && get_type_of_node(call->arguments[0].get())->kind == TypeKind::UInt32;
                    if (!valid_seed) m_diagnostics.report_error("E3080", "Array.shuffle expects one UInt seed", call->span);
                    m_node_types[call] = m_type_void;
                    return m_type_void;
                }
                if (target_t->kind == TypeKind::Array && mem->member_name == "choose") {
                    const bool valid_seed = call->arguments.size() == 1 && get_type_of_node(call->arguments[0].get())->kind == TypeKind::UInt32;
                    if (!valid_seed) m_diagnostics.report_error("E3080", "Array.choose expects one UInt seed", call->span);
                    auto result = std::make_shared<Type>(*element_type);
                    result->is_nullable = true;
                    m_node_types[call] = result;
                    return result;
                }
                if (target_t->kind == TypeKind::Array && (mem->member_name == "map" || mem->member_name == "filter" || mem->member_name == "sortBy")) {
                    const auto callback = call->arguments.size() == 1 ? get_type_of_node(call->arguments[0].get()) : m_type_error;
                    const bool valid = callback && callback->kind == TypeKind::Function && callback->param_types.size() == (mem->member_name == "sortBy" ? 2u : 1u) &&
                        std::all_of(callback->param_types.begin(), callback->param_types.end(), [&](const auto& parameter) { return element_type->is_assignable_to(parameter); });
                    if (!valid) m_diagnostics.report_error("E3080", std::format("Array.{} expects a callback over '{}' values", mem->member_name, element_type->to_string()), call->span);
                    if (mem->member_name == "map") {
                        auto result = Type::make_generic(TypeKind::Array, "Array", {valid ? callback->return_type : m_type_error});
                        m_node_types[call] = result; return result;
                    }
                    if (mem->member_name == "filter") {
                        if (valid && !callback->return_type->is_bool()) m_diagnostics.report_error("E3080", "Array.filter callback must return Bool", call->span);
                        m_node_types[call] = target_t; return target_t;
                    }
                    if (valid && !callback->return_type->is_integer()) m_diagnostics.report_error("E3080", "Array.sortBy callback must return Int", call->span);
                    m_node_types[call] = m_type_void; return m_type_void;
                }
                if (target_t->kind == TypeKind::Array && mem->member_name == "reduce") {
                    const auto callback = call->arguments.size() == 2 ? get_type_of_node(call->arguments[1].get()) : m_type_error;
                    const auto initial = call->arguments.empty() ? m_type_error : get_type_of_node(call->arguments[0].get());
                    const bool valid = callback && initial && callback->kind == TypeKind::Function && callback->param_types.size() == 2 &&
                        initial->is_assignable_to(callback->param_types[0]) && element_type->is_assignable_to(callback->param_types[1]) && callback->return_type->is_assignable_to(initial);
                    if (!valid) m_diagnostics.report_error("E3080", "Array.reduce expects an initial value and compatible reducer", call->span);
                    m_node_types[call] = initial; return initial;
                }
                if (target_t->kind == TypeKind::Array && mem->member_name == "pop") {
                    if (!call->arguments.empty()) {
                        m_diagnostics.report_error("E3080", "Array.pop expects no arguments", call->span);
                    }
                    auto result = std::make_shared<Type>(*element_type);
                    result->is_nullable = true;
                    m_node_types[call] = result;
                    return result;
                }
                if (mem->member_name == "clear") {
                    if (!call->arguments.empty()) m_diagnostics.report_error("E3080", std::format("{}.clear expects no arguments", target_t->name), call->span);
                    m_node_types[call] = m_type_void;
                    return m_type_void;
                }
            }
            if (target_t && target_t->kind == TypeKind::Deque && !target_t->type_args.empty()) {
                const auto& element_type = target_t->type_args.front();
                if (mem->member_name == "pushFront" || mem->member_name == "pushBack") {
                    if (call->arguments.size() != 1 ||
                        (call->arguments.size() == 1 &&
                         !get_type_of_node(call->arguments.front().get())->is_assignable_to(element_type))) {
                        m_diagnostics.report_error("E3118", std::format("Deque.{} expects one '{}' value",
                            mem->member_name, element_type->to_string()), call->span);
                    }
                    m_node_types[call] = m_type_void;
                    return m_type_void;
                }
                if (mem->member_name == "front" || mem->member_name == "back" ||
                    mem->member_name == "popFront" || mem->member_name == "popBack") {
                    if (!call->arguments.empty()) {
                        m_diagnostics.report_error("E3118", std::format("Deque.{} expects no arguments",
                            mem->member_name), call->span);
                    }
                    auto result = std::make_shared<Type>(*element_type);
                    result->is_nullable = true;
                    m_node_types[call] = result;
                    return result;
                }
                if (mem->member_name == "clear") {
                    if (!call->arguments.empty()) {
                        m_diagnostics.report_error("E3118", "Deque.clear expects no arguments", call->span);
                    }
                    m_node_types[call] = m_type_void;
                    return m_type_void;
                }
            }
            if (target_t && target_t->kind == TypeKind::Map && target_t->type_args.size() == 2) {
                const auto& key_type = target_t->type_args[0];
                const auto& value_type = target_t->type_args[1];
                if (mem->member_name == "set") {
                    const bool valid_arguments = call->arguments.size() == 2 &&
                        get_type_of_node(call->arguments[0].get())->is_assignable_to(key_type) &&
                        get_type_of_node(call->arguments[1].get())->is_assignable_to(value_type);
                    if (!valid_arguments) {
                        m_diagnostics.report_error("E3081", std::format("Map.set expects '{}' key and '{}' value",
                            key_type->to_string(), value_type->to_string()), call->span);
                    }
                    m_node_types[call] = m_type_void;
                    return m_type_void;
                }
                if (mem->member_name == "get") {
                    if (call->arguments.size() != 1 ||
                        (call->arguments.size() == 1 &&
                         !get_type_of_node(call->arguments[0].get())->is_assignable_to(key_type))) {
                        m_diagnostics.report_error("E3081", std::format("Map.get expects one '{}' key",
                            key_type->to_string()), call->span);
                    }
                    auto result = std::make_shared<Type>(*value_type);
                    result->is_nullable = true;
                    m_node_types[call] = result;
                    return result;
                }
                if (mem->member_name == "containsKey" || mem->member_name == "remove") {
                    if (call->arguments.size() != 1 ||
                        (call->arguments.size() == 1 &&
                         !get_type_of_node(call->arguments[0].get())->is_assignable_to(key_type))) {
                        m_diagnostics.report_error("E3081", std::format("Map.{} expects one '{}' key",
                            mem->member_name, key_type->to_string()), call->span);
                    }
                    m_node_types[call] = m_type_bool;
                    return m_type_bool;
                }
                if (mem->member_name == "keys" || mem->member_name == "values") {
                    if (!call->arguments.empty()) {
                        m_diagnostics.report_error("E3081", std::format("Map.{} expects no arguments",
                            mem->member_name), call->span);
                    }
                    auto result = Type::make_generic(TypeKind::Array, "Array",
                        {target_t->type_args[mem->member_name == "keys" ? 0 : 1]});
                    m_node_types[call] = result;
                    return result;
                }
                if (mem->member_name == "clear") {
                    if (!call->arguments.empty()) {
                        m_diagnostics.report_error("E3081", "Map.clear expects no arguments", call->span);
                    }
                    m_node_types[call] = m_type_void;
                    return m_type_void;
                }
            }
            if (target_t && target_t->kind == TypeKind::Weak && mem->member_name == "lock" &&
                target_t->type_args.size() == 1) {
                auto result = std::make_shared<Type>(*target_t->type_args[0]);
                result->is_nullable = true;
                m_node_types[call] = result;
                return result;
            }
            if (target_t && target_t->name == "Result" &&
                (mem->member_name == "success" || mem->member_name == "failure")) {
                if (call->type_arguments.size() != 2) {
                    m_diagnostics.report_error("E3038", "Result.success and Result.failure require <Value, Error> type arguments", call->span);
                }
                const TypeAnnotation value_annotation = call->type_arguments.empty() ? TypeAnnotation{"Void"} : call->type_arguments[0];
                const TypeAnnotation error_annotation = call->type_arguments.size() < 2 ? TypeAnnotation{"String"} : call->type_arguments[1];
                auto value_t = resolve_type_annotation(value_annotation, scope.get());
                auto error_t = resolve_type_annotation(error_annotation, scope.get());
                const bool is_success = mem->member_name == "success";
                const size_t expected_arguments = is_success && value_t->is_void() ? 0 : 1;
                if (call->arguments.size() != expected_arguments) {
                    m_diagnostics.report_error("E3039", std::format("Result.{} expects {} argument{}", mem->member_name, expected_arguments, expected_arguments == 1 ? "" : "s"), call->span);
                } else if (expected_arguments == 1) {
                    const auto argument_t = get_type_of_node(call->arguments.front().get());
                    const auto expected_t = is_success ? value_t : error_t;
                    if (!argument_t->is_assignable_to(expected_t)) {
                        m_diagnostics.report_error("E3040", std::format("Result.{} argument type '{}' is not assignable to '{}'", mem->member_name, argument_t->to_string(), expected_t->to_string()), call->arguments.front()->span);
                    }
                }
                auto res_t = Type::make_generic(TypeKind::Result, "Result", {value_t, error_t});
                m_node_types[call] = res_t;
                return res_t;
            }
            if (target_t && target_t->kind == TypeKind::Result) {
                if (mem->member_name == "isOk" || mem->member_name == "isError") {
                    if (!call->arguments.empty()) m_diagnostics.report_error("E3041", std::format("Result.{} expects no arguments", mem->member_name), call->span);
                    m_node_types[call] = m_type_bool;
                    return m_type_bool;
                }
                if (mem->member_name == "value" || mem->member_name == "error") {
                    if (!call->arguments.empty()) m_diagnostics.report_error("E3041", std::format("Result.{} expects no arguments", mem->member_name), call->span);
                    const size_t type_index = mem->member_name == "value" ? 0 : 1;
                    if (target_t->type_args.size() == 2) {
                        m_node_types[call] = target_t->type_args[type_index];
                        return target_t->type_args[type_index];
                    }
                }
            }
            if (target_t) {
                std::string catalog_owner = target_t->name;
                if (target_t->kind == TypeKind::String) catalog_owner = "String";
                if (target_t->kind == TypeKind::Entity) catalog_owner = "Entity";
                if (target_t->kind == TypeKind::Array) catalog_owner = "Array";
                if (target_t->kind == TypeKind::Deque) catalog_owner = "Deque";
                if (ApiCatalog::instance().has_available_type(catalog_owner)) {
                    m_diagnostics.report_error(
                        "E3047",
                        std::format("{} has no EngineScript method '{}'; use the documented SDK name", catalog_owner, mem->member_name),
                        mem->span
                    );
                    m_node_types[call] = m_type_error;
                    return m_type_error;
                }
            }
        }

        // Global async functions
        if (auto id = dynamic_cast<IdentifierExpr*>(call->callee.get())) {
            if (id->name == "String") {
                if (call->arguments.size() != 1) {
                    m_diagnostics.report_error(
                        "E3093",
                        std::format("String conversion expects exactly 1 numeric argument, got {}",
                            call->arguments.size()),
                        call->span
                    );
                } else {
                    const auto source_type = get_type_of_node(call->arguments.front().get());
                    if (!source_type || !source_type->is_numeric()) {
                        m_diagnostics.report_error(
                            "E3094",
                            std::format("String conversion requires a numeric source, got '{}'",
                                source_type ? source_type->to_string() : "<unknown>"),
                            call->arguments.front()->span
                        );
                    }
                }
                m_node_types[call] = m_type_string;
                return m_type_string;
            }
            if (const auto target_kind = numeric_type_for_name(id->name)) {
                if (call->arguments.size() != 1) {
                    m_diagnostics.report_error(
                        "E3060",
                        std::format("{} conversion expects exactly 1 numeric argument, got {}",
                            id->name, call->arguments.size()),
                        call->span
                    );
                } else {
                    const auto source_type = get_type_of_node(call->arguments.front().get());
                    if (!source_type || !source_type->is_numeric()) {
                        m_diagnostics.report_error(
                            "E3061",
                            std::format("{} conversion requires a numeric source, got '{}'",
                                id->name, source_type ? source_type->to_string() : "<unknown>"),
                            call->arguments.front()->span
                        );
                    }
                }
                auto target_type = Type::make_primitive(*target_kind);
                m_node_types[call] = target_type;
                return target_type;
            }
            if (id->name == "Deque") {
                if (call->type_arguments.size() != 1 || !call->arguments.empty()) {
                    m_diagnostics.report_error("E3119", "Deque<T>() requires one element type and no arguments", call->span);
                }
                const auto element_type = call->type_arguments.empty()
                    ? m_type_error : resolve_type_annotation(call->type_arguments.front(), scope.get());
                auto deque_type = Type::make_generic(TypeKind::Deque, "Deque", {element_type});
                m_node_types[call] = deque_type;
                return deque_type;
            }
            if (const auto constructor_width = value_constructor_width(id->name)) {
                if (call->arguments.size() != *constructor_width) {
                    m_diagnostics.report_error(
                        "E3058",
                        std::format("{} expects {} numeric arguments, got {}", id->name, *constructor_width, call->arguments.size()),
                        call->span
                    );
                }
                for (const auto& argument : call->arguments) {
                    const auto argument_type = get_type_of_node(argument.get());
                    if (argument_type && !argument_type->is_numeric()) {
                        m_diagnostics.report_error(
                            "E3059",
                            std::format("{} constructor arguments must be numeric, got '{}'", id->name, argument_type->to_string()),
                            argument->span
                        );
                    }
                }
                m_node_types[call] = callee_t;
                return callee_t;
            }
            if (id->name == "Weak") {
                if (call->type_arguments.size() != 1 || call->arguments.size() != 1) {
                    m_diagnostics.report_error("E3084", "Weak<T>(value) requires one class type and one strong handle", call->span);
                }
                auto value_type = call->type_arguments.empty()
                    ? m_type_error : resolve_type_annotation(call->type_arguments.front(), scope.get());
                if (!value_type->is_error() && (value_type->kind != TypeKind::Class || value_type->is_behaviour)) {
                    m_diagnostics.report_error("E3084", "Weak<T> only supports non-Behaviour class handles", call->span);
                }
                if (!call->arguments.empty()) {
                    const auto actual = get_type_of_node(call->arguments.front().get());
                    if (actual && !actual->is_assignable_to(value_type)) {
                        m_diagnostics.report_error("E3084", std::format("Weak<{}> cannot be built from '{}'",
                            value_type->to_string(), actual->to_string()), call->span);
                    }
                }
                auto weak_type = Type::make_generic(TypeKind::Weak, "Weak", {value_type});
                m_node_types[call] = weak_type;
                return weak_type;
            }
            const auto called_symbol = scope->lookup(id->name);
            if (called_symbol) {
                if (const auto* function = dynamic_cast<const FunctionDecl*>(called_symbol->ast_node.get())) {
                    normalize_authored(function->params, function->name);
                    std::unordered_map<std::string, std::shared_ptr<Type>> bindings;
                    if (!call->type_arguments.empty() && call->type_arguments.size() != function->generic_params.size()) {
                        m_diagnostics.report_error("E3077", std::format("Generic function '{}' expects {} type arguments, got {}",
                            function->name, function->generic_params.size(), call->type_arguments.size()), call->span);
                    }
                    for (std::size_t index = 0; index < call->type_arguments.size() && index < function->generic_params.size(); ++index) {
                        bindings[function->generic_params[index].name] = resolve_type_annotation(call->type_arguments[index], scope.get());
                    }
                    for (std::size_t index = 0; index < call->arguments.size() && index < function->params.size(); ++index) {
                        const auto& annotation = function->params[index]->type_annotation;
                        if (std::any_of(function->generic_params.begin(), function->generic_params.end(),
                            [&annotation](const GenericParam& parameter) { return parameter.name == annotation.name; })) {
                            const auto argument_type = get_type_of_node(call->arguments[index].get());
                            const auto [it, inserted] = bindings.emplace(annotation.name, argument_type);
                            if (!inserted && argument_type && !argument_type->is_assignable_to(it->second)) {
                                m_diagnostics.report_error("E3077", std::format("Cannot infer one '{}' type for generic call", annotation.name), call->arguments[index]->span);
                            }
                        }
                    }
                    for (const auto& parameter : function->generic_params) {
                        const auto concrete = bindings.find(parameter.name);
                        if (concrete == bindings.end()) {
                            m_diagnostics.report_error("E3077", std::format("Cannot infer generic type '{}' for '{}'", parameter.name, function->name), call->span);
                            continue;
                        }
                        if (parameter.constraint) {
                            const std::string constraint = parameter.constraint->name;
                            const auto& type = concrete->second;
                            bool satisfied = std::find(type->interfaces.begin(), type->interfaces.end(), constraint) != type->interfaces.end();
                            if (constraint == "Equatable") satisfied = satisfied || type->is_numeric() || type->is_bool() || type->is_string() || type->kind == TypeKind::Enum;
                            if (constraint == "Ordered") satisfied = satisfied || type->is_numeric() || type->is_string() || type->kind == TypeKind::Enum;
                            if (constraint == "Send") satisfied = satisfied || type->is_numeric() || type->is_bool() || type->is_string() ||
                                type->kind == TypeKind::Enum || type->kind == TypeKind::Struct || type->kind == TypeKind::Array ||
                                type->kind == TypeKind::Deque || type->kind == TypeKind::FixedArray ||
                                type->kind == TypeKind::Map || type->kind == TypeKind::Set;
                            if (!satisfied) {
                                m_diagnostics.report_error("E3078", std::format("Type '{}' does not satisfy constraint '{}'",
                                    type->to_string(), constraint), call->span);
                            }
                        }
                    }
                    std::function<std::shared_ptr<Type>(const TypeAnnotation&)> resolve_bound = [&](const TypeAnnotation& annotation) {
                        const auto bound = bindings.find(annotation.name);
                        if (bound != bindings.end() && annotation.generic_args.empty()) return bound->second;
                        auto resolved = resolve_type_annotation(annotation, scope.get());
                        if (!annotation.generic_args.empty()) {
                            resolved = std::make_shared<Type>(*resolved);
                            resolved->type_args.clear();
                            for (const auto& argument : annotation.generic_args) {
                                if (!argument.constant_value) resolved->type_args.push_back(resolve_bound(argument));
                            }
                        }
                        return resolved;
                    };
                    for (std::size_t index = 0; index < call->arguments.size() && index < function->params.size(); ++index) {
                        const auto expected = resolve_bound(function->params[index]->type_annotation);
                        const auto actual = get_type_of_node(call->arguments[index].get());
                        if (actual && !actual->is_assignable_to(expected)) {
                            m_diagnostics.report_error("E3049", std::format("{} argument '{}' expects '{}', got '{}'", function->name,
                                function->params[index]->name, expected->to_string(), actual->to_string()), call->arguments[index]->span);
                        }
                    }
                    const auto result = function->return_type ? resolve_bound(*function->return_type) : m_type_void;
                    m_node_types[call] = result;
                    return result;
                }
                if (const auto* class_decl = dynamic_cast<const ClassDecl*>(called_symbol->ast_node.get())) {
                    if (class_decl->is_behaviour()) {
                        m_diagnostics.report_error("E3079", std::format("Behaviour '{}' is entity-owned and cannot be constructed", class_decl->name), call->span);
                    }
                    const auto& parameters = class_decl->constructor_decl
                        ? class_decl->constructor_decl->params : std::vector<std::shared_ptr<ParamDecl>>{};
                    normalize_authored(parameters, class_decl->name + " constructor");
                    if (call->arguments.size() != parameters.size()) {
                        m_diagnostics.report_error("E3079", std::format("{} constructor expects {} arguments, got {}",
                            class_decl->name, parameters.size(), call->arguments.size()), call->span);
                    }
                    for (std::size_t index = 0; index < call->arguments.size() && index < parameters.size(); ++index) {
                        const bool generic_parameter = std::any_of(class_decl->generic_params.begin(), class_decl->generic_params.end(),
                            [&parameters, index](const GenericParam& candidate) {
                                return candidate.name == parameters[index]->type_annotation.name;
                            });
                        const auto expected = resolve_type_annotation(parameters[index]->type_annotation, scope.get());
                        const auto actual = get_type_of_node(call->arguments[index].get());
                        if (!generic_parameter && actual && !actual->is_assignable_to(expected)) {
                            m_diagnostics.report_error("E3079", std::format("Constructor argument '{}' expects '{}', got '{}'",
                                parameters[index]->name, expected->to_string(), actual->to_string()), call->arguments[index]->span);
                        }
                    }
                    if (!call->type_arguments.empty() && call->type_arguments.size() != class_decl->generic_params.size()) {
                        m_diagnostics.report_error("E3077", std::format("Generic class '{}' expects {} type arguments, got {}",
                            class_decl->name, class_decl->generic_params.size(), call->type_arguments.size()), call->span);
                    }
                    TypeAnnotation constructed_annotation{class_decl->name};
                    constructed_annotation.generic_args = call->type_arguments;
                    if (constructed_annotation.generic_args.empty() && !class_decl->generic_params.empty()) {
                        std::unordered_map<std::string, TypeAnnotation> inferred;
                        for (std::size_t index = 0; index < call->arguments.size() && index < parameters.size(); ++index) {
                            const std::string& parameter_name = parameters[index]->type_annotation.name;
                            const auto generic = std::find_if(class_decl->generic_params.begin(), class_decl->generic_params.end(),
                                [&parameter_name](const GenericParam& candidate) { return candidate.name == parameter_name; });
                            if (generic == class_decl->generic_params.end()) continue;
                            const auto argument_type = get_type_of_node(call->arguments[index].get());
                            if (argument_type) inferred.emplace(parameter_name, parse_type_text(argument_type->to_string()));
                        }
                        for (const auto& generic : class_decl->generic_params) {
                            if (const auto found = inferred.find(generic.name); found != inferred.end()) {
                                constructed_annotation.generic_args.push_back(found->second);
                            }
                        }
                    }
                    auto result = resolve_type_annotation(constructed_annotation, scope.get());
                    for (std::size_t index = 0;
                         index < class_decl->generic_params.size() && index < result->type_args.size(); ++index) {
                        const auto& parameter = class_decl->generic_params[index];
                        if (!parameter.constraint) continue;
                        const auto& concrete = result->type_args[index];
                        const std::string& constraint = parameter.constraint->name;
                        bool satisfied = std::find(
                            concrete->interfaces.begin(), concrete->interfaces.end(), constraint
                        ) != concrete->interfaces.end();
                        if (constraint == "Send") satisfied = satisfied || is_send_type(concrete);
                        if (constraint == "Equatable") {
                            satisfied = satisfied || concrete->is_numeric() || concrete->is_bool() ||
                                concrete->is_string() || concrete->kind == TypeKind::Enum;
                        }
                        if (constraint == "Ordered") {
                            satisfied = satisfied || concrete->is_numeric() || concrete->is_string() ||
                                concrete->kind == TypeKind::Enum;
                        }
                        if (!satisfied) {
                            m_diagnostics.report_error(
                                "E3078",
                                std::format("Type '{}' does not satisfy constraint '{}'", concrete->to_string(), constraint),
                                call->span
                            );
                        }
                    }
                    m_node_types[call] = result;
                    return result;
                }
            }
            if (callee_t && callee_t->name == "Log") {
                if (call->arguments.size() != 1) {
                    m_diagnostics.report_error(
                        "E3033",
                        std::format("Log expects 1 String argument, got {}", call->arguments.size()),
                        call->span
                    );
                } else {
                    const auto argument_type = get_type_of_node(call->arguments.front().get());
                    if (!argument_type || !argument_type->is_string()) {
                        m_diagnostics.report_error(
                            "E3034",
                            "Log expects a String argument",
                            call->arguments.front()->span
                        );
                    }
                }
                m_node_types[call] = m_type_void;
                return m_type_void;
            }
            if (id->name == "delay" || id->name == "realDelay" || id->name == "nextFrame" || id->name == "fixedUpdate") {
                auto task_t = Type::make_generic(TypeKind::Task, "Task", {m_type_void});
                m_node_types[call] = task_t;
                return task_t;
            }
            if (const ApiFunctionDescriptor* function =
                    ApiCatalog::instance().find_available_function(id->name)) {
                normalize_catalog(function->parameters, function->name);
                if (call->arguments.size() != function->parameters.size()) {
                    m_diagnostics.report_error(
                        "E3048",
                        std::format("{} expects {} arguments, got {}", function->name,
                            function->parameters.size(), call->arguments.size()),
                        call->span
                    );
                }
                for (std::size_t index = 0;
                     index < call->arguments.size() && index < function->parameters.size(); ++index) {
                    TypeAnnotation parameter_annotation = parse_type_text(function->parameters[index].type);
                    const auto parameter_type = resolve_type_annotation(parameter_annotation, scope.get());
                    const auto argument_type = get_type_of_node(call->arguments[index].get());
                    if (argument_type && !argument_type->is_assignable_to(parameter_type)) {
                        m_diagnostics.report_error(
                            "E3049",
                            std::format("{} argument '{}' expects '{}', got '{}'", function->name,
                                function->parameters[index].name, parameter_type->to_string(),
                                argument_type->to_string()),
                            call->arguments[index]->span
                        );
                    }
                }
                if (id->name != "delay" && id->name != "realDelay" &&
                    id->name != "nextFrame" && id->name != "fixedUpdate") {
                    TypeAnnotation return_annotation = parse_type_text(function->return_type);
                    auto return_type = resolve_type_annotation(return_annotation, scope.get());
                    m_node_types[call] = return_type;
                    return return_type;
                }
            }
        }

        if (callee_t->kind == TypeKind::Function) {
            if (call->arguments.size() != callee_t->param_types.size()) {
                m_diagnostics.report_error(
                    "E3054",
                    std::format("Method expects {} arguments, got {}", callee_t->param_types.size(), call->arguments.size()),
                    call->span
                );
            }
            for (std::size_t index = 0; index < call->arguments.size() && index < callee_t->param_types.size(); ++index) {
                const auto argument_type = get_type_of_node(call->arguments[index].get());
                if (argument_type && !argument_type->is_assignable_to(callee_t->param_types[index])) {
                    m_diagnostics.report_error(
                        "E3055",
                        std::format("Argument {} expects '{}', got '{}'", index + 1,
                            callee_t->param_types[index]->to_string(), argument_type->to_string()),
                        call->arguments[index]->span
                    );
                }
            }
        }
        if (callee_t->return_type) {
            m_node_types[call] = callee_t->return_type;
            return callee_t->return_type;
        }

        m_node_types[call] = m_type_void;
        return m_type_void;
    }

    if (auto mem = dynamic_cast<MemberAccessExpr*>(expr.get())) {
        auto obj_t = check_expression(mem->object, scope);

        if (obj_t->is_nullable && !mem->is_optional && !obj_t->is_error()) {
            m_diagnostics.report_error("E3067", std::format("Nullable value '{}' requires optional access '?.' or a null check",
                obj_t->to_string()), mem->span);
        }

        std::string catalog_owner = obj_t->name;
        if (obj_t->name == "ComponentRef" && obj_t->type_args.size() == 1) {
            catalog_owner = obj_t->type_args.front()->name;
        }
        if (obj_t->kind == TypeKind::String) catalog_owner = "String";
        if (obj_t->kind == TypeKind::Entity) catalog_owner = "Entity";
        if (obj_t->kind == TypeKind::Vec2) catalog_owner = "Vec2";
        if (obj_t->kind == TypeKind::Vec3) catalog_owner = "Vec3";
        if (obj_t->kind == TypeKind::Vec4) catalog_owner = "Vec4";
        if (obj_t->kind == TypeKind::Color) catalog_owner = "Color";
        if (obj_t->kind == TypeKind::Rect) catalog_owner = "Rect";
        if (obj_t->kind == TypeKind::Bounds2) catalog_owner = "Bounds2";
        if (obj_t->kind == TypeKind::Array) catalog_owner = "Array";
        if (obj_t->kind == TypeKind::Deque) catalog_owner = "Deque";
        if (obj_t->kind == TypeKind::FixedArray) catalog_owner = "FixedArray";
        if (obj_t->kind == TypeKind::Map) catalog_owner = "Map";
        if (obj_t->kind == TypeKind::Set) catalog_owner = "Set";
        if ((obj_t->kind == TypeKind::Array || obj_t->kind == TypeKind::Deque || obj_t->kind == TypeKind::FixedArray ||
             obj_t->kind == TypeKind::Map || obj_t->kind == TypeKind::Set) &&
            (mem->member_name == "length" || mem->member_name == "size")) {
            m_node_types[mem] = m_type_int;
            return m_type_int;
        }
        if (obj_t->kind == TypeKind::Deque && mem->member_name == "isEmpty") {
            m_node_types[mem] = m_type_bool;
            return m_type_bool;
        }
        if (const ApiPropertyDescriptor* property = ApiCatalog::instance().find_available_property(catalog_owner, mem->member_name)) {
            if (!property->readable) {
                m_diagnostics.report_error("E3046", std::format("{}.{} is write-only", catalog_owner, mem->member_name), mem->span);
            }
            TypeAnnotation annotation = parse_type_text(property->type);
            auto property_type = resolve_type_annotation(annotation, scope.get());
            if (property->nullable) {
                property_type = std::make_shared<Type>(*property_type);
                property_type->is_nullable = true;
            }
            m_node_types[mem] = property_type;
            return property_type;
        }
        // A member access is checked before its enclosing CallExpr.  Catalog
        // methods are public engine API, so expose their callable shape here
        // instead of reporting them as missing class fields.
        if (const ApiMethodDescriptor* method = ApiCatalog::instance().find_available_method(catalog_owner, mem->member_name)) {
            auto function_type = Type::make_custom(TypeKind::Function, mem->member_name);
            const auto resolve_method_type = [&](const std::string& type_name) {
                if (type_name == "T" && !obj_t->type_args.empty()) return obj_t->type_args.front();
                return resolve_type_annotation(parse_type_text(type_name), scope.get());
            };
            function_type->return_type = resolve_method_type(method->return_type);
            if (method->nullable_return) {
                function_type->return_type = std::make_shared<Type>(*function_type->return_type);
                function_type->return_type->is_nullable = true;
            }
            for (const auto& parameter : method->parameters) {
                function_type->param_types.push_back(resolve_method_type(parameter.type));
            }
            m_node_types[mem] = function_type;
            return function_type;
        }
        // Result is a language value type rather than a catalog component.
        // Its static constructors and instance methods are nevertheless public
        // callable members, and CallExpr validates their generic contracts.
        if ((obj_t->name == "Result" && (mem->member_name == "success" || mem->member_name == "failure")) ||
            (obj_t->kind == TypeKind::Result &&
             (mem->member_name == "isOk" || mem->member_name == "isError" ||
              mem->member_name == "value" || mem->member_name == "error"))) {
            auto function_type = Type::make_custom(TypeKind::Function, mem->member_name);
            if (mem->member_name == "isOk" || mem->member_name == "isError") function_type->return_type = m_type_bool;
            else if (obj_t->kind == TypeKind::Result && obj_t->type_args.size() == 2) {
                function_type->return_type = obj_t->type_args[mem->member_name == "value" ? 0 : 1];
            } else {
                function_type->return_type = m_type_void;
            }
            m_node_types[mem] = function_type;
            return function_type;
        }
        const bool collection_method =
            (obj_t->kind == TypeKind::Array &&
             (mem->member_name == "push" || mem->member_name == "contains" ||
              mem->member_name == "remove" || mem->member_name == "pop" || mem->member_name == "clear" ||
              mem->member_name == "indexOf" || mem->member_name == "lastIndexOf" ||
              mem->member_name == "removeAt" || mem->member_name == "reverse" || mem->member_name == "swap" ||
              mem->member_name == "shuffle" || mem->member_name == "choose" || mem->member_name == "map" ||
              mem->member_name == "filter" || mem->member_name == "reduce" || mem->member_name == "sortBy")) ||
            (obj_t->kind == TypeKind::Deque &&
             (mem->member_name == "pushFront" || mem->member_name == "pushBack" ||
              mem->member_name == "front" || mem->member_name == "back" ||
              mem->member_name == "popFront" || mem->member_name == "popBack" ||
              mem->member_name == "clear")) ||
            (obj_t->kind == TypeKind::Set &&
             (mem->member_name == "add" || mem->member_name == "contains" ||
              mem->member_name == "remove" || mem->member_name == "clear")) ||
            (obj_t->kind == TypeKind::Map &&
             (mem->member_name == "set" || mem->member_name == "get" ||
              mem->member_name == "containsKey" || mem->member_name == "remove" ||
              mem->member_name == "keys" || mem->member_name == "values" ||
              mem->member_name == "clear"));
        if (collection_method) {
            auto function_type = Type::make_custom(TypeKind::Function, mem->member_name);
            function_type->return_type = m_type_void;
            m_node_types[mem] = function_type;
            return function_type;
        }
        if (obj_t->name == "Assets" && mem->member_name == "load") {
            auto function_type = Type::make_custom(TypeKind::Function, "load");
            auto asset_type = Type::make_custom(TypeKind::Class, "Asset");
            auto result_type = Type::make_generic(TypeKind::Result, "Result", {asset_type, m_type_string});
            function_type->return_type = Type::make_generic(TypeKind::Task, "Task", {result_type});
            function_type->param_types.push_back(m_type_string);
            m_node_types[mem] = function_type;
            return function_type;
        }
        if (obj_t->name == "Events" && (mem->member_name == "emit" || mem->member_name == "next")) {
            auto function_type = Type::make_custom(TypeKind::Function, mem->member_name);
            function_type->return_type = mem->member_name == "emit"
                ? m_type_bool
                : Type::make_generic(TypeKind::Task, "Task", {m_type_error});
            m_node_types[mem] = function_type;
            return function_type;
        }
        if (obj_t->name == "Jobs" && mem->member_name == "run") {
            auto function_type = Type::make_custom(TypeKind::Function, "run");
            function_type->return_type = Type::make_generic(TypeKind::Task, "Task", {m_type_error});
            m_node_types[mem] = function_type;
            return function_type;
        }

        if (obj_t->kind == TypeKind::Enum) {
            const auto owner = scope->lookup(obj_t->name);
            if (owner) {
                const auto member = std::find_if(owner->members.begin(), owner->members.end(),
                    [mem](const auto& candidate) { return candidate && candidate->name == mem->member_name; });
                if (member != owner->members.end()) {
                    m_member_symbols[mem] = *member;
                    m_node_types[mem] = obj_t;
                    return obj_t;
                }
            }
            m_diagnostics.report_error("E3057", std::format("Enum '{}' has no member '{}'", obj_t->name, mem->member_name), mem->span);
            m_node_types[mem] = m_type_error;
            return m_type_error;
        }

        if (obj_t->kind == TypeKind::Class || obj_t->kind == TypeKind::Struct || obj_t->kind == TypeKind::Interface) {
            const auto owner = scope->lookup(obj_t->name);
            if (owner) {
                std::unordered_map<std::string, std::shared_ptr<Type>> owner_bindings;
                const auto bind_owner_parameters = [&](const auto& parameters) {
                    for (std::size_t index = 0; index < parameters.size() && index < obj_t->type_args.size(); ++index) {
                        owner_bindings[parameters[index].name] = obj_t->type_args[index];
                    }
                };
                if (const auto* declaration = dynamic_cast<const ClassDecl*>(owner->ast_node.get())) {
                    bind_owner_parameters(declaration->generic_params);
                } else if (const auto* declaration = dynamic_cast<const StructDecl*>(owner->ast_node.get())) {
                    bind_owner_parameters(declaration->generic_params);
                } else if (const auto* declaration = dynamic_cast<const InterfaceDecl*>(owner->ast_node.get())) {
                    bind_owner_parameters(declaration->generic_params);
                }
                std::function<std::shared_ptr<Type>(const TypeAnnotation&)> resolve_owner_type =
                    [&](const TypeAnnotation& annotation) {
                        if (const auto bound = owner_bindings.find(annotation.name);
                            bound != owner_bindings.end() && annotation.generic_args.empty()) {
                            auto result = std::make_shared<Type>(*bound->second);
                            result->is_nullable = annotation.is_nullable;
                            return result;
                        }
                        auto result = resolve_type_annotation(annotation, scope.get());
                        if (!annotation.generic_args.empty()) {
                            result = std::make_shared<Type>(*result);
                            result->type_args.clear();
                            for (const auto& argument : annotation.generic_args) {
                                if (!argument.constant_value) result->type_args.push_back(resolve_owner_type(argument));
                            }
                        }
                        return result;
                    };
                const auto member = std::find_if(
                    owner->members.begin(), owner->members.end(),
                    [mem](const std::shared_ptr<Symbol>& candidate) {
                        return candidate && candidate->name == mem->member_name;
                    }
                );
                if (member != owner->members.end()) {
                    m_member_symbols[mem] = *member;
                    bool accessed_through_type = false;
                    if (const auto* owner_identifier = dynamic_cast<const IdentifierExpr*>(mem->object.get())) {
                        const auto owner_symbol = scope->lookup(owner_identifier->name);
                        accessed_through_type = owner_symbol &&
                            (owner_symbol->kind == SymbolKind::Class || owner_symbol->kind == SymbolKind::Struct ||
                             owner_symbol->kind == SymbolKind::Import);
                    }
                    if (accessed_through_type && !(*member)->is_static) {
                        m_diagnostics.report_error("E3146", std::format("Instance member '{}.{}' requires an instance", obj_t->name, mem->member_name), mem->span);
                    } else if (!accessed_through_type && (*member)->is_static) {
                        m_diagnostics.report_error("E3147", std::format("Static member '{}.{}' must be accessed through its type", obj_t->name, mem->member_name), mem->span);
                    }
                    if ((*member)->access != AccessModifier::Public && obj_t->name != m_current_class_name) {
                        m_diagnostics.report_error(
                            "E3056",
                            std::format("{}.{} is not public", obj_t->name, mem->member_name),
                            mem->span
                        );
                    }
                    if ((*member)->kind == SymbolKind::Method) {
                        auto function_type = Type::make_custom(TypeKind::Function, mem->member_name);
                        if (const auto* method = dynamic_cast<const MethodDecl*>((*member)->ast_node.get())) {
                            function_type->return_type = method->return_type
                                ? resolve_owner_type(*method->return_type)
                                : m_type_void;
                            for (const auto& parameter : method->params) {
                                function_type->param_types.push_back(
                                    resolve_owner_type(parameter->type_annotation)
                                );
                            }
                        } else {
                            TypeAnnotation annotation = parse_type_text((*member)->type_string);
                            function_type->return_type = resolve_owner_type(annotation);
                        }
                        m_node_types[mem] = function_type;
                        return function_type;
                    }
                    std::shared_ptr<Type> field_type;
                    if (const auto* property = dynamic_cast<const PropertyDecl*>((*member)->ast_node.get())) {
                        field_type = property->type_annotation
                            ? resolve_owner_type(*property->type_annotation)
                            : get_type_of_node(property);
                    }
                    if (!field_type || field_type->is_error()) {
                        TypeAnnotation annotation = parse_type_text((*member)->type_string);
                        field_type = resolve_owner_type(annotation);
                    }
                    if (mem->is_optional) {
                        field_type = std::make_shared<Type>(*field_type);
                        field_type->is_nullable = true;
                    }
                    m_node_types[mem] = field_type;
                    return field_type;
                }
            }
            m_diagnostics.report_error(
                "E3057",
                std::format("{} has no public member '{}'", obj_t->name, mem->member_name),
                mem->span
            );
            m_node_types[mem] = m_type_error;
            return m_type_error;
        }

        m_diagnostics.report_error("E3057", std::format("Type '{}' has no member '{}'", obj_t->to_string(), mem->member_name), mem->span);
        m_node_types[mem] = m_type_error;
        return m_type_error;
    }

    if (auto idx = dynamic_cast<IndexExpr*>(expr.get())) {
        auto obj_t = check_expression(idx->object, scope);
        auto idx_t = check_expression(idx->index, scope);

        if ((obj_t->kind == TypeKind::Array || obj_t->kind == TypeKind::FixedArray) && !obj_t->type_args.empty()) {
            if (!idx_t->is_integer() && !idx_t->is_error()) {
                m_diagnostics.report_error("E3075", "Array indices must be integers", idx->index->span);
            }
            m_node_types[idx] = obj_t->type_args[0];
            return obj_t->type_args[0];
        }
        if (obj_t->kind == TypeKind::Map && obj_t->type_args.size() == 2) {
            if (!idx_t->is_assignable_to(obj_t->type_args[0])) {
                m_diagnostics.report_error("E3075", std::format("Map key expects '{}', got '{}'",
                    obj_t->type_args[0]->to_string(), idx_t->to_string()), idx->index->span);
            }
            auto value = std::make_shared<Type>(*obj_t->type_args[1]);
            value->is_nullable = true;
            m_node_types[idx] = value;
            return value;
        }
        m_node_types[idx] = m_type_error;
        return m_type_error;
    }

    if (auto arr = dynamic_cast<ArrayLiteralExpr*>(expr.get())) {
        std::shared_ptr<Type> elem_t = m_type_error;
        for (const auto& el : arr->elements) {
            const auto current = check_expression(el, scope);
            if (elem_t->is_error()) elem_t = current;
            else if (!current->is_assignable_to(elem_t) && !elem_t->is_assignable_to(current)) {
                m_diagnostics.report_error("E3072", std::format("Array literal mixes incompatible element types '{}' and '{}'",
                    elem_t->to_string(), current->to_string()), el->span);
                elem_t = m_type_error;
            } else if (elem_t->is_assignable_to(current)) {
                elem_t = current;
            }
        }
        auto arr_t = Type::make_generic(TypeKind::Array, "Array", {elem_t});
        m_node_types[arr] = arr_t;
        return arr_t;
    }

    if (auto collection = dynamic_cast<CollectionLiteralExpr*>(expr.get())) {
        if (collection->kind == CollectionLiteralExpr::Kind::Map) {
            std::shared_ptr<Type> key_type = m_type_error;
            std::shared_ptr<Type> value_type = m_type_error;
            for (const auto& [key, value] : collection->map_entries) {
                const auto current_key = check_expression(key, scope);
                const auto current_value = check_expression(value, scope);
                if (key_type->is_error()) key_type = current_key;
                else if (!current_key->is_assignable_to(key_type)) {
                    m_diagnostics.report_error("E3073", "Map literal keys must have one compatible type", key->span);
                }
                if (value_type->is_error()) value_type = current_value;
                else if (!current_value->is_assignable_to(value_type)) {
                    m_diagnostics.report_error("E3073", "Map literal values must have one compatible type", value->span);
                }
            }
            auto type = Type::make_generic(TypeKind::Map, "Map", {key_type, value_type});
            m_node_types[collection] = type;
            return type;
        }
        std::shared_ptr<Type> element_type = m_type_error;
        for (const auto& element : collection->elements) {
            const auto current = check_expression(element, scope);
            if (element_type->is_error()) element_type = current;
            else if (!current->is_assignable_to(element_type)) {
                m_diagnostics.report_error("E3073", "Collection literal elements must have one compatible type", element->span);
            }
        }
        const TypeKind kind = collection->kind == CollectionLiteralExpr::Kind::Set
            ? TypeKind::Set : TypeKind::FixedArray;
        auto type = Type::make_generic(kind, kind == TypeKind::Set ? "Set" : "FixedArray", {element_type});
        if (kind == TypeKind::FixedArray) type->fixed_array_size = collection->elements.size();
        m_node_types[collection] = type;
        return type;
    }

    if (auto object = dynamic_cast<ObjectLiteralExpr*>(expr.get())) {
        return check_object_literal(*object, scope, nullptr);
    }

    if (auto arrow = dynamic_cast<ArrowFunctionExpr*>(expr.get())) {
        auto type = Type::make_custom(TypeKind::Function, "<closure>");
        auto function_scope = std::make_shared<Scope>(ScopeKind::Function, scope);
        const auto previous_narrowing = m_narrowed_nonnull;
        for (const auto& parameter : arrow->params) {
            const auto parameter_type = resolve_type_annotation(parameter->type_annotation, scope.get());
            type->param_types.push_back(parameter_type);
            auto symbol = std::make_shared<Symbol>();
            symbol->name = parameter->name;
            symbol->kind = SymbolKind::Parameter;
            symbol->type_string = parameter->type_annotation.to_string();
            function_scope->define(symbol);
        }
        if (arrow->body_expr) type->return_type = check_expression(arrow->body_expr, function_scope);
        else {
            type->return_type = arrow->return_type
                ? resolve_type_annotation(*arrow->return_type, scope.get()) : m_type_void;
            check_statement(arrow->body_stmt, function_scope, type->return_type, false);
        }
        m_narrowed_nonnull = previous_narrowing;
        m_node_types[arrow] = type;
        return type;
    }

    if (auto self = dynamic_cast<ThisExpr*>(expr.get())) {
        if (!m_current_self_type || m_in_static_method) {
            m_diagnostics.report_error("E3042", "'this' is only valid in an instance member", self->span);
            m_node_types[self] = m_type_error;
            return m_type_error;
        }
        m_node_types[self] = m_current_self_type;
        return m_current_self_type;
    }

    if (auto aw = dynamic_cast<AwaitExpr*>(expr.get())) {
        auto op_t = check_expression(aw->operand, scope);
        if (op_t->kind == TypeKind::Task && !op_t->type_args.empty()) {
            m_node_types[aw] = op_t->type_args[0];
            return op_t->type_args[0];
        }
        if (!op_t->is_error()) m_diagnostics.report_error("E3076", std::format("Type '{}' is not awaitable", op_t->to_string()), aw->span);
        m_node_types[aw] = m_type_error;
        return m_type_error;
    }

    return m_type_error;
}

} // namespace EngineScript
