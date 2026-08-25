#include "TypeChecker.hpp"
#include "../api/ApiCatalog.hpp"
#include <format>

namespace EngineScript {

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

std::shared_ptr<Type> TypeChecker::get_type_of_node(const ASTNode* node) const {
    auto it = m_node_types.find(node);
    if (it != m_node_types.end()) return it->second;
    return m_type_error;
}

std::vector<InferredTypeInfo> TypeChecker::inferred_types() const {
    std::vector<InferredTypeInfo> inferred;
    inferred.reserve(m_node_types.size());
    for (const auto& [node, type] : m_node_types) {
        if (!node || !type || !node->span.is_valid() || type->is_error()) continue;
        inferred.push_back({node->span, type->to_string()});
    }
    return inferred;
}

std::shared_ptr<Type> TypeChecker::resolve_type_annotation(const TypeAnnotation& annot, const Scope* scope) const {
    std::shared_ptr<Type> t;
    if (annot.name == "Void") t = m_type_void;
    else if (annot.name == "Bool") t = m_type_bool;
    else if (annot.name == "Int" || annot.name == "Int32") t = m_type_int;
    else if (annot.name == "Int8") t = Type::make_primitive(TypeKind::Int8);
    else if (annot.name == "Int16") t = Type::make_primitive(TypeKind::Int16);
    else if (annot.name == "Int64") t = Type::make_primitive(TypeKind::Int64);
    else if (annot.name == "UInt" || annot.name == "UInt32") t = Type::make_primitive(TypeKind::UInt32);
    else if (annot.name == "UInt8") t = Type::make_primitive(TypeKind::UInt8);
    else if (annot.name == "UInt16") t = Type::make_primitive(TypeKind::UInt16);
    else if (annot.name == "UInt64") t = Type::make_primitive(TypeKind::UInt64);
    else if (annot.name == "Float" || annot.name == "Float32") t = m_type_float;
    else if (annot.name == "Float64") t = Type::make_primitive(TypeKind::Float64);
    else if (annot.name == "String") t = m_type_string;
    else if (annot.name == "Vec2") t = Type::make_primitive(TypeKind::Vec2);
    else if (annot.name == "Vec3") t = Type::make_primitive(TypeKind::Vec3);
    else if (annot.name == "Vec4") t = Type::make_primitive(TypeKind::Vec4);
    else if (annot.name == "Color") t = Type::make_primitive(TypeKind::Color);
    else if (annot.name == "Duration") t = m_type_duration;
    else if (annot.name == "Entity") t = m_type_entity;
    else if (annot.name == "Array") {
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) {
            args.push_back(resolve_type_annotation(a, scope));
        }
        t = Type::make_generic(TypeKind::Array, "Array", args);
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
    } else {
        // User class / struct / interface / enum
        std::vector<std::shared_ptr<Type>> args;
        for (const auto& a : annot.generic_args) {
            args.push_back(resolve_type_annotation(a, scope));
        }
        t = Type::make_generic(TypeKind::Class, annot.name, args);
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

    if (auto cls = dynamic_cast<const ClassDecl*>(decl.get())) {
        check_class_decl(*cls, scope);
    } else if (auto st = dynamic_cast<const StructDecl*>(decl.get())) {
        check_struct_decl(*st, scope);
    } else if (auto fn = dynamic_cast<const FunctionDecl*>(decl.get())) {
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

    for (const auto& prop : cls.properties) {
        if (prop->type_annotation) {
            m_node_types[prop.get()] = resolve_type_annotation(*prop->type_annotation, cls_scope.get());
        }
        if (prop->initializer) {
            auto init_t = check_expression(prop->initializer, cls_scope);
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
        check_statement(cls.constructor_decl->body, ctor_scope, m_type_void, false);
    }

    for (const auto& m : cls.methods) {
        if (m->body) {
            std::shared_ptr<Type> ret_t = m->return_type ? resolve_type_annotation(*m->return_type, cls_scope.get()) : m_type_void;
            auto m_scope = std::make_shared<Scope>(ScopeKind::Function, cls_scope);
            check_statement(m->body, m_scope, ret_t, m->is_async);

            if (!ret_t->is_void() && !m->is_async) {
                if (!check_statement_returns(m->body)) {
                    m_diagnostics.report_error("E3003", std::format("Method '{}' does not return a value on all code paths", m->name), m->span);
                }
            }
        }
    }
}

void TypeChecker::check_struct_decl(const StructDecl& st, const std::shared_ptr<Scope>& scope) {
    auto st_scope = m_resolver.find_scope_for_node(&st);
    if (!st_scope) return;

    for (const auto& prop : st.properties) {
        if (prop->type_annotation) {
            m_node_types[prop.get()] = resolve_type_annotation(*prop->type_annotation, st_scope.get());
        }
        if (prop->initializer) {
            auto init_t = check_expression(prop->initializer, st_scope);
            if (prop->type_annotation) {
                auto decl_t = resolve_type_annotation(*prop->type_annotation, st_scope.get());
                if (!init_t->is_assignable_to(decl_t)) {
                    m_diagnostics.report_error("E3002", std::format("Property initializer type '{}' does not match declared type '{}'", init_t->to_string(), decl_t->to_string()), prop->initializer->span);
                }
            }
        }
    }
}

void TypeChecker::check_function_decl(const FunctionDecl& fn, const std::shared_ptr<Scope>& scope) {
    if (fn.body) {
        std::shared_ptr<Type> ret_t = fn.return_type ? resolve_type_annotation(*fn.return_type, scope.get()) : m_type_void;
        auto fn_scope = std::make_shared<Scope>(ScopeKind::Function, scope);
        check_statement(fn.body, fn_scope, ret_t, fn.is_async);

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
    return false;
}

void TypeChecker::check_statement(const std::shared_ptr<Statement>& stmt, const std::shared_ptr<Scope>& scope, const std::shared_ptr<Type>& expected_return, bool is_async) {
    if (!stmt) return;

    if (auto blk = dynamic_cast<BlockStmt*>(stmt.get())) {
        auto blk_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        for (const auto& s : blk->statements) {
            check_statement(s, blk_scope, expected_return, is_async);
        }
    } else if (auto v = dynamic_cast<VarDeclStmt*>(stmt.get())) {
        if (v->type_annotation) {
            m_node_types[v] = resolve_type_annotation(*v->type_annotation, scope.get());
        }
        if (v->initializer) {
            auto init_t = check_expression(v->initializer, scope);
            if (v->type_annotation) {
                auto decl_t = resolve_type_annotation(*v->type_annotation, scope.get());
                if (!init_t->is_assignable_to(decl_t)) {
                    m_diagnostics.report_error("E3010", std::format("Cannot initialize variable '{}' of type '{}' with value of type '{}'", v->name, decl_t->to_string(), init_t->to_string()), v->initializer->span);
                }
                m_node_types[v] = decl_t;
            } else {
                m_node_types[v] = init_t;
            }
        }
    } else if (auto e = dynamic_cast<ExprStmt*>(stmt.get())) {
        check_expression(e->expr, scope);
    } else if (auto if_s = dynamic_cast<IfStmt*>(stmt.get())) {
        auto cond_t = check_expression(if_s->condition, scope);
        if (!cond_t->is_bool() && !cond_t->is_error()) {
            m_diagnostics.report_error("E3011", std::format("If condition must be of type Bool, got '{}'", cond_t->to_string()), if_s->condition->span);
        }
        check_statement(if_s->then_branch, scope, expected_return, is_async);
        if (if_s->else_branch) check_statement(if_s->else_branch, scope, expected_return, is_async);
    } else if (auto w = dynamic_cast<WhileStmt*>(stmt.get())) {
        auto cond_t = check_expression(w->condition, scope);
        if (!cond_t->is_bool() && !cond_t->is_error()) {
            m_diagnostics.report_error("E3012", std::format("While condition must be of type Bool, got '{}'", cond_t->to_string()), w->condition->span);
        }
        check_statement(w->body, scope, expected_return, is_async);
    } else if (auto f = dynamic_cast<ForStmt*>(stmt.get())) {
        auto for_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        check_statement(f->initializer, for_scope, expected_return, is_async);
        if (f->condition) {
            auto cond_t = check_expression(f->condition, for_scope);
            if (!cond_t->is_bool() && !cond_t->is_error()) {
                m_diagnostics.report_error("E3013", std::format("For condition must be of type Bool, got '{}'", cond_t->to_string()), f->condition->span);
            }
        }
        if (f->update) check_expression(f->update, for_scope);
        check_statement(f->body, for_scope, expected_return, is_async);
    } else if (auto for_in = dynamic_cast<ForInStmt*>(stmt.get())) {
        auto for_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        auto coll_t = check_expression(for_in->collection, for_scope);
        if (coll_t->kind != TypeKind::Array && coll_t->kind != TypeKind::Set && !coll_t->is_error()) {
            m_diagnostics.report_error("E3014", std::format("Type '{}' is not iterable in for-in", coll_t->to_string()), for_in->collection->span);
        }
        check_statement(for_in->body, for_scope, expected_return, is_async);
    } else if (auto ret = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (ret->value) {
            auto val_t = check_expression(ret->value, scope);
            if (expected_return) {
                // If async, expected return is Task<T> or T
                std::shared_ptr<Type> target_return = expected_return;
                if (is_async && expected_return->kind == TypeKind::Task && !expected_return->type_args.empty()) {
                    target_return = expected_return->type_args[0];
                }

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

    if (auto id = dynamic_cast<IdentifierExpr*>(expr.get())) {
        auto sym = m_resolver.find_symbol_for_node(id);
        if (!sym) {
            sym = scope->lookup(id->name);
        }

        if (sym) {
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

            if (sym->kind == SymbolKind::Function) {
                if (const ApiFunctionDescriptor* function =
                        ApiCatalog::instance().find_available_function(id->name)) {
                    auto t = Type::make_custom(TypeKind::Function, function->name);
                    TypeAnnotation return_annotation{function->return_type};
                    t->return_type = resolve_type_annotation(return_annotation, scope.get());
                    for (const ApiParameterDescriptor& parameter : function->parameters) {
                        TypeAnnotation parameter_annotation{parameter.type};
                        t->param_types.push_back(resolve_type_annotation(parameter_annotation, scope.get()));
                    }
                    m_node_types[id] = t;
                    return t;
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

            TypeAnnotation annot;
            annot.name = sym->type_string;
            auto t = resolve_type_annotation(annot, scope.get());
            m_node_types[id] = t;
            return t;
        }
        return m_type_error;
    }

    if (auto bin = dynamic_cast<BinaryExpr*>(expr.get())) {
        auto left_t = check_expression(bin->left, scope);
        auto right_t = check_expression(bin->right, scope);

        // Arithmetic
        if (bin->op == TokenType::Plus || bin->op == TokenType::Minus || bin->op == TokenType::Star || bin->op == TokenType::Slash || bin->op == TokenType::Percent) {
            if (left_t->is_string() && bin->op == TokenType::Plus) {
                m_node_types[bin] = m_type_string;
                return m_type_string;
            }
            if (left_t->is_numeric() && right_t->is_numeric()) {
                std::shared_ptr<Type> res_t = (left_t->is_float() || right_t->is_float()) ? m_type_float : m_type_int;
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
            m_node_types[bin] = m_type_bool;
            return m_type_bool;
        }

        // Null coalescing (a ?? b)
        if (bin->op == TokenType::QuestionQuestion) {
            auto unwrapped = std::make_shared<Type>(*left_t);
            unwrapped->is_nullable = false;
            m_node_types[bin] = unwrapped;
            return unwrapped;
        }

        m_node_types[bin] = left_t;
        return left_t;
    }

    if (auto un = dynamic_cast<UnaryExpr*>(expr.get())) {
        auto op_t = check_expression(un->operand, scope);
        if (un->op == TokenType::Exclamation) {
            m_node_types[un] = m_type_bool;
            return m_type_bool;
        }
        m_node_types[un] = op_t;
        return op_t;
    }

    if (auto assign = dynamic_cast<AssignmentExpr*>(expr.get())) {
        if (!dynamic_cast<IdentifierExpr*>(assign->target.get()) && !dynamic_cast<MemberAccessExpr*>(assign->target.get())) {
            m_diagnostics.report_error("E3043", "Only variables and properties are assignable in the native EngineScript backend", assign->target->span);
            m_node_types[assign] = m_type_error;
            return m_type_error;
        }
        auto target_t = check_expression(assign->target, scope);
        auto val_t = check_expression(assign->value, scope);
        if (auto member = dynamic_cast<MemberAccessExpr*>(assign->target.get())) {
            const auto owner_t = get_type_of_node(member->object.get());
            std::string owner_name = owner_t ? owner_t->name : "";
            if (owner_t && owner_t->kind == TypeKind::String) owner_name = "String";
            if (owner_t && owner_t->kind == TypeKind::Entity) owner_name = "Entity";
            if (const ApiPropertyDescriptor* property = ApiCatalog::instance().find_available_property(owner_name, member->member_name);
                property && !property->writable) {
                m_diagnostics.report_error("E3044", std::format("{}.{} is read-only", owner_name, member->member_name), member->span);
            }
        }
        if (!val_t->is_assignable_to(target_t)) {
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

        // Handle known engine builtin calls & methods
        if (auto mem = dynamic_cast<MemberAccessExpr*>(call->callee.get())) {
            auto target_t = get_type_of_node(mem->object.get());
            if (target_t) {
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
                    auto asset_type = Type::make_generic(TypeKind::Class, "Asset", {});
                    auto task_type = Type::make_generic(TypeKind::Task, "Task", {asset_type});
                    m_node_types[call] = task_type;
                    return task_type;
                }
                std::string catalog_owner = target_t->name;
                if (target_t->kind == TypeKind::String) catalog_owner = "String";
                if (target_t->kind == TypeKind::Entity) catalog_owner = "Entity";
                if (target_t->kind == TypeKind::Array) catalog_owner = "Array";
                if (const ApiMethodDescriptor* method = ApiCatalog::instance().find_available_method(
                        catalog_owner, mem->member_name)) {
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
                        TypeAnnotation parameter_annotation{method->parameters[index].type};
                        const auto parameter_type = resolve_type_annotation(parameter_annotation, scope.get());
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
                    TypeAnnotation return_annotation;
                    return_annotation.name = method->return_type;
                    auto return_type = resolve_type_annotation(return_annotation, scope.get());
                    return_type->is_nullable = method->nullable_return;
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

            if (mem->member_name == "push" || mem->member_name == "insert" || mem->member_name == "remove") {
                m_node_types[call] = m_type_void;
                return m_type_void;
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
                    TypeAnnotation parameter_annotation{function->parameters[index].type};
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
                    TypeAnnotation return_annotation{function->return_type};
                    auto return_type = resolve_type_annotation(return_annotation, scope.get());
                    m_node_types[call] = return_type;
                    return return_type;
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

        std::string catalog_owner = obj_t->name;
        if (obj_t->kind == TypeKind::String) catalog_owner = "String";
        if (obj_t->kind == TypeKind::Entity) catalog_owner = "Entity";
        if (obj_t->kind == TypeKind::Array) catalog_owner = "Array";
        if (const ApiPropertyDescriptor* property = ApiCatalog::instance().find_available_property(catalog_owner, mem->member_name)) {
            if (!property->readable) {
                m_diagnostics.report_error("E3046", std::format("{}.{} is write-only", catalog_owner, mem->member_name), mem->span);
            }
            TypeAnnotation annotation{property->type};
            auto property_type = resolve_type_annotation(annotation, scope.get());
            property_type->is_nullable = property->nullable;
            m_node_types[mem] = property_type;
            return property_type;
        }

        auto field_t = m_type_float;
        if (mem->is_optional) {
            auto opt_t = std::make_shared<Type>(*field_t);
            opt_t->is_nullable = true;
            m_node_types[mem] = opt_t;
            return opt_t;
        }
        m_node_types[mem] = field_t;
        return field_t;
    }

    if (auto idx = dynamic_cast<IndexExpr*>(expr.get())) {
        auto obj_t = check_expression(idx->object, scope);
        auto idx_t = check_expression(idx->index, scope);

        if (obj_t->kind == TypeKind::Array && !obj_t->type_args.empty()) {
            m_node_types[idx] = obj_t->type_args[0];
            return obj_t->type_args[0];
        }
        if (obj_t->kind == TypeKind::Map && obj_t->type_args.size() == 2) {
            m_node_types[idx] = obj_t->type_args[1];
            return obj_t->type_args[1];
        }
        m_node_types[idx] = m_type_error;
        return m_type_error;
    }

    if (auto arr = dynamic_cast<ArrayLiteralExpr*>(expr.get())) {
        std::shared_ptr<Type> elem_t = m_type_void;
        for (const auto& el : arr->elements) {
            elem_t = check_expression(el, scope);
        }
        auto arr_t = Type::make_generic(TypeKind::Array, "Array", {elem_t});
        m_node_types[arr] = arr_t;
        return arr_t;
    }

    if (dynamic_cast<ObjectLiteralExpr*>(expr.get()) || dynamic_cast<CastExpr*>(expr.get()) ||
        dynamic_cast<ArrowFunctionExpr*>(expr.get()) || dynamic_cast<ThisExpr*>(expr.get())) {
        m_diagnostics.report_error("E3042", "This expression is parsed but not supported by the native EngineScript backend", expr->span);
        m_node_types[expr.get()] = m_type_error;
        return m_type_error;
    }

    if (auto aw = dynamic_cast<AwaitExpr*>(expr.get())) {
        auto op_t = check_expression(aw->operand, scope);
        if (op_t->kind == TypeKind::Task && !op_t->type_args.empty()) {
            m_node_types[aw] = op_t->type_args[0];
            return op_t->type_args[0];
        }
        m_node_types[aw] = m_type_void;
        return m_type_void;
    }

    return m_type_error;
}

} // namespace EngineScript
