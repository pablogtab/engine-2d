#include "TypeChecker.hpp"
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
        auto target_t = check_expression(assign->target, scope);
        auto val_t = check_expression(assign->value, scope);
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
            if (mem->member_name == "axis") {
                m_node_types[call] = m_type_float;
                return m_type_float;
            }
            if (mem->member_name == "pressed" || mem->member_name == "released" || mem->member_name == "down") {
                m_node_types[call] = m_type_bool;
                return m_type_bool;
            }
            if (mem->member_name == "play" || mem->member_name == "stop") {
                m_node_types[call] = m_type_void;
                return m_type_void;
            }
            if (mem->member_name == "play_sound" || mem->member_name == "play_music" || mem->member_name == "load_scene") {
                m_node_types[call] = m_type_bool;
                return m_type_bool;
            }
            if (mem->member_name == "stop_music" || mem->member_name == "set_master_volume") {
                m_node_types[call] = m_type_void;
                return m_type_void;
            }
            if (mem->member_name == "current_scene" || mem->member_name == "current_scene_name") {
                m_node_types[call] = m_type_string;
                return m_type_string;
            }
            if (mem->member_name == "raycast") {
                auto hit_t = Type::make_generic(TypeKind::Class, "RaycastHit", {});
                hit_t->is_nullable = true;
                m_node_types[call] = hit_t;
                return hit_t;
            }
            if (mem->member_name == "push" || mem->member_name == "insert" || mem->member_name == "remove") {
                m_node_types[call] = m_type_void;
                return m_type_void;
            }
            if (mem->member_name == "ok") {
                // Result.ok(...)
                std::shared_ptr<Type> arg_t = call->arguments.empty() ? m_type_void : get_type_of_node(call->arguments[0].get());
                auto res_t = Type::make_generic(TypeKind::Result, "Result", {arg_t, m_type_string});
                m_node_types[call] = res_t;
                return res_t;
            }
            if (mem->member_name == "err") {
                // Result.err(...)
                std::shared_ptr<Type> err_t = call->arguments.empty() ? m_type_string : get_type_of_node(call->arguments[0].get());
                auto res_t = Type::make_generic(TypeKind::Result, "Result", {m_type_void, err_t});
                m_node_types[call] = res_t;
                return res_t;
            }
        }

        // Global async functions
        if (auto id = dynamic_cast<IdentifierExpr*>(call->callee.get())) {
            if (id->name == "delay" || id->name == "realDelay" || id->name == "nextFrame" || id->name == "fixedUpdate") {
                auto task_t = Type::make_generic(TypeKind::Task, "Task", {m_type_void});
                m_node_types[call] = task_t;
                return task_t;
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

        // Vector fields
        if (obj_t->kind == TypeKind::Vec2 || obj_t->kind == TypeKind::Vec3 || obj_t->kind == TypeKind::Vec4) {
            if (mem->member_name == "x" || mem->member_name == "y" || mem->member_name == "z" || mem->member_name == "w") {
                m_node_types[mem] = m_type_float;
                return m_type_float;
            }
        }

        // RigidBody2D fields
        if (obj_t->name == "RigidBody2D") {
            if (mem->member_name == "velocity") {
                auto v2 = Type::make_primitive(TypeKind::Vec2);
                m_node_types[mem] = v2;
                return v2;
            }
            if (mem->member_name == "isGrounded") {
                m_node_types[mem] = m_type_bool;
                return m_type_bool;
            }
        }

        // Display fields
        if (obj_t->name == "Display") {
            if (mem->member_name == "width" || mem->member_name == "height" || mem->member_name == "pixel_width" || mem->member_name == "pixel_height") {
                m_node_types[mem] = m_type_int;
                return m_type_int;
            }
            if (mem->member_name == "dpi_scale") {
                m_node_types[mem] = m_type_float;
                return m_type_float;
            }
            if (mem->member_name == "is_fullscreen") {
                m_node_types[mem] = m_type_bool;
                return m_type_bool;
            }
        }

        // RaycastHit fields
        if (obj_t->name == "RaycastHit") {
            if (mem->member_name == "hit") {
                m_node_types[mem] = m_type_bool;
                return m_type_bool;
            }
            if (mem->member_name == "point" || mem->member_name == "normal") {
                auto v2 = Type::make_primitive(TypeKind::Vec2);
                m_node_types[mem] = v2;
                return v2;
            }
            if (mem->member_name == "distance") {
                m_node_types[mem] = m_type_float;
                return m_type_float;
            }
            if (mem->member_name == "entity") {
                m_node_types[mem] = m_type_entity;
                return m_type_entity;
            }
        }

        // Array fields
        if (obj_t->kind == TypeKind::Array) {
            if (mem->member_name == "length") {
                m_node_types[mem] = m_type_int;
                return m_type_int;
            }
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
