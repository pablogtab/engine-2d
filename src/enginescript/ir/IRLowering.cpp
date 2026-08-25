#include "IRLowering.hpp"

namespace EngineScript {

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
        } else if (auto fn = dynamic_cast<const FunctionDecl*>(decl.get())) {
            ir_mod.functions.push_back(lower_function(*fn));
        }
    }

    return ir_mod;
}

IRClass IRLowering::lower_class(const ClassDecl& cls) {
    IRClass ir_cls;
    ir_cls.name = cls.name;
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
            param.type = m_type_checker.resolve_type_annotation(p->type_annotation);
            for (const auto& d : p->decorators) {
                param.decorators.push_back(d.name);
            }
            ir_cls.constructor_params.push_back(param);
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
    for (const auto& prop : st.properties) {
        ir_st.properties.push_back(lower_property(*prop));
    }
    for (const auto& m : st.methods) {
        ir_st.methods.push_back(lower_method(*m));
    }
    return ir_st;
}

IRProperty IRLowering::lower_property(const PropertyDecl& prop) {
    IRProperty ir_prop;
    ir_prop.name = prop.name;
    ir_prop.is_public = prop.access == AccessModifier::Public;
    ir_prop.type = prop.type_annotation ? m_type_checker.resolve_type_annotation(*prop.type_annotation) : m_type_checker.get_type_of_node(&prop);

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
    ir_m.is_async = m.is_async;
    ir_m.is_override = m.is_override;
    ir_m.is_public = m.access == AccessModifier::Public;
    ir_m.return_type = m.return_type ? m_type_checker.resolve_type_annotation(*m.return_type) : Type::make_primitive(TypeKind::Void);

    for (const auto& d : m.decorators) {
        ir_m.decorators.push_back(d.name);
    }

    for (const auto& p : m.params) {
        IRParam param;
        param.name = p->name;
        param.type = m_type_checker.resolve_type_annotation(p->type_annotation);
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
    ir_fn.is_async = fn.is_async;
    ir_fn.return_type = fn.return_type ? m_type_checker.resolve_type_annotation(*fn.return_type) : Type::make_primitive(TypeKind::Void);

    for (const auto& p : fn.params) {
        IRParam param;
        param.name = p->name;
        param.type = m_type_checker.resolve_type_annotation(p->type_annotation);
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
        declaration->name = v->name;
        declaration->is_const = v->is_const;
        declaration->declared_type = v->type_annotation
            ? m_type_checker.resolve_type_annotation(*v->type_annotation)
            : m_type_checker.get_type_of_node(v);
        declaration->initializer = lower_expression(v->initializer);
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
    }
}

std::shared_ptr<IROperation> IRLowering::lower_expression(const std::shared_ptr<Expression>& expr) {
    if (!expr) return nullptr;

    if (auto lit = dynamic_cast<const LiteralExpr*>(expr.get())) {
        auto c = std::make_shared<IRConstant>();
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
                c->result_type = Type::make_primitive(TypeKind::Null);
                break;
        }
        return c;
    }

    if (auto id = dynamic_cast<const IdentifierExpr*>(expr.get())) {
        auto v = std::make_shared<IRVarRef>();
        v->name = id->name;
        v->result_type = m_type_checker.get_type_of_node(id);
        v->span = id->span;
        return v;
    }

    if (auto bin = dynamic_cast<const BinaryExpr*>(expr.get())) {
        auto b = std::make_shared<IRBinary>();
        b->op = bin->op;
        b->left = lower_expression(bin->left);
        b->right = lower_expression(bin->right);
        b->result_type = m_type_checker.get_type_of_node(bin);
        b->span = bin->span;
        return b;
    }

    if (auto un = dynamic_cast<const UnaryExpr*>(expr.get())) {
        auto u = std::make_shared<IRUnary>();
        u->op = un->op;
        u->operand = lower_expression(un->operand);
        u->result_type = m_type_checker.get_type_of_node(un);
        u->span = un->span;
        return u;
    }

    if (auto assign = dynamic_cast<const AssignmentExpr*>(expr.get())) {
        if (auto mem = dynamic_cast<const MemberAccessExpr*>(assign->target.get())) {
            auto m_set = std::make_shared<IRMemberSet>();
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
            a->target_name = id->name;
            a->assignment_op = assign->op;
            a->value = lower_expression(assign->value);
            a->result_type = a->value ? a->value->result_type : nullptr;
            a->span = assign->span;
            return a;
        }
    }

    if (auto call = dynamic_cast<const CallExpr*>(expr.get())) {
        // Check if method call vs free function call
        if (auto mem = dynamic_cast<const MemberAccessExpr*>(call->callee.get())) {
            if (mem->is_optional) {
                auto opt = std::make_shared<IROptionalChain>();
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
                auto mc = std::make_shared<IRMethodCall>();
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
            auto fc = std::make_shared<IRCall>();
            fc->callee_name = id->name;
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
            opt->target = lower_expression(mem->object);
            opt->member_name = mem->member_name;
            opt->is_method_call = false;
            opt->result_type = m_type_checker.get_type_of_node(mem);
            opt->span = mem->span;
            return opt;
        } else {
            auto mg = std::make_shared<IRMemberGet>();
            mg->target = lower_expression(mem->object);
            mg->member_name = mem->member_name;
            mg->result_type = m_type_checker.get_type_of_node(mem);
            mg->span = mem->span;
            return mg;
        }
    }

    if (auto idx = dynamic_cast<const IndexExpr*>(expr.get())) {
        auto get = std::make_shared<IRIndexGet>();
        get->target = lower_expression(idx->object);
        get->index = lower_expression(idx->index);
        get->result_type = m_type_checker.get_type_of_node(idx);
        get->span = idx->span;
        return get;
    }

    if (auto arr = dynamic_cast<const ArrayLiteralExpr*>(expr.get())) {
        auto call = std::make_shared<IRCall>();
        call->callee_name = "__array_literal";
        for (const auto& element : arr->elements) call->args.push_back(lower_expression(element));
        call->result_type = m_type_checker.get_type_of_node(arr);
        call->span = arr->span;
        return call;
    }

    if (auto aw = dynamic_cast<const AwaitExpr*>(expr.get())) {
        auto ir_aw = std::make_shared<IRAwait>();
        ir_aw->operand = lower_expression(aw->operand);
        ir_aw->result_type = m_type_checker.get_type_of_node(aw);
        ir_aw->span = aw->span;
        return ir_aw;
    }

    return nullptr;
}

} // namespace EngineScript
