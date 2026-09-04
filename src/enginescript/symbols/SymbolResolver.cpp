#include "SymbolResolver.hpp"
#include "../api/ApiCatalog.hpp"
#include <format>
#include <sstream>

namespace EngineScript {

SymbolResolver::SymbolResolver(const SourceManager& source_manager, DiagnosticEngine& diagnostics)
    : m_source_manager(source_manager), m_diagnostics(diagnostics) {
    m_global_scope = std::make_shared<Scope>(ScopeKind::Global);
    register_builtin_types();
}

void SymbolResolver::register_builtin_types() {
    auto add_builtin_type = [this](const ApiTypeDescriptor& type) {
        auto sym = std::make_shared<Symbol>();
        sym->id = next_symbol_id();
        sym->name = type.name;
        sym->kind = type.is_interface ? SymbolKind::Interface : SymbolKind::Class;
        sym->is_exported = true;
        sym->access = AccessModifier::Public;
        sym->type_string = type.name;
        m_global_scope->define(sym);
    };

    for (const ApiTypeDescriptor& type : ApiCatalog::instance().types()) {
        if (type.availability == ApiAvailability::Available) {
            add_builtin_type(type);
        }
    }

    // Builtin global functions
    auto add_builtin_func = [this](std::string name, std::string ret_type) {
        auto sym = std::make_shared<Symbol>();
        sym->id = next_symbol_id();
        sym->name = name;
        sym->kind = SymbolKind::Function;
        sym->is_exported = true;
        sym->access = AccessModifier::Public;
        sym->type_string = std::format("Function(): {}", ret_type);
        m_global_scope->define(sym);
    };

    for (const ApiFunctionDescriptor& function : ApiCatalog::instance().functions()) {
        if (function.availability == ApiAvailability::Available) {
            add_builtin_func(function.name, function.return_type);
        }
    }
}

std::shared_ptr<Scope> SymbolResolver::get_module_scope(const std::string& module_path) const {
    auto it = m_module_scopes.find(module_path);
    if (it != m_module_scopes.end()) return it->second;
    return nullptr;
}

std::shared_ptr<Symbol> SymbolResolver::find_symbol_for_node(const ASTNode* node) const {
    auto it = m_node_to_symbol.find(node);
    if (it != m_node_to_symbol.end()) return it->second;
    return nullptr;
}

std::shared_ptr<Scope> SymbolResolver::find_scope_for_node(const ASTNode* node) const {
    auto it = m_node_to_scope.find(node);
    if (it != m_node_to_scope.end()) return it->second;
    return nullptr;
}

void SymbolResolver::declare_symbols_in_module(const std::shared_ptr<ModuleNode>& module) {
    auto mod_scope = std::make_shared<Scope>(ScopeKind::Module, m_global_scope);
    m_module_scopes[module->module_path] = mod_scope;
    m_node_to_scope[module.get()] = mod_scope;

    for (const auto& decl : module->declarations) {
        if (auto cls = dynamic_cast<ClassDecl*>(decl.get())) {
            auto sym = std::make_shared<Symbol>();
            sym->id = next_symbol_id();
            sym->name = cls->name;
            sym->kind = SymbolKind::Class;
            sym->span = cls->span;
            sym->file_id = module->file_id;
            sym->is_exported = cls->is_exported;
            sym->is_behaviour = cls->is_behaviour();
            sym->ast_node = decl;
            sym->type_string = cls->name;

            if (!mod_scope->define(sym)) {
                m_diagnostics.report_error("E2001", std::format("Duplicate symbol '{}' in module", cls->name), cls->span);
            }
            m_node_to_symbol[cls] = sym;

            // Class scope
            auto cls_scope = std::make_shared<Scope>(ScopeKind::Class, mod_scope);
            m_node_to_scope[cls] = cls_scope;
            for (const auto& parameter : cls->generic_params) {
                auto type_parameter = std::make_shared<Symbol>();
                type_parameter->id = next_symbol_id();
                type_parameter->name = parameter.name;
                type_parameter->kind = SymbolKind::TypeParameter;
                type_parameter->span = parameter.span;
                type_parameter->file_id = module->file_id;
                type_parameter->type_string = parameter.name;
                cls_scope->define(type_parameter);
            }

            // Add fields from constructor parameters
            if (cls->constructor_decl) {
                for (const auto& param : cls->constructor_decl->params) {
                    if (param->access != AccessModifier::None) {
                        auto field_sym = std::make_shared<Symbol>();
                        field_sym->id = next_symbol_id();
                        field_sym->name = param->name;
                        field_sym->kind = SymbolKind::Field;
                        field_sym->span = param->span;
                        field_sym->file_id = module->file_id;
                        field_sym->access = param->access;
                        field_sym->type_string = param->type_annotation.to_string();
                        field_sym->ast_node = param;
                        field_sym->parent_symbol = sym;
                        sym->members.push_back(field_sym);
                        cls_scope->define(field_sym);
                        m_node_to_symbol[param.get()] = field_sym;
                    }
                }
            }

            // Properties
            for (const auto& prop : cls->properties) {
                auto prop_sym = std::make_shared<Symbol>();
                prop_sym->id = next_symbol_id();
                prop_sym->name = prop->name;
                prop_sym->kind = SymbolKind::Field;
                prop_sym->span = prop->span;
                prop_sym->file_id = module->file_id;
                prop_sym->access = prop->access;
                prop_sym->is_static = prop->is_static;
                prop_sym->type_string = prop->type_annotation ? prop->type_annotation->to_string() : "<inferred>";
                prop_sym->ast_node = prop;
                prop_sym->parent_symbol = sym;
                sym->members.push_back(prop_sym);
                cls_scope->define(prop_sym);
                m_node_to_symbol[prop.get()] = prop_sym;
            }

            // Methods
            for (const auto& m : cls->methods) {
                auto m_sym = std::make_shared<Symbol>();
                m_sym->id = next_symbol_id();
                m_sym->name = m->name;
                m_sym->kind = SymbolKind::Method;
                m_sym->span = m->span;
                m_sym->file_id = module->file_id;
                m_sym->access = m->access;
                m_sym->is_async = m->is_async;
                m_sym->is_static = m->is_static;
                m_sym->type_string = m->return_type ? m->return_type->to_string() : "Void";
                m_sym->ast_node = m;
                m_sym->parent_symbol = sym;
                sym->members.push_back(m_sym);
                cls_scope->define(m_sym);
                m_node_to_symbol[m.get()] = m_sym;
            }
        } else if (auto st = dynamic_cast<StructDecl*>(decl.get())) {
            auto sym = std::make_shared<Symbol>();
            sym->id = next_symbol_id();
            sym->name = st->name;
            sym->kind = SymbolKind::Struct;
            sym->span = st->span;
            sym->file_id = module->file_id;
            sym->is_exported = st->is_exported;
            sym->ast_node = decl;
            sym->type_string = st->name;

            if (!mod_scope->define(sym)) {
                m_diagnostics.report_error("E2001", std::format("Duplicate symbol '{}' in module", st->name), st->span);
            }
            m_node_to_symbol[st] = sym;

            auto st_scope = std::make_shared<Scope>(ScopeKind::Class, mod_scope);
            m_node_to_scope[st] = st_scope;
            for (const auto& parameter : st->generic_params) {
                auto type_parameter = std::make_shared<Symbol>();
                type_parameter->id = next_symbol_id();
                type_parameter->name = parameter.name;
                type_parameter->kind = SymbolKind::TypeParameter;
                type_parameter->span = parameter.span;
                type_parameter->file_id = module->file_id;
                type_parameter->type_string = parameter.name;
                st_scope->define(type_parameter);
            }

            for (const auto& prop : st->properties) {
                auto prop_sym = std::make_shared<Symbol>();
                prop_sym->id = next_symbol_id();
                prop_sym->name = prop->name;
                prop_sym->kind = SymbolKind::Field;
                prop_sym->span = prop->span;
                prop_sym->file_id = module->file_id;
                prop_sym->access = prop->access;
                prop_sym->type_string = prop->type_annotation ? prop->type_annotation->to_string() : "<inferred>";
                prop_sym->ast_node = prop;
                prop_sym->parent_symbol = sym;
                sym->members.push_back(prop_sym);
                st_scope->define(prop_sym);
                m_node_to_symbol[prop.get()] = prop_sym;
            }
            for (const auto& method : st->methods) {
                auto method_symbol = std::make_shared<Symbol>();
                method_symbol->id = next_symbol_id();
                method_symbol->name = method->name;
                method_symbol->kind = SymbolKind::Method;
                method_symbol->span = method->span;
                method_symbol->file_id = module->file_id;
                method_symbol->access = AccessModifier::Public;
                method_symbol->type_string = method->return_type ? method->return_type->to_string() : "Void";
                method_symbol->ast_node = method;
                method_symbol->parent_symbol = sym;
                sym->members.push_back(method_symbol);
                st_scope->define(method_symbol);
                m_node_to_symbol[method.get()] = method_symbol;
            }
        } else if (auto iface = dynamic_cast<InterfaceDecl*>(decl.get())) {
            auto symbol = std::make_shared<Symbol>();
            symbol->id = next_symbol_id();
            symbol->name = iface->name;
            symbol->kind = SymbolKind::Interface;
            symbol->span = iface->span;
            symbol->file_id = module->file_id;
            symbol->is_exported = iface->is_exported;
            symbol->ast_node = decl;
            symbol->type_string = iface->name;
            if (!mod_scope->define(symbol)) {
                m_diagnostics.report_error("E2001", std::format("Duplicate symbol '{}' in module", iface->name), iface->span);
            }
            m_node_to_symbol[iface] = symbol;
            auto interface_scope = std::make_shared<Scope>(ScopeKind::Class, mod_scope);
            m_node_to_scope[iface] = interface_scope;
            for (const auto& member : iface->members) {
                auto member_symbol = std::make_shared<Symbol>();
                member_symbol->id = next_symbol_id();
                member_symbol->name = member.name;
                member_symbol->kind = member.is_method ? SymbolKind::Method : SymbolKind::Field;
                member_symbol->span = member.span;
                member_symbol->file_id = module->file_id;
                member_symbol->type_string = member.return_type.to_string();
                member_symbol->parent_symbol = symbol;
                symbol->members.push_back(member_symbol);
                interface_scope->define(member_symbol);
            }
        } else if (auto fn = dynamic_cast<FunctionDecl*>(decl.get())) {
            auto sym = std::make_shared<Symbol>();
            sym->id = next_symbol_id();
            sym->name = fn->name;
            sym->kind = SymbolKind::Function;
            sym->span = fn->span;
            sym->file_id = module->file_id;
            sym->is_exported = fn->is_exported;
            sym->is_async = fn->is_async;
            sym->ast_node = decl;
            sym->type_string = fn->return_type ? fn->return_type->to_string() : "Void";

            if (!mod_scope->define(sym)) {
                m_diagnostics.report_error("E2001", std::format("Duplicate symbol '{}' in module", fn->name), fn->span);
            }
            m_node_to_symbol[fn] = sym;
        } else if (auto cn = dynamic_cast<ConstDecl*>(decl.get())) {
            auto sym = std::make_shared<Symbol>();
            sym->id = next_symbol_id();
            sym->name = cn->name;
            sym->kind = SymbolKind::Variable;
            sym->span = cn->span;
            sym->file_id = module->file_id;
            sym->is_exported = cn->is_exported;
            sym->is_const = true;
            sym->ast_node = decl;
            sym->type_string = cn->type_annotation ? cn->type_annotation->to_string() : "<inferred>";

            if (!mod_scope->define(sym)) {
                m_diagnostics.report_error("E2001", std::format("Duplicate symbol '{}' in module", cn->name), cn->span);
            }
            m_node_to_symbol[cn] = sym;
        } else if (auto en = dynamic_cast<EnumDecl*>(decl.get())) {
            auto sym = std::make_shared<Symbol>();
            sym->id = next_symbol_id();
            sym->name = en->name;
            sym->kind = SymbolKind::Enum;
            sym->span = en->span;
            sym->file_id = module->file_id;
            sym->is_exported = en->is_exported;
            sym->ast_node = decl;
            sym->type_string = en->name;

            if (!mod_scope->define(sym)) {
                m_diagnostics.report_error("E2001", std::format("Duplicate symbol '{}' in module", en->name), en->span);
            }
            m_node_to_symbol[en] = sym;

            for (const auto& entry : en->entries) {
                auto mem_sym = std::make_shared<Symbol>();
                mem_sym->id = next_symbol_id();
                mem_sym->name = entry.name;
                mem_sym->kind = SymbolKind::EnumMember;
                mem_sym->span = entry.span;
                mem_sym->file_id = module->file_id;
                mem_sym->type_string = en->name;
                sym->members.push_back(mem_sym);
            }
        }
    }
}

void SymbolResolver::resolve_imports(const std::shared_ptr<ModuleNode>& module) {
    auto mod_scope = m_module_scopes[module->module_path];
    if (!mod_scope) return;

    for (const auto& decl : module->declarations) {
        if (auto imp = dynamic_cast<ImportDecl*>(decl.get())) {
            auto target_path_opt = m_source_manager.resolve_import(module->module_path, imp->module_path);
            if (!target_path_opt) {
                m_diagnostics.report_error("E2010", std::format("Import target '{}' escapes project source boundary", imp->module_path), imp->span);
                continue;
            }

            auto target_scope = get_module_scope(*target_path_opt);
            if (!target_scope) {
                m_diagnostics.report_error(
                    "E2012",
                    std::format("Import module '{}' was not found", imp->module_path),
                    imp->module_path_span
                );
            }

            if (!imp->is_wildcard && imp->items.empty()) {
                m_diagnostics.report_error(
                    "E2013",
                    "Import list cannot be empty; import at least one exported symbol or remove the declaration",
                    imp->specifier_span
                );
            }

            if (!target_scope) {
                continue;
            }

            for (const auto& item : imp->items) {
                auto sym = target_scope->lookup_local(item.name);
                if (!sym || !sym->is_exported) {
                    m_diagnostics.report_error("E2011", std::format("Symbol '{}' is not exported from '{}'", item.name, imp->module_path), item.span);
                    continue;
                }

                std::string local_name = item.alias ? *item.alias : item.name;
                auto imported_sym = std::make_shared<Symbol>(*sym);
                imported_sym->name = local_name;
                imported_sym->kind = SymbolKind::Import;
                if (item.alias) imported_sym->id = next_symbol_id();
                imported_sym->span = item.span;
                imported_sym->file_id = module->file_id;
                mod_scope->define(imported_sym);
            }
        }
    }
}

void SymbolResolver::resolve_members_and_types(const std::shared_ptr<ModuleNode>& module) {
    auto mod_scope = m_module_scopes[module->module_path];
    if (!mod_scope) return;

    for (const auto& decl : module->declarations) {
        if (auto cls = dynamic_cast<ClassDecl*>(decl.get())) {
            auto cls_scope = m_node_to_scope[cls];

            // Resolve base class
            if (cls->base_class) {
                auto base_sym = mod_scope->lookup(cls->base_class->name);
                if (!base_sym) {
                    m_diagnostics.report_error("E2020", std::format("Base class '{}' not found", cls->base_class->name), cls->base_class->span);
                }
            }
            for (const auto& interface_type : cls->interfaces) {
                const auto interface_symbol = mod_scope->lookup(interface_type.name);
                if (!interface_symbol || interface_symbol->kind != SymbolKind::Interface) {
                    m_diagnostics.report_error("E2021", std::format("Interface '{}' not found", interface_type.name), interface_type.span);
                }
            }

            // Resolve constructor body
            if (cls->constructor_decl && cls->constructor_decl->body) {
                auto ctor_scope = std::make_shared<Scope>(ScopeKind::Function, cls_scope);
                for (const auto& p : cls->constructor_decl->params) {
                    auto p_sym = std::make_shared<Symbol>();
                    p_sym->id = next_symbol_id();
                    p_sym->name = p->name;
                    p_sym->kind = SymbolKind::Parameter;
                    p_sym->span = p->span;
                    p_sym->file_id = module->file_id;
                    p_sym->type_string = p->type_annotation.to_string();
                    ctor_scope->define(p_sym);
                    m_node_to_symbol[p.get()] = p_sym;
                }
                resolve_statement_and_expr_symbols(cls->constructor_decl->body, ctor_scope);
            }

            // Resolve method bodies
            for (const auto& m : cls->methods) {
                if (m->body) {
                    auto m_scope = std::make_shared<Scope>(ScopeKind::Function, cls_scope);
                    for (const auto& p : m->params) {
                        auto p_sym = std::make_shared<Symbol>();
                        p_sym->id = next_symbol_id();
                        p_sym->name = p->name;
                        p_sym->kind = SymbolKind::Parameter;
                        p_sym->span = p->span;
                        p_sym->file_id = module->file_id;
                        p_sym->type_string = p->type_annotation.to_string();
                        m_scope->define(p_sym);
                        m_node_to_symbol[p.get()] = p_sym;
                    }
                    resolve_statement_and_expr_symbols(m->body, m_scope);
                }
            }
        } else if (auto st = dynamic_cast<StructDecl*>(decl.get())) {
            auto st_scope = m_node_to_scope[st];
            for (const auto& interface_type : st->interfaces) {
                const auto interface_symbol = mod_scope->lookup(interface_type.name);
                if (!interface_symbol || interface_symbol->kind != SymbolKind::Interface) {
                    m_diagnostics.report_error("E2021", std::format("Interface '{}' not found", interface_type.name), interface_type.span);
                }
            }
            for (const auto& method : st->methods) {
                auto method_scope = std::make_shared<Scope>(ScopeKind::Function, st_scope);
                for (const auto& parameter : method->params) {
                    auto parameter_symbol = std::make_shared<Symbol>();
                    parameter_symbol->id = next_symbol_id();
                    parameter_symbol->name = parameter->name;
                    parameter_symbol->kind = SymbolKind::Parameter;
                    parameter_symbol->type_string = parameter->type_annotation.to_string();
                    parameter_symbol->span = parameter->span;
                    method_scope->define(parameter_symbol);
                    m_node_to_symbol[parameter.get()] = parameter_symbol;
                }
                resolve_statement_and_expr_symbols(method->body, method_scope);
            }
        } else if (auto iface = dynamic_cast<InterfaceDecl*>(decl.get())) {
            for (const auto& extended : iface->extended_interfaces) {
                const auto extended_symbol = mod_scope->lookup(extended.name);
                if (!extended_symbol || extended_symbol->kind != SymbolKind::Interface) {
                    m_diagnostics.report_error("E2022", std::format("Extended interface '{}' not found", extended.name), extended.span);
                }
            }
        } else if (auto fn = dynamic_cast<FunctionDecl*>(decl.get())) {
            if (fn->body) {
                auto fn_scope = std::make_shared<Scope>(ScopeKind::Function, mod_scope);
                for (const auto& p : fn->params) {
                    auto p_sym = std::make_shared<Symbol>();
                    p_sym->id = next_symbol_id();
                    p_sym->name = p->name;
                    p_sym->kind = SymbolKind::Parameter;
                    p_sym->span = p->span;
                    p_sym->file_id = module->file_id;
                    p_sym->type_string = p->type_annotation.to_string();
                    fn_scope->define(p_sym);
                    m_node_to_symbol[p.get()] = p_sym;
                }
                resolve_statement_and_expr_symbols(fn->body, fn_scope);
            }
        }
    }
}

void SymbolResolver::resolve_statement_and_expr_symbols(const std::shared_ptr<Statement>& stmt, const std::shared_ptr<Scope>& scope) {
    if (!stmt) return;

    if (auto blk = dynamic_cast<BlockStmt*>(stmt.get())) {
        auto blk_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        for (const auto& s : blk->statements) {
            resolve_statement_and_expr_symbols(s, blk_scope);
        }
    } else if (auto v = dynamic_cast<VarDeclStmt*>(stmt.get())) {
        if (v->initializer) {
            resolve_expression_symbols(v->initializer, scope);
        }
        auto v_sym = std::make_shared<Symbol>();
        v_sym->id = next_symbol_id();
        v_sym->name = v->name;
        v_sym->kind = SymbolKind::Variable;
        v_sym->span = v->span;
        v_sym->is_const = v->is_const;
        v_sym->type_string = v->type_annotation ? v->type_annotation->to_string() : "<inferred>";
        v_sym->ast_node = stmt;
        scope->define(v_sym);
        m_node_to_symbol[v] = v_sym;
    } else if (auto e = dynamic_cast<ExprStmt*>(stmt.get())) {
        resolve_expression_symbols(e->expr, scope);
    } else if (auto if_s = dynamic_cast<IfStmt*>(stmt.get())) {
        resolve_expression_symbols(if_s->condition, scope);
        resolve_statement_and_expr_symbols(if_s->then_branch, scope);
        if (if_s->else_branch) resolve_statement_and_expr_symbols(if_s->else_branch, scope);
    } else if (auto binding = dynamic_cast<OptionalBindingIfStmt*>(stmt.get())) {
        resolve_expression_symbols(binding->optional_value, scope);
        auto then_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        auto symbol = std::make_shared<Symbol>();
        symbol->id = next_symbol_id();
        symbol->name = binding->name;
        symbol->kind = SymbolKind::Variable;
        symbol->type_string = "<optional-binding>";
        symbol->span = binding->span;
        then_scope->define(symbol);
        m_node_to_symbol[binding] = symbol;
        resolve_statement_and_expr_symbols(binding->then_branch, then_scope);
        if (binding->else_branch) resolve_statement_and_expr_symbols(binding->else_branch, scope);
    } else if (auto guard = dynamic_cast<GuardStmt*>(stmt.get())) {
        resolve_expression_symbols(guard->optional_value, scope);
        resolve_statement_and_expr_symbols(guard->else_branch, scope);
        auto symbol = std::make_shared<Symbol>();
        symbol->id = next_symbol_id();
        symbol->name = guard->name;
        symbol->kind = SymbolKind::Variable;
        symbol->type_string = "<optional-binding>";
        symbol->span = guard->span;
        scope->define(symbol);
        m_node_to_symbol[guard] = symbol;
    } else if (auto match_statement = dynamic_cast<MatchStmt*>(stmt.get())) {
        resolve_expression_symbols(match_statement->subject, scope);
        for (const auto& arm : match_statement->arms) {
            resolve_statement_and_expr_symbols(arm.body, scope);
        }
    } else if (auto w = dynamic_cast<WhileStmt*>(stmt.get())) {
        resolve_expression_symbols(w->condition, scope);
        resolve_statement_and_expr_symbols(w->body, scope);
    } else if (auto f = dynamic_cast<ForStmt*>(stmt.get())) {
        auto for_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        resolve_statement_and_expr_symbols(f->initializer, for_scope);
        resolve_expression_symbols(f->condition, for_scope);
        resolve_expression_symbols(f->update, for_scope);
        resolve_statement_and_expr_symbols(f->body, for_scope);
    } else if (auto for_in = dynamic_cast<ForInStmt*>(stmt.get())) {
        auto for_scope = std::make_shared<Scope>(ScopeKind::Block, scope);
        resolve_expression_symbols(for_in->collection, for_scope);
        auto v_sym = std::make_shared<Symbol>();
        v_sym->id = next_symbol_id();
        v_sym->name = for_in->var_name;
        v_sym->kind = SymbolKind::Variable;
        for_scope->define(v_sym);
        resolve_statement_and_expr_symbols(for_in->body, for_scope);
    } else if (auto ret = dynamic_cast<ReturnStmt*>(stmt.get())) {
        if (ret->value) resolve_expression_symbols(ret->value, scope);
    }
}

void SymbolResolver::resolve_expression_symbols(const std::shared_ptr<Expression>& expr, const std::shared_ptr<Scope>& scope) {
    if (!expr) return;

    if (auto id = dynamic_cast<IdentifierExpr*>(expr.get())) {
        auto sym = scope->lookup(id->name);
        if (!sym) {
            m_diagnostics.report_error("E2030", std::format("Unresolved identifier '{}'", id->name), id->span);
        } else {
            m_node_to_symbol[id] = sym;
        }
    } else if (auto interpolated = dynamic_cast<InterpolatedStringExpr*>(expr.get())) {
        for (const auto& segment : interpolated->segments) {
            resolve_expression_symbols(segment.expression, scope);
        }
    } else if (auto conditional = dynamic_cast<ConditionalExpr*>(expr.get())) {
        resolve_expression_symbols(conditional->condition, scope);
        resolve_expression_symbols(conditional->then_value, scope);
        resolve_expression_symbols(conditional->else_value, scope);
    } else if (auto match_expression = dynamic_cast<MatchExpr*>(expr.get())) {
        resolve_expression_symbols(match_expression->subject, scope);
        for (const auto& arm : match_expression->arms) {
            resolve_expression_symbols(arm.value, scope);
        }
    } else if (auto bin = dynamic_cast<BinaryExpr*>(expr.get())) {
        resolve_expression_symbols(bin->left, scope);
        resolve_expression_symbols(bin->right, scope);
    } else if (auto un = dynamic_cast<UnaryExpr*>(expr.get())) {
        resolve_expression_symbols(un->operand, scope);
    } else if (auto assign = dynamic_cast<AssignmentExpr*>(expr.get())) {
        resolve_expression_symbols(assign->target, scope);
        resolve_expression_symbols(assign->value, scope);
    } else if (auto call = dynamic_cast<CallExpr*>(expr.get())) {
        resolve_expression_symbols(call->callee, scope);
        for (const auto& a : call->arguments) {
            resolve_expression_symbols(a, scope);
        }
    } else if (auto mem = dynamic_cast<MemberAccessExpr*>(expr.get())) {
        resolve_expression_symbols(mem->object, scope);
    } else if (auto idx = dynamic_cast<IndexExpr*>(expr.get())) {
        resolve_expression_symbols(idx->object, scope);
        resolve_expression_symbols(idx->index, scope);
    } else if (auto arr = dynamic_cast<ArrayLiteralExpr*>(expr.get())) {
        for (const auto& el : arr->elements) {
            resolve_expression_symbols(el, scope);
        }
    } else if (auto collection = dynamic_cast<CollectionLiteralExpr*>(expr.get())) {
        for (const auto& [key, value] : collection->map_entries) {
            resolve_expression_symbols(key, scope);
            resolve_expression_symbols(value, scope);
        }
        for (const auto& element : collection->elements) resolve_expression_symbols(element, scope);
    } else if (auto obj = dynamic_cast<ObjectLiteralExpr*>(expr.get())) {
        for (const auto& entry : obj->entries) {
            resolve_expression_symbols(entry.value, scope);
        }
    } else if (auto aw = dynamic_cast<AwaitExpr*>(expr.get())) {
        resolve_expression_symbols(aw->operand, scope);
    } else if (auto arrow = dynamic_cast<ArrowFunctionExpr*>(expr.get())) {
        auto function_scope = std::make_shared<Scope>(ScopeKind::Function, scope);
        for (const auto& parameter : arrow->params) {
            auto parameter_symbol = std::make_shared<Symbol>();
            parameter_symbol->id = next_symbol_id();
            parameter_symbol->name = parameter->name;
            parameter_symbol->kind = SymbolKind::Parameter;
            parameter_symbol->type_string = parameter->type_annotation.to_string();
            parameter_symbol->span = parameter->span;
            function_scope->define(parameter_symbol);
            m_node_to_symbol[parameter.get()] = parameter_symbol;
        }
        if (arrow->body_stmt) resolve_statement_and_expr_symbols(arrow->body_stmt, function_scope);
        if (arrow->body_expr) resolve_expression_symbols(arrow->body_expr, function_scope);
    }
}

bool SymbolResolver::resolve_module(const std::shared_ptr<ModuleNode>& module_node) {
    declare_symbols_in_module(module_node);
    resolve_imports(module_node);
    resolve_members_and_types(module_node);
    return !m_diagnostics.has_errors();
}

bool SymbolResolver::resolve_all(const std::vector<std::shared_ptr<ModuleNode>>& modules) {
    for (const auto& m : modules) {
        declare_symbols_in_module(m);
    }
    for (const auto& m : modules) {
        resolve_imports(m);
    }
    for (const auto& m : modules) {
        resolve_members_and_types(m);
    }
    return !m_diagnostics.has_errors();
}

std::string SymbolResolver::compute_public_signature_hash(const ModuleNode& module) const {
    std::ostringstream ss;
    auto scope = get_module_scope(module.module_path);
    if (!scope) return "0";

    for (const auto& [name, sym] : scope->all_symbols()) {
        if (sym->is_exported) {
            ss << symbol_kind_to_string(sym->kind) << ":" << sym->name << ":" << sym->type_string << ";";
            for (const auto& m : sym->members) {
                if (m->access == AccessModifier::Public) {
                    ss << m->name << ":" << m->type_string << ",";
                }
            }
        }
    }

    size_t h = std::hash<std::string>{}(ss.str());
    return std::to_string(h);
}

} // namespace EngineScript
