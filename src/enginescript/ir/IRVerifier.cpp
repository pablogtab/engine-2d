#include "IRVerifier.hpp"

namespace EngineScript {

IRVerifier::IRVerifier(DiagnosticEngine& diagnostics)
    : m_diagnostics(diagnostics) {
}

bool IRVerifier::verify_module(const IRModule& module) {
    bool ok = true;
    for (const auto& cls : module.classes) {
        if (!verify_class(cls)) ok = false;
    }
    for (const auto& st : module.structs) {
        if (!verify_struct(st)) ok = false;
    }
    for (const auto& fn : module.functions) {
        if (!verify_function(fn)) ok = false;
    }
    return ok;
}

bool IRVerifier::verify_class(const IRClass& cls) {
    if (cls.name.empty()) {
        m_diagnostics.report_error("E4001", "IRClass has empty name", {});
        return false;
    }
    bool ok = true;
    for (const auto& property : cls.properties) {
        if (property.initializer && !verify_operation(property.initializer.get())) ok = false;
    }
    for (const auto& m : cls.methods) {
        for (const auto& b : m.blocks) {
            if (!verify_block(b)) ok = false;
        }
    }
    return ok;
}

bool IRVerifier::verify_struct(const IRStruct& st) {
    if (st.name.empty()) {
        m_diagnostics.report_error("E4002", "IRStruct has empty name", {});
        return false;
    }
    bool ok = true;
    for (const auto& property : st.properties) {
        if (property.initializer && !verify_operation(property.initializer.get())) ok = false;
    }
    for (const auto& method : st.methods) {
        for (const auto& block : method.blocks) if (!verify_block(block)) ok = false;
    }
    return ok;
}

bool IRVerifier::verify_function(const IRFunction& fn) {
    if (fn.name.empty()) {
        m_diagnostics.report_error("E4003", "IRFunction has empty name", {});
        return false;
    }
    bool ok = true;
    for (const auto& b : fn.blocks) {
        if (!verify_block(b)) ok = false;
    }
    return ok;
}

bool IRVerifier::verify_block(const IRBlock& block) {
    bool ok = true;
    for (const auto& st : block.statements) {
        if (!st.op) {
            m_diagnostics.report_error("E4010", "IRStatement contains null operation", st.span);
            ok = false;
        } else {
            if (!verify_operation(st.op.get())) ok = false;
        }
    }
    return ok;
}

bool IRVerifier::verify_operation(const IROperation* op) {
    if (!op) return false;
    const auto fail = [this, op](std::string message) {
        m_diagnostics.report_error("E4099", std::move(message), op->span);
        return false;
    };
    const auto verify_all = [this](const auto& operations) {
        for (const auto& child : operations) if (!child || !verify_operation(child.get())) return false;
        return true;
    };

    switch (op->kind) {
        case IROpKind::Constant:
            return dynamic_cast<const IRConstant*>(op) && op->result_type;
        case IROpKind::VarRef: {
            const auto* value = dynamic_cast<const IRVarRef*>(op);
            return value && !value->name.empty() && value->result_type;
        }
        case IROpKind::VarDecl: {
            const auto* value = dynamic_cast<const IRVarDecl*>(op);
            return value && !value->name.empty() && value->declared_type &&
                (!value->initializer || verify_operation(value->initializer.get()));
        }
        case IROpKind::Assign: {
            const auto* value = dynamic_cast<const IRAssign*>(op);
            return value && !value->target_name.empty() && value->value && verify_operation(value->value.get());
        }
        case IROpKind::Binary: {
            const auto* value = dynamic_cast<const IRBinary*>(op);
            return value && value->left && value->right && verify_operation(value->left.get()) && verify_operation(value->right.get());
        }
        case IROpKind::Unary: {
            const auto* value = dynamic_cast<const IRUnary*>(op);
            return value && value->operand && verify_operation(value->operand.get());
        }
        case IROpKind::NumericConvert: {
            const auto* value = dynamic_cast<const IRNumericConvert*>(op);
            if (!value || !numeric_type_info(value->target_kind) || !value->result_type ||
                value->result_type->kind != value->target_kind) {
                m_diagnostics.report_error("E4025", "IRNumericConvert has an invalid numeric target", op->span);
                return false;
            }
            if (!value->operand || !value->operand->result_type || !value->operand->result_type->is_numeric()) {
                m_diagnostics.report_error("E4026", "IRNumericConvert requires one numeric operand", op->span);
                return false;
            }
            return verify_operation(value->operand.get());
        }
        case IROpKind::StringConvert: {
            const auto* value = dynamic_cast<const IRStringConvert*>(op);
            if (!value || !value->result_type || value->result_type->kind != TypeKind::String) {
                m_diagnostics.report_error("E4039", "IRStringConvert must produce String", op->span);
                return false;
            }
            if (!value->operand || !value->operand->result_type || !value->operand->result_type->is_numeric()) {
                m_diagnostics.report_error("E4040", "IRStringConvert requires one numeric operand", op->span);
                return false;
            }
            return verify_operation(value->operand.get());
        }
        case IROpKind::ValueConstruct: {
            const auto* value = dynamic_cast<const IRValueConstruct*>(op);
            const std::size_t expected = value && value->target_kind == TypeKind::Vec2 ? 2 :
                (value && value->target_kind == TypeKind::Vec3 ? 3 :
                    (value && (value->target_kind == TypeKind::Vec4 || value->target_kind == TypeKind::Color) ? 4 : 0));
            if (!value || expected == 0 || value->args.size() != expected) {
                m_diagnostics.report_error("E4027", "IRValueConstruct has an invalid target or arity", op->span);
                return false;
            }
            if (!verify_all(value->args)) {
                m_diagnostics.report_error("E4028", "IRValueConstruct requires valid numeric operands", op->span);
                return false;
            }
            return true;
        }
        case IROpKind::ClassConstruct: {
            const auto* value = dynamic_cast<const IRClassConstruct*>(op);
            return value && !value->class_name.empty() && verify_all(value->args);
        }
        case IROpKind::WeakConstruct: {
            const auto* value = dynamic_cast<const IRWeakConstruct*>(op);
            return value && value->result_type && value->result_type->kind == TypeKind::Weak && value->value &&
                verify_operation(value->value.get());
        }
        case IROpKind::ArrayLiteral: {
            const auto* value = dynamic_cast<const IRArrayLiteral*>(op);
            return value && value->result_type && verify_all(value->elements);
        }
        case IROpKind::CollectionLiteral: {
            const auto* value = dynamic_cast<const IRCollectionLiteral*>(op);
            if (!value || !value->result_type || !verify_all(value->elements)) return false;
            for (const auto& [key, mapped] : value->map_entries) {
                if (!key || !mapped || !verify_operation(key.get()) || !verify_operation(mapped.get())) return false;
            }
            return true;
        }
        case IROpKind::StructLiteral: {
            const auto* value = dynamic_cast<const IRStructLiteral*>(op);
            if (!value || value->struct_name.empty()) return false;
            for (const auto& [name, field] : value->fields) if (name.empty() || !field || !verify_operation(field.get())) return false;
            return true;
        }
        case IROpKind::ThisRef:
            return dynamic_cast<const IRThisRef*>(op) && op->result_type;
        case IROpKind::Closure: {
            const auto* value = dynamic_cast<const IRClosure*>(op);
            return value && verify_all(value->body_operations) &&
                (!value->body_expression || verify_operation(value->body_expression.get()));
        }
        case IROpKind::TypedEventEmit: {
            const auto* value = dynamic_cast<const IRTypedEventEmit*>(op);
            return value && value->target && value->event && verify_operation(value->target.get()) &&
                verify_operation(value->event.get());
        }
        case IROpKind::TypedEventNext: {
            const auto* value = dynamic_cast<const IRTypedEventNext*>(op);
            return value && value->target && !value->event_type.empty() && verify_operation(value->target.get());
        }
        case IROpKind::AssetLoad: {
            const auto* value = dynamic_cast<const IRAssetLoad*>(op);
            return value && value->target && value->path && !value->asset_type.empty() &&
                verify_operation(value->target.get()) && verify_operation(value->path.get());
        }
        case IROpKind::JobRun: {
            const auto* value = dynamic_cast<const IRJobRun*>(op);
            return value && value->target && value->work && verify_operation(value->target.get()) &&
                verify_operation(value->work.get());
        }
        case IROpKind::Call: {
            const auto* value = dynamic_cast<const IRCall*>(op);
            return value && !value->callee_name.empty() &&
                std::all_of(value->type_arguments.begin(), value->type_arguments.end(),
                    [](const auto& type) { return type && !type->is_error(); }) &&
                verify_all(value->args);
        }
        case IROpKind::MethodCall: {
            const auto* value = dynamic_cast<const IRMethodCall*>(op);
            return value && value->target && !value->method_name.empty() && verify_operation(value->target.get()) && verify_all(value->args);
        }
        case IROpKind::MemberGet: {
            const auto* value = dynamic_cast<const IRMemberGet*>(op);
            return value && value->target && !value->member_name.empty() && verify_operation(value->target.get());
        }
        case IROpKind::MemberSet: {
            const auto* value = dynamic_cast<const IRMemberSet*>(op);
            return value && value->target && value->value && !value->member_name.empty() &&
                verify_operation(value->target.get()) && verify_operation(value->value.get());
        }
        case IROpKind::OptionalChain: {
            const auto* value = dynamic_cast<const IROptionalChain*>(op);
            return value && value->target && !value->member_name.empty() &&
                verify_operation(value->target.get()) && verify_all(value->method_args);
        }
        case IROpKind::IndexGet: {
            const auto* value = dynamic_cast<const IRIndexGet*>(op);
            return value && value->target && value->index && verify_operation(value->target.get()) && verify_operation(value->index.get());
        }
        case IROpKind::IndexSet: {
            const auto* value = dynamic_cast<const IRIndexSet*>(op);
            return value && value->target && value->index && value->value && verify_operation(value->target.get()) &&
                verify_operation(value->index.get()) && verify_operation(value->value.get());
        }
        case IROpKind::Return: {
            const auto* value = dynamic_cast<const IRReturn*>(op);
            return value && (!value->value || verify_operation(value->value.get()));
        }
        case IROpKind::If: {
            const auto* value = dynamic_cast<const IRIf*>(op);
            return value && value->condition && verify_operation(value->condition.get()) &&
                verify_all(value->then_operations) && verify_all(value->else_operations);
        }
        case IROpKind::While: {
            const auto* value = dynamic_cast<const IRWhile*>(op);
            return value && value->condition && verify_operation(value->condition.get()) && verify_all(value->body);
        }
        case IROpKind::For: {
            const auto* value = dynamic_cast<const IRFor*>(op);
            return value && (!value->initializer || verify_operation(value->initializer.get())) &&
                (!value->condition || verify_operation(value->condition.get())) &&
                (!value->update || verify_operation(value->update.get())) && verify_all(value->body);
        }
        case IROpKind::ForIn: {
            const auto* value = dynamic_cast<const IRForIn*>(op);
            return value && !value->variable_name.empty() && value->collection &&
                verify_operation(value->collection.get()) && verify_all(value->body);
        }
        case IROpKind::Break:
            return dynamic_cast<const IRBreak*>(op) != nullptr;
        case IROpKind::Continue:
            return dynamic_cast<const IRContinue*>(op) != nullptr;
        case IROpKind::Await: {
            const auto* value = dynamic_cast<const IRAwait*>(op);
            return value && value->operand && verify_operation(value->operand.get());
        }
        case IROpKind::Branch:
        case IROpKind::Jump:
            return fail("Unsupported control-flow operation in structured TypedIR");
    }
    return fail("Unknown TypedIR operation");
}

} // namespace EngineScript
