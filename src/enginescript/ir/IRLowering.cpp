#include "IRLowering.hpp"
#include "../api/ApiCatalog.hpp"
#include <algorithm>

namespace EngineScript {

namespace {

IRMatchPattern lower_match_pattern(const MatchPattern& pattern) {
    IRMatchPattern result;
    result.kind = static_cast<IRMatchPattern::Kind>(pattern.kind);
    result.bool_value = pattern.bool_value;
    result.int_value = pattern.int_value;
    result.float_value = pattern.float_value;
    result.string_value = pattern.string_value;
    result.enum_name = pattern.enum_name;
    result.member_name = pattern.member_name;
    return result;
}

} // namespace

IRLowering::IRLowering(const TypeChecker& type_checker)
    : m_type_checker(type_checker) {
}

IRModule IRLowering::lower_module(const ModuleNode& module) {
    IRModule ir_mod;
    ir_mod.module_path = module.module_path;
    ir_mod.file_id = module.file_id;

    for (const auto& decl : module.declarations) {
        if (auto imp = dynamic_cast<const ImportDecl*>(decl.get())) {
            ir_mod.imports.push_back(imp->module_path);
            for (const ImportDecl::ImportItem& item : imp->items) {
                if (item.alias) ir_mod.import_aliases.emplace(*item.alias, item.name);
            }
        } else if (auto cls = dynamic_cast<const ClassDecl*>(decl.get())) {
            ir_mod.classes.push_back(lower_class(*cls));
        } else if (auto st = dynamic_cast<const StructDecl*>(decl.get())) {
            ir_mod.structs.push_back(lower_struct(*st));
        } else if (auto interface_decl = dynamic_cast<const InterfaceDecl*>(decl.get())) {
            ir_mod.interfaces.push_back(lower_interface(*interface_decl));
        } else if (auto enum_decl = dynamic_cast<const EnumDecl*>(decl.get())) {
            ir_mod.enums.push_back(lower_enum(*enum_decl));
        } else if (auto fn = dynamic_cast<const FunctionDecl*>(decl.get())) {
            ir_mod.functions.push_back(lower_function(*fn));
        }
    }

    return ir_mod;
}

IRClass IRLowering::lower_class(const ClassDecl& cls) {
    IRClass ir_cls;
    ir_cls.name = cls.name;
    for (const auto& parameter : cls.generic_params) {
        ir_cls.generic_params.push_back({parameter.name, parameter.constraint ? parameter.constraint->name : std::string{}});
    }
    if (cls.base_class) ir_cls.base_class = cls.base_class->name;
    for (const auto& iface : cls.interfaces) {
        ir_cls.interfaces.push_back(iface.name);
    }
    ir_cls.is_behaviour = cls.is_behaviour();

    for (const auto& dec : cls.decorators) {
        ir_cls.decorators.push_back(dec.name);
    }

    if (cls.constructor_decl) {
        for (const auto& p : cls.constructor_decl->params) {
            IRParam param;
            param.name = p->name;
            param.type = m_type_checker.get_type_of_node(p.get());
            if (!param.type || param.type->is_error()) param.type = m_type_checker.resolve_type_annotation(p->type_annotation);
            for (const auto& d : p->decorators) {
                param.decorators.push_back(d.name);
            }
            ir_cls.constructor_params.push_back(param);
        }
        if (cls.constructor_decl->body) {
            IRBlock constructor_block;
            constructor_block.label = "constructor";
            for (const auto& statement : cls.constructor_decl->body->statements) {
                lower_statement(statement, constructor_block);
            }
            ir_cls.constructor_blocks.push_back(std::move(constructor_block));
        }
    }

    for (const auto& prop : cls.properties) {
        ir_cls.properties.push_back(lower_property(*prop));
    }

    for (const auto& m : cls.methods) {
        ir_cls.methods.push_back(lower_method(*m));
    }

    return ir_cls;
}

IRStruct IRLowering::lower_struct(const StructDecl& st) {
    IRStruct ir_st;
    ir_st.name = st.name;
    for (const auto& parameter : st.generic_params) {
        ir_st.generic_params.push_back({parameter.name, parameter.constraint ? parameter.constraint->name : std::string{}});
    }
    for (const auto& interface_type : st.interfaces) ir_st.interfaces.push_back(interface_type.name);
    for (const auto& prop : st.properties) {
        ir_st.properties.push_back(lower_property(*prop));
    }
    for (const auto& m : st.methods) {
        ir_st.methods.push_back(lower_method(*m));
    }
    return ir_st;
}

IRInterface IRLowering::lower_interface(const InterfaceDecl& interface_decl) {
    IRInterface result;
    result.name = interface_decl.name;
    for (const auto& parameter : interface_decl.generic_params) {
        result.generic_params.push_back({parameter.name, parameter.constraint ? parameter.constraint->name : std::string{}});
    }
    for (const auto& extended : interface_decl.extended_interfaces) result.extended_interfaces.push_back(extended.name);
    for (const auto& member : interface_decl.members) {
        IRInterfaceMember lowered;
        lowered.name = member.name;
        for (const auto& parameter : member.generic_params) {
            lowered.generic_params.push_back({parameter.name, parameter.constraint ? parameter.constraint->name : std::string{}});
        }
        lowered.is_method = member.is_method;
        lowered.type = m_type_checker.resolve_type_annotation(member.return_type);
        for (const auto& parameter : member.params) {
            IRParam lowered_parameter;
            lowered_parameter.name = parameter->name;
            lowered_parameter.type = m_type_checker.resolve_type_annotation(parameter->type_annotation);
            lowered.params.push_back(std::move(lowered_parameter));
        }
        result.members.push_back(std::move(lowered));
    }
    return result;
}

IREnum IRLowering::lower_enum(const EnumDecl& enum_decl) {
    IREnum result;
    result.name = enum_decl.name;
    result.string_backed = std::any_of(enum_decl.entries.begin(), enum_decl.entries.end(),
        [](const EnumEntry& entry) { return entry.string_value.has_value(); });
    for (const auto& entry : enum_decl.entries) {
        result.entries.push_back({entry.name, entry.int_value, entry.string_value});
    }
    return result;
}

IRProperty IRLowering::lower_property(const PropertyDecl& prop) {
    IRProperty ir_prop;
    ir_prop.name = prop.name;
    ir_prop.is_public = prop.access == AccessModifier::Public;
    ir_prop.type = m_type_checker.get_type_of_node(&prop);
    if (!ir_prop.type || ir_prop.type->is_error()) {
        ir_prop.type = prop.type_annotation ? m_type_checker.resolve_type_annotation(*prop.type_annotation) : ir_prop.type;
    }

    for (const auto& dec : prop.decorators) {
        ir_prop.decorators.push_back(dec.name);
        for (const auto& named : dec.named_args) {
            if (auto lit = dynamic_cast<const LiteralExpr*>(named.value.get())) {
                if (lit->kind == LiteralExpr::Kind::Float) {
                    ir_prop.numeric_attributes[named.key] = lit->float_val;
                } else if (lit->kind == LiteralExpr::Kind::Integer) {
                    ir_prop.numeric_attributes[named.key] = static_cast<double>(lit->int_val);
                }
            }
        }
    }

    if (prop.initializer) {
        ir_prop.initializer = lower_expression(prop.initializer);
    }
    return ir_prop;
}

IRMethod IRLowering::lower_method(const MethodDecl& m) {
    IRMethod ir_m;
    ir_m.name = m.name;
    for (const auto& parameter : m.generic_params) {
        ir_m.generic_params.push_back({parameter.name, parameter.constraint ? parameter.constraint->name : std::string{}});
    }
    ir_m.is_async = m.is_async;
    ir_m.is_override = m.is_override;
    ir_m.is_public = m.access == AccessModifier::Public;
    ir_m.return_type = m_type_checker.get_type_of_node(&m);
    if (!ir_m.return_type || ir_m.return_type->is_error()) {
        ir_m.return_type = m.return_type ? m_type_checker.resolve_type_annotation(*m.return_type) : Type::make_primitive(TypeKind::Void);
    }

    for (const auto& d : m.decorators) {
        ir_m.decorators.push_back(d.name);
    }

    for (const auto& p : m.params) {
        IRParam param;
        param.name = p->name;
        param.type = m_type_checker.get_type_of_node(p.get());
        if (!param.type || param.type->is_error()) param.type = m_type_checker.resolve_type_annotation(p->type_annotation);
        ir_m.params.push_back(param);
    }

    if (m.body) {
        IRBlock entry_block;
        entry_block.label = "entry";
        for (const auto& s : m.body->statements) {
            lower_statement(s, entry_block);
        }
        ir_m.blocks.push_back(std::move(entry_block));
    }

    return ir_m;
}

IRFunction IRLowering::lower_function(const FunctionDecl& fn) {
    IRFunction ir_fn;
    ir_fn.name = fn.name;
    for (const auto& parameter : fn.generic_params) {
        ir_fn.generic_params.push_back({parameter.name, parameter.constraint ? parameter.constraint->name : std::string{}});
    }
    ir_fn.is_async = fn.is_async;
    ir_fn.return_type = m_type_checker.get_type_of_node(&fn);
    if (!ir_fn.return_type || ir_fn.return_type->is_error()) {
        ir_fn.return_type = fn.return_type ? m_type_checker.resolve_type_annotation(*fn.return_type) : Type::make_primitive(TypeKind::Void);
    }

    for (const auto& p : fn.params) {
        IRParam param;
        param.name = p->name;
        param.type = m_type_checker.get_type_of_node(p.get());
        if (!param.type || param.type->is_error()) param.type = m_type_checker.resolve_type_annotation(p->type_annotation);
        ir_fn.params.push_back(param);
    }

    if (fn.body) {
        IRBlock entry_block;
        entry_block.label = "entry";
        for (const auto& s : fn.body->statements) {
            lower_statement(s, entry_block);
        }
        ir_fn.blocks.push_back(std::move(entry_block));
    }

    return ir_fn;
}

void IRLowering::lower_statement(const std::shared_ptr<Statement>& stmt, IRBlock& current_block) {
    if (!stmt) return;

    if (auto blk = dynamic_cast<const BlockStmt*>(stmt.get())) {
        for (const auto& s : blk->statements) {
            lower_statement(s, current_block);
        }
    } else if (auto v = dynamic_cast<const VarDeclStmt*>(stmt.get())) {
        auto declaration = std::make_shared<IRVarDecl>();
        declaration->kind = IROpKind::VarDecl;
        declaration->name = v->name;
        declaration->is_const = v->is_const;
        declaration->declared_type = m_type_checker.get_type_of_node(v);
        if (!declaration->declared_type || declaration->declared_type->is_error()) {
            declaration->declared_type = v->type_annotation
                ? m_type_checker.resolve_type_annotation(*v->type_annotation)
                : declaration->declared_type;
        }
        declaration->initializer = lower_expression(v->initializer);
        if (const auto* member_access = dynamic_cast<const MemberAccessExpr*>(v->initializer.get())) {
            const auto member = m_type_checker.resolved_member_symbols().find(member_access);
            const auto value_type = m_type_checker.get_type_of_node(v->initializer.get());
            const bool reference_value = value_type &&
                (value_type->kind == TypeKind::Vec2 || value_type->kind == TypeKind::Vec3 ||
                 value_type->kind == TypeKind::Vec4 || value_type->kind == TypeKind::Color ||
                 value_type->kind == TypeKind::Struct);
            const bool authored_mutable_field = member != m_type_checker.resolved_member_symbols().end() &&
                member->second && member->second->kind == SymbolKind::Field && !member->second->is_const;
            const auto owner_type = m_type_checker.get_type_of_node(member_access->object.get());
            const auto native_property = owner_type
                ? ApiCatalog::instance().find_available_property(owner_type->name, member_access->member_name)
                : nullptr;
            declaration->is_reference = !member_access->is_optional && reference_value &&
                (authored_mutable_field || (native_property && native_property->writable));
        }
        declaration->result_type = declaration->declared_type;
        declaration->span = v->span;

        IRStatement st;
        st.op = declaration;
        st.span = v->span;
        current_block.statements.push_back(std::move(st));
    } else if (auto e = dynamic_cast<const ExprStmt*>(stmt.get())) {
        auto op = lower_expression(e->expr);
        if (op) {
            IRStatement st;
            st.op = op;
            st.span = e->span;
            current_block.statements.push_back(st);
        }
    } else if (auto ret = dynamic_cast<const ReturnStmt*>(stmt.get())) {
        auto ir_ret = std::make_shared<IRReturn>();
        ir_ret->kind = IROpKind::Return;
        if (ret->value) {
            ir_ret->value = lower_expression(ret->value);
            ir_ret->result_type = ir_ret->value ? ir_ret->value->result_type : nullptr;
        }
        ir_ret->span = ret->span;

        IRStatement st;
        st.op = ir_ret;
        st.span = ret->span;
        current_block.statements.push_back(st);
    } else if (auto if_s = dynamic_cast<const IfStmt*>(stmt.get())) {
        auto branch = std::make_shared<IRIf>();
        branch->kind = IROpKind::If;
        branch->condition = lower_expression(if_s->condition);
        branch->span = if_s->span;

        IRBlock then_block;
        lower_statement(if_s->then_branch, then_block);
        for (auto& nested : then_block.statements) {
            branch->then_operations.push_back(std::move(nested.op));
        }
        if (if_s->else_branch) {
            IRBlock else_block;
            lower_statement(if_s->else_branch, else_block);
            for (auto& nested : else_block.statements) {
                branch->else_operations.push_back(std::move(nested.op));
            }
        }

        IRStatement st;
        st.op = std::move(branch);
        st.span = if_s->span;
        current_block.statements.push_back(std::move(st));
    } else if (auto binding = dynamic_cast<const OptionalBindingIfStmt*>(stmt.get())) {
        auto operation = std::make_shared<IROptionalBindingIf>();
        operation->kind = IROpKind::OptionalBindingIf;
        operation->name = binding->name;
        operation->optional_value = lower_expression(binding->optional_value);
        operation->span = binding->span;
        IRBlock then_block;
        lower_statement(binding->then_branch, then_block);
        for (auto& statement : then_block.statements) {
            operation->then_operations.push_back(std::move(statement.op));
        }
        if (binding->else_branch) {
            IRBlock else_block;
            lower_statement(binding->else_branch, else_block);
            for (auto& statement : else_block.statements) {
                operation->else_operations.push_back(std::move(statement.op));
            }
        }
        current_block.statements.push_back({operation, binding->span});
    } else if (auto guard = dynamic_cast<const GuardStmt*>(stmt.get())) {
        auto operation = std::make_shared<IRGuard>();
        operation->kind = IROpKind::Guard;
        operation->name = guard->name;
        operation->optional_value = lower_expression(guard->optional_value);
        operation->span = guard->span;
        IRBlock else_block;
        lower_statement(guard->else_branch, else_block);
        for (auto& statement : else_block.statements) {
            operation->else_operations.push_back(std::move(statement.op));
        }
        current_block.statements.push_back({operation, guard->span});
    } else if (auto match_statement = dynamic_cast<const MatchStmt*>(stmt.get())) {
        auto operation = std::make_shared<IRMatchStatement>();
        operation->kind = IROpKind::MatchStatement;
        operation->subject = lower_expression(match_statement->subject);
        operation->span = match_statement->span;
        for (const auto& arm : match_statement->arms) {
            IRMatchStatementArm lowered_arm;
            lowered_arm.pattern = lower_match_pattern(arm.pattern);
            IRBlock body;
            lower_statement(arm.body, body);
            for (auto& statement : body.statements) {
                lowered_arm.operations.push_back(std::move(statement.op));
            }
            operation->arms.push_back(std::move(lowered_arm));
        }
        current_block.statements.push_back({operation, match_statement->span});
    } else if (auto loop = dynamic_cast<const WhileStmt*>(stmt.get())) {
        auto operation = std::make_shared<IRWhile>();
        operation->kind = IROpKind::While;
        operation->condition = lower_expression(loop->condition);
        operation->span = loop->span;
        IRBlock body;
        lower_statement(loop->body, body);
        for (auto& statement : body.statements) operation->body.push_back(std::move(statement.op));
        current_block.statements.push_back({operation, loop->span});
    } else if (auto loop = dynamic_cast<const ForStmt*>(stmt.get())) {
        auto operation = std::make_shared<IRFor>();
        operation->kind = IROpKind::For;
        if (loop->initializer) {
            IRBlock initializer;
            lower_statement(loop->initializer, initializer);
            if (!initializer.statements.empty()) operation->initializer = std::move(initializer.statements.front().op);
        }
        operation->condition = lower_expression(loop->condition);
        operation->update = lower_expression(loop->update);
        operation->span = loop->span;
        IRBlock body;
        lower_statement(loop->body, body);
        for (auto& statement : body.statements) operation->body.push_back(std::move(statement.op));
        current_block.statements.push_back({operation, loop->span});
    } else if (auto loop = dynamic_cast<const ForInStmt*>(stmt.get())) {
        auto operation = std::make_shared<IRForIn>();
        operation->kind = IROpKind::ForIn;
        operation->variable_name = loop->var_name;
        operation->collection = lower_expression(loop->collection);
        operation->span = loop->span;
        IRBlock body;
        lower_statement(loop->body, body);
        for (auto& statement : body.statements) operation->body.push_back(std::move(statement.op));
        current_block.statements.push_back({operation, loop->span});
    } else if (dynamic_cast<const BreakStmt*>(stmt.get())) {
        auto operation = std::make_shared<IRBreak>();
        operation->kind = IROpKind::Break;
        operation->span = stmt->span;
        current_block.statements.push_back({operation, stmt->span});
    } else if (dynamic_cast<const ContinueStmt*>(stmt.get())) {
        auto operation = std::make_shared<IRContinue>();
        operation->kind = IROpKind::Continue;
        operation->span = stmt->span;
        current_block.statements.push_back({operation, stmt->span});
    }
}

std::shared_ptr<IROperation> IRLowering::lower_expression(const std::shared_ptr<Expression>& expr) {
    if (!expr) return nullptr;

    if (auto lit = dynamic_cast<const LiteralExpr*>(expr.get())) {
        auto c = std::make_shared<IRConstant>();
        c->kind = IROpKind::Constant;
        c->span = lit->span;
        switch (lit->kind) {
            case LiteralExpr::Kind::Integer:
                c->int_val = lit->int_val;
                c->result_type = Type::make_primitive(TypeKind::Int32);
                break;
            case LiteralExpr::Kind::Float:
                c->float_val = lit->float_val;
                c->result_type = Type::make_primitive(TypeKind::Float32);
                break;
            case LiteralExpr::Kind::Duration:
                // Desugared to duration seconds (float)
                c->float_val = lit->duration_seconds;
                c->result_type = Type::make_primitive(TypeKind::Float32);
                break;
            case LiteralExpr::Kind::String:
                c->string_val = lit->string_val;
                c->result_type = Type::make_primitive(TypeKind::String);
                break;
            case LiteralExpr::Kind::Boolean:
                c->bool_val = lit->bool_val;
                c->result_type = Type::make_primitive(TypeKind::Bool);
                break;
            case LiteralExpr::Kind::Null:
                c->is_null = true;
                c->result_type = m_type_checker.get_type_of_node(lit);
                if (!c->result_type || c->result_type->is_error()) {
                    c->result_type = Type::make_primitive(TypeKind::Null);
                }
                break;
        }
        return c;
    }

    if (auto id = dynamic_cast<const IdentifierExpr*>(expr.get())) {
        auto v = std::make_shared<IRVarRef>();
        v->kind = IROpKind::VarRef;
        v->name = id->name;
        v->result_type = m_type_checker.get_type_of_node(id);
        v->span = id->span;
        return v;
    }

    if (auto interpolated = dynamic_cast<const InterpolatedStringExpr*>(expr.get())) {
        auto value = std::make_shared<IRInterpolatedString>();
        value->kind = IROpKind::InterpolatedString;
        value->result_type = m_type_checker.get_type_of_node(interpolated);
        value->span = interpolated->span;
        for (const auto& segment : interpolated->segments) {
            IRInterpolatedStringSegment lowered;
            lowered.text = segment.text;
            lowered.format = segment.format;
            if (segment.expression) lowered.value = lower_expression(segment.expression);
            value->segments.push_back(std::move(lowered));
        }
        return value;
    }

    if (auto bin = dynamic_cast<const BinaryExpr*>(expr.get())) {
        auto b = std::make_shared<IRBinary>();
        b->kind = IROpKind::Binary;
        b->op = bin->op;
        b->left = lower_expression(bin->left);
        b->right = lower_expression(bin->right);
        b->result_type = m_type_checker.get_type_of_node(bin);
        b->span = bin->span;
        return b;
    }

    if (auto conditional = dynamic_cast<const ConditionalExpr*>(expr.get())) {
        auto value = std::make_shared<IRConditional>();
        value->kind = IROpKind::Conditional;
        value->condition = lower_expression(conditional->condition);
        value->then_value = lower_expression(conditional->then_value);
        value->else_value = lower_expression(conditional->else_value);
        value->result_type = m_type_checker.get_type_of_node(conditional);
        value->span = conditional->span;
        return value;
    }

    if (auto match_expression = dynamic_cast<const MatchExpr*>(expr.get())) {
        auto value = std::make_shared<IRMatchExpression>();
        value->kind = IROpKind::MatchExpression;
        value->subject = lower_expression(match_expression->subject);
        value->result_type = m_type_checker.get_type_of_node(match_expression);
        value->span = match_expression->span;
        for (const auto& arm : match_expression->arms) {
            value->arms.push_back({lower_match_pattern(arm.pattern), lower_expression(arm.value)});
        }
        return value;
    }

    if (auto un = dynamic_cast<const UnaryExpr*>(expr.get())) {
        auto u = std::make_shared<IRUnary>();
        u->kind = IROpKind::Unary;
        u->op = un->op;
        u->operand = lower_expression(un->operand);
        u->result_type = m_type_checker.get_type_of_node(un);
        u->span = un->span;
        return u;
    }

    if (auto assign = dynamic_cast<const AssignmentExpr*>(expr.get())) {
        if (auto mem = dynamic_cast<const MemberAccessExpr*>(assign->target.get())) {
            auto m_set = std::make_shared<IRMemberSet>();
            m_set->kind = IROpKind::MemberSet;
            m_set->target = lower_expression(mem->object);
            m_set->member_name = mem->member_name;
            m_set->assignment_op = assign->op;
            m_set->value = lower_expression(assign->value);
            m_set->result_type = m_set->value ? m_set->value->result_type : nullptr;
            m_set->span = assign->span;
            return m_set;
        }
        if (auto id = dynamic_cast<const IdentifierExpr*>(assign->target.get())) {
            auto a = std::make_shared<IRAssign>();
            a->kind = IROpKind::Assign;
            a->target_name = id->name;
            a->assignment_op = assign->op;
            a->value = lower_expression(assign->value);
            a->result_type = a->value ? a->value->result_type : nullptr;
            a->span = assign->span;
            return a;
        }
        if (auto index = dynamic_cast<const IndexExpr*>(assign->target.get())) {
            auto set = std::make_shared<IRIndexSet>();
            set->kind = IROpKind::IndexSet;
            set->target = lower_expression(index->object);
            set->index = lower_expression(index->index);
            set->value = lower_expression(assign->value);
            set->result_type = m_type_checker.get_type_of_node(assign);
            set->span = assign->span;
            return set;
        }
    }

    if (auto call = dynamic_cast<const CallExpr*>(expr.get())) {
        // Check if method call vs free function call
        if (auto mem = dynamic_cast<const MemberAccessExpr*>(call->callee.get())) {
            if (mem->is_optional) {
                auto opt = std::make_shared<IROptionalChain>();
                opt->kind = IROpKind::OptionalChain;
                opt->target = lower_expression(mem->object);
                opt->member_name = mem->member_name;
                opt->is_method_call = true;
                for (const auto& a : call->arguments) {
                    opt->method_args.push_back(lower_expression(a));
                }
                opt->result_type = m_type_checker.get_type_of_node(call);
                opt->span = call->span;
                return opt;
            } else {
                const auto target_type = m_type_checker.get_type_of_node(mem->object.get());
                if (target_type && target_type->name == "Events" && mem->member_name == "emit") {
                    auto event_emit = std::make_shared<IRTypedEventEmit>();
                    event_emit->kind = IROpKind::TypedEventEmit;
                    event_emit->target = lower_expression(mem->object);
                    if (!call->arguments.empty()) event_emit->event = lower_expression(call->arguments.front());
                    event_emit->result_type = m_type_checker.get_type_of_node(call);
                    event_emit->span = call->span;
                    return event_emit;
                }
                if (target_type && target_type->name == "Events" && mem->member_name == "next") {
                    auto event_next = std::make_shared<IRTypedEventNext>();
                    event_next->kind = IROpKind::TypedEventNext;
                    event_next->target = lower_expression(mem->object);
                    if (!call->type_arguments.empty()) event_next->event_type = call->type_arguments.front().to_string();
                    event_next->result_type = m_type_checker.get_type_of_node(call);
                    event_next->span = call->span;
                    return event_next;
                }
                if (target_type && target_type->name == "Assets" && mem->member_name == "load") {
                    auto asset_load = std::make_shared<IRAssetLoad>();
                    asset_load->kind = IROpKind::AssetLoad;
                    asset_load->target = lower_expression(mem->object);
                    if (!call->type_arguments.empty()) asset_load->asset_type = call->type_arguments.front().to_string();
                    if (!call->arguments.empty()) asset_load->path = lower_expression(call->arguments.front());
                    asset_load->result_type = m_type_checker.get_type_of_node(call);
                    asset_load->span = call->span;
                    return asset_load;
                }
                if (target_type && target_type->name == "Jobs" && mem->member_name == "run") {
                    auto job_run = std::make_shared<IRJobRun>();
                    job_run->kind = IROpKind::JobRun;
                    job_run->target = lower_expression(mem->object);
                    if (!call->arguments.empty()) job_run->work = lower_expression(call->arguments.front());
                    job_run->result_type = m_type_checker.get_type_of_node(call);
                    job_run->span = call->span;
                    return job_run;
                }
                auto mc = std::make_shared<IRMethodCall>();
                mc->kind = IROpKind::MethodCall;
                mc->target = lower_expression(mem->object);
                mc->method_name = mem->member_name;
                for (const auto& type_argument : call->type_arguments) {
                    mc->type_arguments.push_back(type_argument.to_string());
                }
                for (const auto& a : call->arguments) {
                    mc->args.push_back(lower_expression(a));
                }
                mc->result_type = m_type_checker.get_type_of_node(call);
                mc->span = call->span;
                return mc;
            }
        }
        if (auto id = dynamic_cast<const IdentifierExpr*>(call->callee.get())) {
            if (id->name == "String") {
                auto conversion = std::make_shared<IRStringConvert>();
                conversion->kind = IROpKind::StringConvert;
                if (call->arguments.size() == 1) {
                    conversion->operand = lower_expression(call->arguments.front());
                }
                conversion->result_type = m_type_checker.get_type_of_node(call);
                conversion->span = call->span;
                return conversion;
            }
            if (const auto target_kind = numeric_type_for_name(id->name)) {
                auto conversion = std::make_shared<IRNumericConvert>();
                conversion->kind = IROpKind::NumericConvert;
                conversion->target_kind = *target_kind;
                if (call->arguments.size() == 1) {
                    conversion->operand = lower_expression(call->arguments.front());
                }
                conversion->result_type = m_type_checker.get_type_of_node(call);
                conversion->span = call->span;
                return conversion;
            }
            if (value_constructor_width(id->name)) {
                auto construction = std::make_shared<IRValueConstruct>();
                construction->kind = IROpKind::ValueConstruct;
                construction->target_kind = m_type_checker.get_type_of_node(call)->kind;
                for (const auto& argument : call->arguments) {
                    construction->args.push_back(lower_expression(argument));
                }
                construction->result_type = m_type_checker.get_type_of_node(call);
                construction->span = call->span;
                return construction;
            }
            if (id->name == "Weak") {
                auto construction = std::make_shared<IRWeakConstruct>();
                construction->kind = IROpKind::WeakConstruct;
                if (!call->arguments.empty()) construction->value = lower_expression(call->arguments.front());
                construction->result_type = m_type_checker.get_type_of_node(call);
                construction->span = call->span;
                return construction;
            }
            const auto call_type = m_type_checker.get_type_of_node(call);
            const auto callee_type = m_type_checker.get_type_of_node(id);
            if (call_type && callee_type && call_type->kind == TypeKind::Class && callee_type->kind == TypeKind::Class) {
                auto construction = std::make_shared<IRClassConstruct>();
                construction->kind = IROpKind::ClassConstruct;
                construction->class_name = call_type->name;
                for (const auto& argument : call->arguments) construction->args.push_back(lower_expression(argument));
                construction->result_type = call_type;
                construction->span = call->span;
                return construction;
            }
            auto fc = std::make_shared<IRCall>();
            fc->kind = IROpKind::Call;
            fc->callee_name = id->name;
            for (const auto& type_argument : call->type_arguments) {
                fc->type_arguments.push_back(m_type_checker.resolve_type_annotation(type_argument));
            }
            for (const auto& a : call->arguments) {
                fc->args.push_back(lower_expression(a));
            }
            fc->result_type = m_type_checker.get_type_of_node(call);
            fc->span = call->span;
            return fc;
        }
    }

    if (auto mem = dynamic_cast<const MemberAccessExpr*>(expr.get())) {
        if (mem->is_optional) {
            auto opt = std::make_shared<IROptionalChain>();
            opt->kind = IROpKind::OptionalChain;
            opt->target = lower_expression(mem->object);
            opt->member_name = mem->member_name;
            opt->is_method_call = false;
            opt->result_type = m_type_checker.get_type_of_node(mem);
            opt->span = mem->span;
            return opt;
        } else {
            auto mg = std::make_shared<IRMemberGet>();
            mg->kind = IROpKind::MemberGet;
            mg->target = lower_expression(mem->object);
            mg->member_name = mem->member_name;
            mg->result_type = m_type_checker.get_type_of_node(mem);
            mg->span = mem->span;
            return mg;
        }
    }

    if (auto idx = dynamic_cast<const IndexExpr*>(expr.get())) {
        auto get = std::make_shared<IRIndexGet>();
        get->kind = IROpKind::IndexGet;
        get->target = lower_expression(idx->object);
        get->index = lower_expression(idx->index);
        get->result_type = m_type_checker.get_type_of_node(idx);
        get->span = idx->span;
        return get;
    }

    if (auto arr = dynamic_cast<const ArrayLiteralExpr*>(expr.get())) {
        auto literal = std::make_shared<IRArrayLiteral>();
        literal->kind = IROpKind::ArrayLiteral;
        for (const auto& element : arr->elements) literal->elements.push_back(lower_expression(element));
        literal->result_type = m_type_checker.get_type_of_node(arr);
        literal->span = arr->span;
        return literal;
    }

    if (auto collection = dynamic_cast<const CollectionLiteralExpr*>(expr.get())) {
        auto literal = std::make_shared<IRCollectionLiteral>();
        literal->kind = IROpKind::CollectionLiteral;
        literal->result_type = m_type_checker.get_type_of_node(collection);
        literal->collection_kind = literal->result_type ? literal->result_type->kind : TypeKind::Error;
        for (const auto& [key, value] : collection->map_entries) {
            literal->map_entries.emplace_back(lower_expression(key), lower_expression(value));
        }
        for (const auto& element : collection->elements) literal->elements.push_back(lower_expression(element));
        literal->span = collection->span;
        return literal;
    }

    if (auto object = dynamic_cast<const ObjectLiteralExpr*>(expr.get())) {
        auto literal = std::make_shared<IRStructLiteral>();
        literal->kind = IROpKind::StructLiteral;
        literal->result_type = m_type_checker.get_type_of_node(object);
        literal->struct_name = literal->result_type ? literal->result_type->name : std::string{};
        for (const auto& entry : object->entries) literal->fields.emplace_back(entry.key, lower_expression(entry.value));
        literal->span = object->span;
        return literal;
    }

    if (auto self = dynamic_cast<const ThisExpr*>(expr.get())) {
        auto reference = std::make_shared<IRThisRef>();
        reference->kind = IROpKind::ThisRef;
        reference->result_type = m_type_checker.get_type_of_node(self);
        reference->span = self->span;
        return reference;
    }

    if (auto arrow = dynamic_cast<const ArrowFunctionExpr*>(expr.get())) {
        auto closure = std::make_shared<IRClosure>();
        closure->kind = IROpKind::Closure;
        closure->result_type = m_type_checker.get_type_of_node(arrow);
        for (const auto& parameter : arrow->params) {
            closure->params.push_back({parameter->name, m_type_checker.resolve_type_annotation(parameter->type_annotation)});
        }
        if (arrow->body_expr) closure->body_expression = lower_expression(arrow->body_expr);
        if (arrow->body_stmt) {
            IRBlock body;
            lower_statement(arrow->body_stmt, body);
            for (auto& statement : body.statements) closure->body_operations.push_back(std::move(statement.op));
        }
        closure->span = arrow->span;
        return closure;
    }

    if (auto aw = dynamic_cast<const AwaitExpr*>(expr.get())) {
        auto ir_aw = std::make_shared<IRAwait>();
        ir_aw->kind = IROpKind::Await;
        ir_aw->operand = lower_expression(aw->operand);
        ir_aw->result_type = m_type_checker.get_type_of_node(aw);
        ir_aw->span = aw->span;
        return ir_aw;
    }

    return nullptr;
}

} // namespace EngineScript
