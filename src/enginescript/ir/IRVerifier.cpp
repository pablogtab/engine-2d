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
    return true;
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

    if (auto bin = dynamic_cast<const IRBinary*>(op)) {
        if (!bin->left || !bin->right) {
            m_diagnostics.report_error("E4020", "IRBinary missing left or right operand", bin->span);
            return false;
        }
        return verify_operation(bin->left.get()) && verify_operation(bin->right.get());
    }

    if (auto un = dynamic_cast<const IRUnary*>(op)) {
        if (!un->operand) {
            m_diagnostics.report_error("E4021", "IRUnary missing operand", un->span);
            return false;
        }
        return verify_operation(un->operand.get());
    }

    if (auto assign = dynamic_cast<const IRAssign*>(op)) {
        if (!assign->value) {
            m_diagnostics.report_error("E4022", "IRAssign missing value operand", assign->span);
            return false;
        }
        return verify_operation(assign->value.get());
    }

    if (auto declaration = dynamic_cast<const IRVarDecl*>(op)) {
        if (declaration->name.empty() || !declaration->declared_type) {
            m_diagnostics.report_error("E4023", "IRVarDecl is missing its name or type", declaration->span);
            return false;
        }
        return !declaration->initializer || verify_operation(declaration->initializer.get());
    }

    if (auto branch = dynamic_cast<const IRIf*>(op)) {
        if (!branch->condition || !verify_operation(branch->condition.get())) {
            m_diagnostics.report_error("E4024", "IRIf is missing a valid condition", branch->span);
            return false;
        }
        for (const auto& nested : branch->then_operations) {
            if (!verify_operation(nested.get())) return false;
        }
        for (const auto& nested : branch->else_operations) {
            if (!verify_operation(nested.get())) return false;
        }
    }

    return true;
}

} // namespace EngineScript
