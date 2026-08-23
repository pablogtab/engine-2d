#include "CppEmitter.hpp"
#include <format>
#include <sstream>
#include <filesystem>

namespace EngineScript {

CppEmitter::CppEmitter(const SourceManager& source_manager)
    : m_source_manager(source_manager) {
}

std::string CppEmitter::type_to_cpp(const Type* type, bool is_const_ref) const {
    if (!type) return "void";

    std::string s;
    switch (type->kind) {
        case TypeKind::Void:       s = "void"; break;
        case TypeKind::Bool:       s = "bool"; break;
        case TypeKind::Int8:       s = "int8_t"; break;
        case TypeKind::Int16:      s = "int16_t"; break;
        case TypeKind::Int32:      s = "int32_t"; break;
        case TypeKind::Int64:      s = "int64_t"; break;
        case TypeKind::UInt8:      s = "uint8_t"; break;
        case TypeKind::UInt16:     s = "uint16_t"; break;
        case TypeKind::UInt32:     s = "uint32_t"; break;
        case TypeKind::UInt64:     s = "uint64_t"; break;
        case TypeKind::Float32:    s = "float"; break;
        case TypeKind::Float64:    s = "double"; break;
        case TypeKind::String:     s = "std::string"; break;
        case TypeKind::Vec2:       s = "glm::vec2"; break;
        case TypeKind::Vec3:       s = "glm::vec3"; break;
        case TypeKind::Vec4:       s = "glm::vec4"; break;
        case TypeKind::Color:      s = "glm::vec4"; break;
        case TypeKind::Duration:   s = "float"; break;
        case TypeKind::Entity:     s = "uint32_t"; break;
        case TypeKind::Array:
            if (!type->type_args.empty()) {
                s = std::format("std::vector<{}>", type_to_cpp(type->type_args[0].get()));
            } else {
                s = "std::vector<void*>";
            }
            break;
        case TypeKind::Map:
            if (type->type_args.size() == 2) {
                s = std::format("std::map<{}, {}>", type_to_cpp(type->type_args[0].get()), type_to_cpp(type->type_args[1].get()));
            } else {
                s = "std::map<std::string, std::string>";
            }
            break;
        case TypeKind::Task:
            if (!type->type_args.empty()) {
                s = std::format("EngineScript::Task<{}>", type_to_cpp(type->type_args[0].get()));
            } else {
                s = "EngineScript::Task<void>";
            }
            break;
        case TypeKind::Result:
            if (type->type_args.size() == 2) {
                s = std::format("EngineScript::Result<{}, {}>", type_to_cpp(type->type_args[0].get()), type_to_cpp(type->type_args[1].get()));
            } else {
                s = "EngineScript::Result<void, std::string>";
            }
            break;
        default:
            s = type->name;
            break;
    }

    if (type->is_nullable) {
        if (type->kind == TypeKind::Class) {
            s = s + "*";
        } else {
            s = std::format("std::optional<{}>", s);
        }
    } else if (type->kind == TypeKind::Class && s != "void") {
        s = s + "*";
    }

    if (is_const_ref && (type->kind == TypeKind::String || type->kind == TypeKind::Array || type->kind == TypeKind::Map)) {
        return std::format("const {}&", s);
    }

    return s;
}

CppOutput CppEmitter::emit_module(const IRModule& module) {
    std::filesystem::path p(module.module_path);
    std::string stem = p.stem().string();
    if (stem.empty()) stem = "Module";

    CppOutput out;
    out.header_filename = stem + ".generated.hpp";
    out.source_filename = stem + ".generated.cpp";

    // Header
    std::ostringstream h_ss;
    h_ss << "#pragma once\n\n";
    h_ss << "#include <cstdint>\n";
    h_ss << "#include <string>\n";
    h_ss << "#include <vector>\n";
    h_ss << "#include <map>\n";
    h_ss << "#include <optional>\n";
    h_ss << "#include <glm/glm.hpp>\n";
    h_ss << "#include \"enginescript/async/Task.hpp\"\n";
    h_ss << "#include \"enginescript/module/GameModule.hpp\"\n\n";

    for (const auto& imp : module.imports) {
        std::filesystem::path imp_p(imp);
        h_ss << std::format("#include \"{}.generated.hpp\"\n", imp_p.stem().string());
    }
    h_ss << "\n";

    for (const auto& st : module.structs) {
        emit_struct_header(st, out.header_content);
        h_ss << out.header_content;
        out.header_content.clear();
    }

    for (const auto& cls : module.classes) {
        emit_class_header(cls, out.header_content);
        h_ss << out.header_content;
        out.header_content.clear();
    }

    for (const auto& fn : module.functions) {
        emit_function_header(fn, out.header_content);
        h_ss << out.header_content;
        out.header_content.clear();
    }

    out.header_content = h_ss.str();

    // Source
    std::ostringstream s_ss;
    s_ss << std::format("#include \"{}\"\n\n", out.header_filename);

    for (const auto& cls : module.classes) {
        emit_class_source(cls, out.header_filename, out.source_content);
        s_ss << out.source_content;
        out.source_content.clear();
    }

    out.source_content = s_ss.str();
    return out;
}

void CppEmitter::emit_struct_header(const IRStruct& st, std::string& out) {
    out += std::format("struct {} {{\n", st.name);
    for (const auto& prop : st.properties) {
        out += std::format("    {} {} = {{}};\n", type_to_cpp(prop.type.get()), prop.name);
    }
    out += "};\n\n";
}

void CppEmitter::emit_class_header(const IRClass& cls, std::string& out) {
    std::string base = cls.base_class.empty() ? "public EngineScript::NativeBehaviour" : ("public " + cls.base_class);
    out += std::format("class {} : {} {{\n", cls.name, base);
    out += "public:\n";

    // Constructor with injection parameters
    out += std::format("    {}(", cls.name);
    for (size_t i = 0; i < cls.constructor_params.size(); ++i) {
        const auto& p = cls.constructor_params[i];
        out += std::format("{} {}", type_to_cpp(p.type.get(), true), p.name);
        if (i + 1 < cls.constructor_params.size()) out += ", ";
    }
    out += ");\n";

    // Properties
    for (const auto& prop : cls.properties) {
        out += std::format("    {} {} = {{}};\n", type_to_cpp(prop.type.get()), prop.name);
    }

    // Methods
    for (const auto& m : cls.methods) {
        std::string ret = type_to_cpp(m.return_type.get());
        std::string virt = m.is_override ? " override" : "";
        out += std::format("    {} {}(", ret, m.name);
        for (size_t i = 0; i < m.params.size(); ++i) {
            const auto& p = m.params[i];
            out += std::format("{} {}", type_to_cpp(p.type.get(), true), p.name);
            if (i + 1 < m.params.size()) out += ", ";
        }
        out += std::format("){};\n", virt);
    }

    out += "private:\n";
    for (const auto& p : cls.constructor_params) {
        out += std::format("    {} m_{};\n", type_to_cpp(p.type.get()), p.name);
    }

    out += "};\n\n";
}

void CppEmitter::emit_function_header(const IRFunction& fn, std::string& out) {
    std::string ret = type_to_cpp(fn.return_type.get());
    out += std::format("{} {}(", ret, fn.name);
    for (size_t i = 0; i < fn.params.size(); ++i) {
        const auto& p = fn.params[i];
        out += std::format("{} {}", type_to_cpp(p.type.get(), true), p.name);
        if (i + 1 < fn.params.size()) out += ", ";
    }
    out += ");\n\n";
}

void CppEmitter::emit_class_source(const IRClass& cls, const std::string& header_name, std::string& out) {
    // Constructor implementation
    out += std::format("{}::{}(", cls.name, cls.name);
    for (size_t i = 0; i < cls.constructor_params.size(); ++i) {
        const auto& p = cls.constructor_params[i];
        out += std::format("{} {}", type_to_cpp(p.type.get(), true), p.name);
        if (i + 1 < cls.constructor_params.size()) out += ", ";
    }
    out += ")\n";

    if (!cls.constructor_params.empty()) {
        out += "    : ";
        for (size_t i = 0; i < cls.constructor_params.size(); ++i) {
            const auto& p = cls.constructor_params[i];
            out += std::format("m_{}({})", p.name, p.name);
            if (i + 1 < cls.constructor_params.size()) out += ", ";
        }
        out += "\n";
    }

    out += "{\n";
    // Initialize properties with defaults
    for (const auto& prop : cls.properties) {
        if (prop.initializer) {
            out += std::format("    this->{} = {};\n", prop.name, emit_operation_cpp(prop.initializer.get()));
        }
    }
    out += "}\n\n";

    // Method implementations
    for (const auto& m : cls.methods) {
        emit_method_source(cls, m, out);
    }

    // Factory export
    out += std::format("extern \"C\" void* create_{}(void** args) {{\n", cls.name);
    out += std::format("    return new {}();\n", cls.name);
    out += "}\n\n";
}

void CppEmitter::emit_method_source(const IRClass& cls, const IRMethod& m, std::string& out) {
    std::string ret = type_to_cpp(m.return_type.get());
    out += std::format("{} {}::{}(", ret, cls.name, m.name);
    for (size_t i = 0; i < m.params.size(); ++i) {
        const auto& p = m.params[i];
        out += std::format("{} {}", type_to_cpp(p.type.get(), true), p.name);
        if (i + 1 < m.params.size()) out += ", ";
    }
    out += ") {\n";

    for (const auto& block : m.blocks) {
        for (const auto& st : block.statements) {
            emit_statement_cpp(st, out, 1);
        }
    }

    out += "}\n\n";
}

void CppEmitter::emit_statement_cpp(const IRStatement& st, std::string& out, int indent) {
    std::string ind(indent * 4, ' ');
    if (st.span.is_valid()) {
        const SourceFile* sf = m_source_manager.get_file(st.span.file_id);
        if (sf) {
            out += std::format("#line {} \"{}\"\n", st.span.start_line, sf->path);
        }
    }

    if (!st.op) return;

    if (auto assign = dynamic_cast<const IRAssign*>(st.op.get())) {
        out += std::format("{}{} = {};\n", ind, assign->target_name, emit_operation_cpp(assign->value.get()));
    } else if (auto m_set = dynamic_cast<const IRMemberSet*>(st.op.get())) {
        out += std::format("{}{}.{} = {};\n", ind, emit_operation_cpp(m_set->target.get()), m_set->member_name, emit_operation_cpp(m_set->value.get()));
    } else if (auto ret = dynamic_cast<const IRReturn*>(st.op.get())) {
        if (ret->value) {
            out += std::format("{}return {};\n", ind, emit_operation_cpp(ret->value.get()));
        } else {
            out += std::format("{}return;\n", ind);
        }
    } else if (auto opt = dynamic_cast<const IROptionalChain*>(st.op.get())) {
        out += std::format("{}if ({}) {{ {}->{}(", ind, emit_operation_cpp(opt->target.get()), emit_operation_cpp(opt->target.get()), opt->member_name);
        for (size_t i = 0; i < opt->method_args.size(); ++i) {
            out += emit_operation_cpp(opt->method_args[i].get());
            if (i + 1 < opt->method_args.size()) out += ", ";
        }
        out += "); }\n";
    } else {
        out += std::format("{}{};\n", ind, emit_operation_cpp(st.op.get()));
    }
}

std::string CppEmitter::emit_operation_cpp(const IROperation* op) {
    if (!op) return "";

    if (auto c = dynamic_cast<const IRConstant*>(op)) {
        if (c->is_null) return "nullptr";
        if (c->result_type && c->result_type->is_bool()) return c->bool_val ? "true" : "false";
        if (c->result_type && c->result_type->is_string()) return std::format("\"{}\"", c->string_val);
        if (c->result_type && c->result_type->is_float()) return std::format("{}f", c->float_val);
        if (c->result_type && c->result_type->is_integer()) return std::to_string(c->int_val);
        return "0";
    }

    if (auto v = dynamic_cast<const IRVarRef*>(op)) {
        return v->name;
    }

    if (auto bin = dynamic_cast<const IRBinary*>(op)) {
        return std::format("({} {} {})", emit_operation_cpp(bin->left.get()), token_type_to_string(bin->op), emit_operation_cpp(bin->right.get()));
    }

    if (auto un = dynamic_cast<const IRUnary*>(op)) {
        return std::format("({}{})", token_type_to_string(un->op), emit_operation_cpp(un->operand.get()));
    }

    if (auto call = dynamic_cast<const IRCall*>(op)) {
        std::string s = call->callee_name + "(";
        for (size_t i = 0; i < call->args.size(); ++i) {
            s += emit_operation_cpp(call->args[i].get());
            if (i + 1 < call->args.size()) s += ", ";
        }
        s += ")";
        return s;
    }

    if (auto mc = dynamic_cast<const IRMethodCall*>(op)) {
        std::string s = emit_operation_cpp(mc->target.get()) + "." + mc->method_name + "(";
        for (size_t i = 0; i < mc->args.size(); ++i) {
            s += emit_operation_cpp(mc->args[i].get());
            if (i + 1 < mc->args.size()) s += ", ";
        }
        s += ")";
        return s;
    }

    if (auto mg = dynamic_cast<const IRMemberGet*>(op)) {
        return emit_operation_cpp(mg->target.get()) + "." + mg->member_name;
    }

    if (auto aw = dynamic_cast<const IRAwait*>(op)) {
        return std::format("co_await {}", emit_operation_cpp(aw->operand.get()));
    }

    return "";
}

} // namespace EngineScript
