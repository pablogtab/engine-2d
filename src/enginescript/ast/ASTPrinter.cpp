#include "ASTPrinter.hpp"
#include <format>

namespace EngineScript {

std::string ASTPrinter::make_indent(int indent) {
    return std::string(indent * 2, ' ');
}

std::string ASTPrinter::print(const ModuleNode& module) {
    std::string out = std::format("Module ({})\n", module.module_path);
    for (const auto& decl : module.declarations) {
        if (decl) {
            print_decl(*decl, out, 1);
        }
    }
    return out;
}

std::string ASTPrinter::print(const ASTNode& node) {
    std::string out;
    if (auto d = dynamic_cast<const Declaration*>(&node)) {
        print_decl(*d, out, 0);
    } else if (auto s = dynamic_cast<const Statement*>(&node)) {
        print_stmt(*s, out, 0);
    } else if (auto e = dynamic_cast<const Expression*>(&node)) {
        print_expr(*e, out, 0);
    }
    return out;
}

void ASTPrinter::print_decl(const Declaration& decl, std::string& out, int indent) {
    std::string ind = make_indent(indent);

    for (const auto& dec : decl.decorators) {
        out += std::format("{}@{}", ind, dec.name);
        if (!dec.positional_args.empty() || !dec.named_args.empty()) {
            out += "(";
            for (size_t i = 0; i < dec.positional_args.size(); ++i) {
                out += ASTPrinter::print(*dec.positional_args[i]);
                if (i + 1 < dec.positional_args.size() || !dec.named_args.empty()) out += ", ";
            }
            for (size_t i = 0; i < dec.named_args.size(); ++i) {
                out += std::format("{}: {}", dec.named_args[i].key, ASTPrinter::print(*dec.named_args[i].value));
                if (i + 1 < dec.named_args.size()) out += ", ";
            }
            out += ")";
        }
        out += "\n";
    }

    if (auto imp = dynamic_cast<const ImportDecl*>(&decl)) {
        out += std::format("{}Import from \"{}\"\n", ind, imp->module_path);
        for (const auto& item : imp->items) {
            out += std::format("{}  - {}{}\n", ind, item.name, item.alias ? (" as " + *item.alias) : "");
        }
    } else if (auto cls = dynamic_cast<const ClassDecl*>(&decl)) {
        out += std::format("{}{}Class {}", ind, cls->is_exported ? "Export " : "", cls->name);
        if (cls->base_class) out += std::format(" extends {}", cls->base_class->to_string());
        out += "\n";
        if (cls->constructor_decl) {
            out += std::format("{}  Constructor\n", ind);
            for (const auto& p : cls->constructor_decl->params) {
                out += std::format("{}    Param {}: {}\n", ind, p->name, p->type_annotation.to_string());
            }
        }
        for (const auto& prop : cls->properties) {
            print_decl(*prop, out, indent + 1);
        }
        for (const auto& m : cls->methods) {
            print_decl(*m, out, indent + 1);
        }
    } else if (auto st = dynamic_cast<const StructDecl*>(&decl)) {
        out += std::format("{}{}Struct {}\n", ind, st->is_exported ? "Export " : "", st->name);
        for (const auto& prop : st->properties) {
            print_decl(*prop, out, indent + 1);
        }
    } else if (auto prop = dynamic_cast<const PropertyDecl*>(&decl)) {
        out += std::format("{}Property {}: {}\n", ind, prop->name, prop->type_annotation ? prop->type_annotation->to_string() : "<inferred>");
    } else if (auto m = dynamic_cast<const MethodDecl*>(&decl)) {
        out += std::format("{}{}Method {}(...): {}\n", ind, m->is_async ? "Async " : "", m->name, m->return_type ? m->return_type->to_string() : "Void");
    } else if (auto fn = dynamic_cast<const FunctionDecl*>(&decl)) {
        out += std::format("{}{}Function {}(...): {}\n", ind, fn->is_async ? "Async " : "", fn->name, fn->return_type ? fn->return_type->to_string() : "Void");
    }
}

void ASTPrinter::print_stmt(const Statement& stmt, std::string& out, int indent) {
    std::string ind = make_indent(indent);
    if (auto blk = dynamic_cast<const BlockStmt*>(&stmt)) {
        out += std::format("{}Block\n", ind);
        for (const auto& s : blk->statements) {
            print_stmt(*s, out, indent + 1);
        }
    } else if (auto v = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        out += std::format("{}VarDecl {} {}: {}\n", ind, v->is_const ? "const" : "let", v->name, v->type_annotation ? v->type_annotation->to_string() : "<inferred>");
    } else if (auto e = dynamic_cast<const ExprStmt*>(&stmt)) {
        out += std::format("{}ExprStmt\n", ind);
        print_expr(*e->expr, out, indent + 1);
    } else if (auto ret = dynamic_cast<const ReturnStmt*>(&stmt)) {
        out += std::format("{}Return\n", ind);
        if (ret->value) print_expr(*ret->value, out, indent + 1);
    } else if (auto if_s = dynamic_cast<const IfStmt*>(&stmt)) {
        out += std::format("{}If\n", ind);
        print_expr(*if_s->condition, out, indent + 1);
        print_stmt(*if_s->then_branch, out, indent + 1);
        if (if_s->else_branch) print_stmt(*if_s->else_branch, out, indent + 1);
    } else if (auto binding = dynamic_cast<const OptionalBindingIfStmt*>(&stmt)) {
        out += std::format("{}IfLet({})\n", ind, binding->name);
        print_expr(*binding->optional_value, out, indent + 1);
        print_stmt(*binding->then_branch, out, indent + 1);
        if (binding->else_branch) print_stmt(*binding->else_branch, out, indent + 1);
    } else if (auto guard = dynamic_cast<const GuardStmt*>(&stmt)) {
        out += std::format("{}GuardLet({})\n", ind, guard->name);
        print_expr(*guard->optional_value, out, indent + 1);
        print_stmt(*guard->else_branch, out, indent + 1);
    } else if (auto match = dynamic_cast<const MatchStmt*>(&stmt)) {
        out += std::format("{}Match\n", ind);
        print_expr(*match->subject, out, indent + 1);
        for (const auto& arm : match->arms) print_stmt(*arm.body, out, indent + 1);
    }
}

void ASTPrinter::print_expr(const Expression& expr, std::string& out, int indent) {
    std::string ind = make_indent(indent);
    if (auto lit = dynamic_cast<const LiteralExpr*>(&expr)) {
        switch (lit->kind) {
            case LiteralExpr::Kind::Integer:  out += std::format("{}Literal(Int: {})\n", ind, lit->int_val); break;
            case LiteralExpr::Kind::Float:    out += std::format("{}Literal(Float: {})\n", ind, lit->float_val); break;
            case LiteralExpr::Kind::Duration: out += std::format("{}Literal(Duration: {}s)\n", ind, lit->duration_seconds); break;
            case LiteralExpr::Kind::String:   out += std::format("{}Literal(String: \"{}\")\n", ind, lit->string_val); break;
            case LiteralExpr::Kind::Boolean:  out += std::format("{}Literal(Bool: {})\n", ind, lit->bool_val ? "true" : "false"); break;
            case LiteralExpr::Kind::Null:     out += std::format("{}Literal(Null)\n", ind); break;
        }
    } else if (auto interpolated = dynamic_cast<const InterpolatedStringExpr*>(&expr)) {
        out += std::format("{}InterpolatedString\n", ind);
        for (const auto& segment : interpolated->segments) {
            if (!segment.text.empty()) out += std::format("{}  Text({})\n", ind, segment.text);
            if (segment.expression) {
                out += std::format("{}  Expression{}\n", ind,
                    segment.format.empty() ? "" : std::format("(:{})", segment.format));
                print_expr(*segment.expression, out, indent + 2);
            }
        }
    } else if (auto id = dynamic_cast<const IdentifierExpr*>(&expr)) {
        out += std::format("{}Identifier({})\n", ind, id->name);
    } else if (dynamic_cast<const ThisExpr*>(&expr)) {
        out += std::format("{}This\n", ind);
    } else if (auto bin = dynamic_cast<const BinaryExpr*>(&expr)) {
        out += std::format("{}BinaryOp({})\n", ind, token_type_to_string(bin->op));
        print_expr(*bin->left, out, indent + 1);
        print_expr(*bin->right, out, indent + 1);
    } else if (auto conditional = dynamic_cast<const ConditionalExpr*>(&expr)) {
        out += std::format("{}Conditional\n", ind);
        print_expr(*conditional->condition, out, indent + 1);
        print_expr(*conditional->then_value, out, indent + 1);
        print_expr(*conditional->else_value, out, indent + 1);
    } else if (auto match = dynamic_cast<const MatchExpr*>(&expr)) {
        out += std::format("{}MatchExpression\n", ind);
        print_expr(*match->subject, out, indent + 1);
        for (const auto& arm : match->arms) print_expr(*arm.value, out, indent + 1);
    } else if (auto call = dynamic_cast<const CallExpr*>(&expr)) {
        out += std::format("{}Call\n", ind);
        print_expr(*call->callee, out, indent + 1);
        for (std::size_t index = 0; index < call->arguments.size(); ++index) {
            if (index < call->argument_names.size() && call->argument_names[index]) {
                out += std::format("{}  NamedArgument({})\n", ind, *call->argument_names[index]);
            }
            print_expr(*call->arguments[index], out, indent + 2);
        }
    } else if (auto mem = dynamic_cast<const MemberAccessExpr*>(&expr)) {
        out += std::format("{}MemberAccess({}{})\n", ind, mem->is_optional ? "?." : ".", mem->member_name);
        print_expr(*mem->object, out, indent + 1);
    }
}

} // namespace EngineScript
