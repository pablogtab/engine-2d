#include "CppEmitter.hpp"
#include "../api/ApiCatalog.hpp"
#include "../module/GameModule.hpp"
#include <format>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <stdexcept>

namespace EngineScript {

namespace {

std::string template_declaration(const std::vector<IRGenericParam>& parameters, std::string_view indentation = {}) {
    if (parameters.empty()) return {};
    std::string result(indentation);
    result += "template <";
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index > 0) result += ", ";
        result += "typename " + parameters[index].name;
    }
    result += ">\n";
    return result;
}

std::string specialized_name(std::string_view name, const std::vector<IRGenericParam>& parameters) {
    std::string result(name);
    if (parameters.empty()) return result;
    result += '<';
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index > 0) result += ", ";
        result += parameters[index].name;
    }
    result += '>';
    return result;
}

} // namespace

CppEmitter::CppEmitter(const SourceManager& source_manager)
    : m_source_manager(source_manager) {
}

std::string CppEmitter::type_to_cpp(const Type* type, bool is_const_ref) const {
    if (!type) return "void";

    std::string s;
    if (const NumericTypeInfo* numeric = numeric_type_info(type->kind)) {
        s = numeric->cpp_name;
    } else switch (type->kind) {
        case TypeKind::Void:       s = "void"; break;
        case TypeKind::Bool:       s = "bool"; break;
        case TypeKind::String:     s = "std::string"; break;
        case TypeKind::Vec2:       s = "glm::vec2"; break;
        case TypeKind::Vec3:       s = "glm::vec3"; break;
        case TypeKind::Vec4:       s = "glm::vec4"; break;
        case TypeKind::Color:      s = "glm::vec4"; break;
        case TypeKind::Duration:   s = "float"; break;
        case TypeKind::Entity:     s = "EngineScript::Entity"; break;
        case TypeKind::Array:
            if (!type->type_args.empty()) {
                s = std::format("std::vector<{}>", type_to_cpp(type->type_args[0].get()));
            } else {
                s = "std::vector<void*>";
            }
            break;
        case TypeKind::FixedArray:
            if (!type->type_args.empty() && type->fixed_array_size) {
                s = std::format("std::array<{}, {}>", type_to_cpp(type->type_args[0].get()), *type->fixed_array_size);
            } else {
                s = "std::array<int, 0>";
            }
            break;
        case TypeKind::Map:
            if (type->type_args.size() == 2) {
                s = std::format("std::map<{}, {}>", type_to_cpp(type->type_args[0].get()), type_to_cpp(type->type_args[1].get()));
            } else {
                s = "std::map<std::string, std::string>";
            }
            break;
        case TypeKind::Set:
            s = !type->type_args.empty()
                ? std::format("std::set<{}>", type_to_cpp(type->type_args[0].get()))
                : "std::set<int>";
            break;
        case TypeKind::Optional:
            s = !type->type_args.empty()
                ? std::format("std::optional<{}>", type_to_cpp(type->type_args[0].get()))
                : "std::nullopt_t";
            break;
        case TypeKind::Weak:
            s = !type->type_args.empty()
                ? std::format("std::weak_ptr<{}>", type->type_args[0]->name)
                : "std::weak_ptr<void>";
            break;
        case TypeKind::TaskScope:
            s = "EngineScript::TaskScope";
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
        case TypeKind::Interface:
            s = type->name;
            if (!type->type_args.empty()) {
                s += '<';
                for (std::size_t index = 0; index < type->type_args.size(); ++index) {
                    if (index > 0) s += ", ";
                    s += type_to_cpp(type->type_args[index].get());
                }
                s += '>';
            }
            s = std::format("std::shared_ptr<{}>", s);
            break;
        case TypeKind::Struct:
        case TypeKind::TypeParameter:
            s = type->name;
            if (!type->type_args.empty()) {
                s += '<';
                for (std::size_t index = 0; index < type->type_args.size(); ++index) {
                    if (index > 0) s += ", ";
                    s += type_to_cpp(type->type_args[index].get());
                }
                s += '>';
            }
            break;
        case TypeKind::Enum:
            s = type->name;
            break;
        default:
            if (ApiCatalog::instance().has_available_type(type->name)) {
                s = "EngineScript::" + type->name;
            } else if (type->kind == TypeKind::Class) {
                std::string authored_name = type->name;
                if (!type->type_args.empty()) {
                    authored_name += '<';
                    for (std::size_t index = 0; index < type->type_args.size(); ++index) {
                        if (index > 0) authored_name += ", ";
                        authored_name += type_to_cpp(type->type_args[index].get());
                    }
                    authored_name += '>';
                }
                s = type->is_behaviour
                    ? std::format("EngineScript::ComponentHandle<{}>", authored_name)
                    : std::format("std::shared_ptr<{}>", authored_name);
            } else {
                s = type->name;
            }
            break;
    }

    const ApiTypeDescriptor* api_type = type->kind == TypeKind::Class
        ? ApiCatalog::instance().find_available_type(type->name) : nullptr;
    const bool is_value_type = api_type && api_type->value_type;
    const bool is_behaviour_handle = type->kind == TypeKind::Class && type->is_behaviour;
    if (type->is_nullable && !is_behaviour_handle) {
        if (type->kind == TypeKind::Class) {
            s = is_value_type ? std::format("std::optional<{}>", s) : s + "*";
        } else {
            s = std::format("std::optional<{}>", s);
        }
    } else if (type->kind == TypeKind::Class && api_type && s != "void" && !is_value_type && !is_behaviour_handle) {
        s = s + "*";
    }

    if (is_const_ref && (type->kind == TypeKind::String || type->kind == TypeKind::Array || type->kind == TypeKind::Map || type->kind == TypeKind::Entity)) {
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
    h_ss << "#include <string_view>\n";
    h_ss << "#include <vector>\n";
    h_ss << "#include <map>\n";
    h_ss << "#include <set>\n";
    h_ss << "#include <array>\n";
    h_ss << "#include <memory>\n";
    h_ss << "#include <functional>\n";
    h_ss << "#include <optional>\n";
    h_ss << "#include <glm/glm.hpp>\n";
    h_ss << "#include \"enginescript/module/GameModule.hpp\"\n\n";

    for (const auto& imp : module.imports) {
        std::filesystem::path imp_p(imp);
        h_ss << std::format("#include \"{}.generated.hpp\"\n", imp_p.stem().string());
    }
    for (const auto& [alias, original] : module.import_aliases) {
        h_ss << std::format("using {} = {};\n", alias, original);
    }
    h_ss << "\n";

    for (const auto& enum_decl : module.enums) {
        emit_enum_header(enum_decl, out.header_content);
        h_ss << out.header_content;
        out.header_content.clear();
    }

    for (const auto& interface_decl : module.interfaces) {
        emit_interface_header(interface_decl, out.header_content);
        h_ss << out.header_content;
        out.header_content.clear();
    }

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

    // Template definitions must be visible to importing translation units.
    // Keeping them only in the generated .cpp makes cross-module generic calls
    // compile but fail to instantiate or link.
    for (const auto& st : module.structs) {
        if (st.generic_params.empty()) continue;
        emit_struct_source(st, out.header_content);
        h_ss << out.header_content;
        out.header_content.clear();
    }
    for (const auto& cls : module.classes) {
        if (cls.generic_params.empty()) continue;
        emit_class_source(cls, out.header_filename, out.header_content);
        h_ss << out.header_content;
        out.header_content.clear();
    }
    for (const auto& fn : module.functions) {
        if (fn.generic_params.empty()) continue;
        emit_function_source(fn, out.header_content);
        h_ss << out.header_content;
        out.header_content.clear();
    }

    out.header_content = h_ss.str();

    // Source
    std::ostringstream s_ss;
    s_ss << std::format("#include \"{}\"\n\n", out.header_filename);

    for (const auto& st : module.structs) {
        if (!st.generic_params.empty()) continue;
        emit_struct_source(st, out.source_content);
        s_ss << out.source_content;
        out.source_content.clear();
    }

    for (const auto& cls : module.classes) {
        if (!cls.generic_params.empty()) continue;
        emit_class_source(cls, out.header_filename, out.source_content);
        s_ss << out.source_content;
        out.source_content.clear();
    }

    for (const auto& fn : module.functions) {
        if (!fn.generic_params.empty()) continue;
        emit_function_source(fn, out.source_content);
        s_ss << out.source_content;
        out.source_content.clear();
    }

    out.source_content = s_ss.str();
    return out;
}

std::string CppEmitter::emit_game_module_entrypoint(
    const std::vector<IRModule>& modules,
    const std::string& module_name,
    uint32_t build_number
) const {
    std::unordered_set<std::string> behaviour_names;
    std::ostringstream output;
    output << "#include \"enginescript/module/GameModule.hpp\"\n\n";
    for (const IRModule& module : modules) {
        for (const IRClass& behaviour : module.classes) {
            if (!behaviour.is_behaviour) continue;
            if (!behaviour_names.emplace(behaviour.name).second) {
                throw std::runtime_error("EngineScript native module requires unique behaviour class names");
            }
            output << std::format(
                "extern \"C\" EngineScript::NativeBehaviour* create_{}(void** args);\n",
                behaviour.name
            );
        }
    }
    output << "\nextern \"C\" const EngineScript::NativeModuleExports* "
           << "engine_script_module_exports() {\n";
    if (behaviour_names.empty()) {
        output << std::format(
            "    static const EngineScript::NativeModuleExports exports{{{}, EngineScript::ENGINE_SCRIPT_SDK_HASH, \"{}\", {}, nullptr, 0}};\n",
            ENGINE_SCRIPT_ABI_VERSION,
            module_name,
            build_number
        );
        output << "    return &exports;\n}\n";
        return output.str();
    }
    for (const IRModule& module : modules) {
        for (const IRClass& behaviour : module.classes) {
            if (!behaviour.is_behaviour || behaviour.constructor_params.empty()) continue;
            output << std::format(
                "    static const EngineScript::NativeDependencyExport dependencies_{}[] = {{\n",
                behaviour.name
            );
            for (const IRParam& parameter : behaviour.constructor_params) {
                const bool is_component = std::find(
                    parameter.decorators.begin(), parameter.decorators.end(), "component"
                ) != parameter.decorators.end();
                const bool is_optional = std::find(
                    parameter.decorators.begin(), parameter.decorators.end(), "optional"
                ) != parameter.decorators.end();
                const NativeDependencyKind kind = is_component
                    ? NativeDependencyKind::RequiredComponent
                    : (is_optional ? NativeDependencyKind::OptionalComponent : NativeDependencyKind::Service);
                const char* kind_name = kind == NativeDependencyKind::Service
                    ? "Service"
                    : (kind == NativeDependencyKind::RequiredComponent ? "RequiredComponent" : "OptionalComponent");
                output << std::format(
                    "        {{\"{}\", EngineScript::NativeDependencyKind::{}}},\n",
                    parameter.type ? parameter.type->name : "",
                    kind_name
                );
            }
            output << "    };\n";
        }
    }
    output << "    static const EngineScript::NativeBehaviourExport behaviours[] = {\n";
    for (const IRModule& module : modules) {
        for (const IRClass& behaviour : module.classes) {
            if (behaviour.is_behaviour) {
                if (behaviour.constructor_params.empty()) {
                    output << std::format("        {{\"{}\", create_{}, nullptr, 0}},\n", behaviour.name, behaviour.name);
                } else {
                    output << std::format(
                        "        {{\"{}\", create_{}, dependencies_{}, sizeof(dependencies_{}) / sizeof(dependencies_{}[0])}},\n",
                        behaviour.name,
                        behaviour.name,
                        behaviour.name,
                        behaviour.name,
                        behaviour.name
                    );
                }
            }
        }
    }
    output << "    };\n";
    output << std::format(
        "    static const EngineScript::NativeModuleExports exports{{{}, EngineScript::ENGINE_SCRIPT_SDK_HASH, \"{}\", {}, behaviours, sizeof(behaviours) / sizeof(behaviours[0])}};\n",
        ENGINE_SCRIPT_ABI_VERSION,
        module_name,
        build_number
    );
    output << "    return &exports;\n}\n";
    return output.str();
}

void CppEmitter::emit_struct_header(const IRStruct& st, std::string& out) {
    out += template_declaration(st.generic_params);
    out += std::format("struct {}", st.name);
    for (std::size_t index = 0; index < st.interfaces.size(); ++index) {
        out += index == 0 ? " : public " : ", public ";
        out += st.interfaces[index];
    }
    out += " {\n";
    for (const auto& prop : st.properties) {
        out += std::format("    {} {} = {{}};\n", type_to_cpp(prop.type.get()), prop.name);
        out += std::format("    {} get_{}() const {{ return {}; }}\n", type_to_cpp(prop.type.get()), prop.name, prop.name);
    }
    for (const auto& method : st.methods) {
        out += template_declaration(method.generic_params, "    ");
        out += std::format("    {} {}(", type_to_cpp(method.return_type.get()), method.name);
        for (std::size_t index = 0; index < method.params.size(); ++index) {
            out += std::format("{} {}", type_to_cpp(method.params[index].type.get(), true), method.params[index].name);
            if (index + 1 < method.params.size()) out += ", ";
        }
        out += ");\n";
    }
    out += "};\n\n";
}

void CppEmitter::emit_interface_header(const IRInterface& interface_decl, std::string& out) {
    out += template_declaration(interface_decl.generic_params);
    out += std::format("class {}", interface_decl.name);
    for (std::size_t index = 0; index < interface_decl.extended_interfaces.size(); ++index) {
        out += index == 0 ? " : public " : ", public ";
        out += interface_decl.extended_interfaces[index];
    }
    out += " {\npublic:\n";
    out += std::format("    virtual ~{}() = default;\n", interface_decl.name);
    for (const auto& member : interface_decl.members) {
        if (member.is_method) {
            out += template_declaration(member.generic_params, "    ");
            out += std::format("    virtual {} {}(", type_to_cpp(member.type.get()), member.name);
            for (std::size_t index = 0; index < member.params.size(); ++index) {
                out += std::format("{} {}", type_to_cpp(member.params[index].type.get(), true), member.params[index].name);
                if (index + 1 < member.params.size()) out += ", ";
            }
            out += ") = 0;\n";
        } else {
            out += std::format("    virtual {} get_{}() const = 0;\n", type_to_cpp(member.type.get()), member.name);
        }
    }
    out += "};\n\n";
}

void CppEmitter::emit_enum_header(const IREnum& enum_decl, std::string& out) {
    if (!enum_decl.string_backed) {
        out += std::format("enum class {} : int32_t {{\n", enum_decl.name);
        std::int64_t next_value = 0;
        for (std::size_t index = 0; index < enum_decl.entries.size(); ++index) {
            const auto& entry = enum_decl.entries[index];
            if (entry.int_value) next_value = *entry.int_value;
            out += std::format("    {} = {}{}\n", entry.name, next_value,
                index + 1 < enum_decl.entries.size() ? "," : "");
            ++next_value;
        }
        out += "};\n\n";
        return;
    }
    out += std::format("struct {} {{\n", enum_decl.name);
    out += "    std::string_view value;\n";
    out += "    constexpr bool operator==(const " + enum_decl.name + "&) const = default;\n";
    for (const auto& entry : enum_decl.entries) out += std::format("    static const {} {};\n", enum_decl.name, entry.name);
    out += "};\n";
    for (const auto& entry : enum_decl.entries) {
        out += std::format("inline constexpr {} {}::{}{{\"{}\"}};\n", enum_decl.name, enum_decl.name,
            entry.name, entry.string_value.value_or(entry.name));
    }
    out += "\n";
}

void CppEmitter::emit_class_header(const IRClass& cls, std::string& out) {
    std::vector<std::string> bases;
    if (cls.is_behaviour || cls.base_class == "Behaviour") bases.push_back("EngineScript::NativeBehaviour");
    else if (!cls.base_class.empty()) bases.push_back(cls.base_class);
    bases.insert(bases.end(), cls.interfaces.begin(), cls.interfaces.end());
    out += template_declaration(cls.generic_params);
    out += std::format("class {}", cls.name);
    for (std::size_t index = 0; index < bases.size(); ++index) {
        out += index == 0 ? " : public " : ", public ";
        out += bases[index];
    }
    out += " {\n";
    out += "public:\n";
    out += std::format("    static constexpr const char* ENGINE_SCRIPT_TYPE_NAME = \"{}\";\n", cls.name);

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
        if (!prop.is_public) continue;
        out += std::format("    {} {} = {{}};\n", type_to_cpp(prop.type.get()), prop.name);
        out += std::format("    {} get_{}() const {{ return {}; }}\n", type_to_cpp(prop.type.get()), prop.name, prop.name);
    }

    // Methods
    for (const auto& m : cls.methods) {
        if (!m.is_public) continue;
        out += template_declaration(m.generic_params, "    ");
        std::string ret = type_to_cpp(m.return_type.get());
        const bool overrides_lifecycle = cls.is_behaviour &&
            (m.name == "on_start" || m.name == "update" || m.name == "on_destroy" ||
             m.name == "on_collision_enter");
        std::string virt = (m.is_override || overrides_lifecycle) ? " override" : "";
        out += std::format("    {} {}(", ret, m.name);
        for (size_t i = 0; i < m.params.size(); ++i) {
            const auto& p = m.params[i];
            out += std::format("{} {}", type_to_cpp(p.type.get(), true), p.name);
            if (i + 1 < m.params.size()) out += ", ";
        }
        out += std::format("){};\n", virt);
    }

    if (cls.is_behaviour) {
        out += "    bool set_exposed_property(const std::string& name, const ExposedPropertyValue& value) override;\n";
        out += "    bool get_exposed_property(const std::string& name, ExposedPropertyValue& value) const override;\n";
    }

    out += "private:\n";
    for (const auto& prop : cls.properties) {
        if (prop.is_public) continue;
        out += std::format("    {} {} = {{}};\n", type_to_cpp(prop.type.get()), prop.name);
    }
    for (const auto& m : cls.methods) {
        if (m.is_public) continue;
        out += template_declaration(m.generic_params, "    ");
        std::string ret = type_to_cpp(m.return_type.get());
        const bool overrides_lifecycle = cls.is_behaviour &&
            (m.name == "on_start" || m.name == "update" || m.name == "on_destroy" ||
             m.name == "on_collision_enter");
        std::string virt = (m.is_override || overrides_lifecycle) ? " override" : "";
        out += std::format("    {} {}(", ret, m.name);
        for (size_t i = 0; i < m.params.size(); ++i) {
            const auto& p = m.params[i];
            out += std::format("{} {}", type_to_cpp(p.type.get(), true), p.name);
            if (i + 1 < m.params.size()) out += ", ";
        }
        out += std::format("){};\n", virt);
    }
    for (const auto& p : cls.constructor_params) {
        out += std::format("    {} m_{};\n", type_to_cpp(p.type.get()), p.name);
    }

    out += "};\n\n";
}

void CppEmitter::emit_function_header(const IRFunction& fn, std::string& out) {
    out += template_declaration(fn.generic_params);
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
    static_cast<void>(header_name);
    m_injected_members.clear();
    m_async_task_methods.clear();
    for (const auto& parameter : cls.constructor_params) {
        m_injected_members.insert(parameter.name);
    }
    for (const auto& method : cls.methods) {
        if (method.is_async && method.return_type && method.return_type->kind == TypeKind::Task) {
            m_async_task_methods.insert(method.name);
        }
    }

    // Constructor implementation
    out += template_declaration(cls.generic_params);
    out += std::format("{}::{}(", specialized_name(cls.name, cls.generic_params), cls.name);
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
    for (const auto& block : cls.constructor_blocks) {
        for (const auto& statement : block.statements) emit_operation_statement_cpp(statement.op.get(), out, 1);
    }
    out += "}\n\n";

    if (cls.is_behaviour) {
        emit_exposed_property_bridge(cls, out);
    }

    // Method implementations
    for (const auto& m : cls.methods) {
        emit_method_source(cls, m, out);
    }

    if (!cls.is_behaviour) return;

    // Factory export for entity-owned behaviours only.
    out += std::format("extern \"C\" EngineScript::NativeBehaviour* create_{}(void** args) {{\n", cls.name);
    out += std::format("    return new {}(", cls.name);
    for (std::size_t index = 0; index < cls.constructor_params.size(); ++index) {
        const auto& parameter = cls.constructor_params[index];
        const bool behaviour_component = parameter.type && parameter.type->kind == TypeKind::Class &&
            !ApiCatalog::instance().has_available_type(parameter.type->name);
        if (behaviour_component) {
            out += std::format(
                "EngineScript::ComponentHandle<{}>{{args ? *static_cast<EngineScript::ComponentReference*>(args[{}]) : EngineScript::ComponentReference{{}}}}",
                parameter.type->name,
                index
            );
        } else {
            out += std::format(
                "static_cast<{}>(args ? args[{}] : nullptr)",
                type_to_cpp(parameter.type.get()),
                index
            );
        }
        if (index + 1 < cls.constructor_params.size()) out += ", ";
    }
    out += ");\n";
    out += "}\n\n";
}

void CppEmitter::emit_struct_source(const IRStruct& st, std::string& out) {
    m_injected_members.clear();
    for (const auto& method : st.methods) {
        out += template_declaration(st.generic_params);
        out += template_declaration(method.generic_params);
        out += std::format("{} {}::{}(", type_to_cpp(method.return_type.get()), specialized_name(st.name, st.generic_params), method.name);
        for (std::size_t index = 0; index < method.params.size(); ++index) {
            out += std::format("{} {}", type_to_cpp(method.params[index].type.get(), true), method.params[index].name);
            if (index + 1 < method.params.size()) out += ", ";
        }
        out += ") {\n";
        const bool previous_async = m_emitting_async_method;
        m_emitting_async_method = method.is_async;
        for (const auto& block : method.blocks) {
            for (const auto& statement : block.statements) emit_statement_cpp(statement, out, 1);
        }
        m_emitting_async_method = previous_async;
        out += "}\n\n";
    }
}

void CppEmitter::emit_function_source(const IRFunction& fn, std::string& out) {
    m_injected_members.clear();
    out += template_declaration(fn.generic_params);
    out += std::format("{} {}(", type_to_cpp(fn.return_type.get()), fn.name);
    for (std::size_t index = 0; index < fn.params.size(); ++index) {
        out += std::format("{} {}", type_to_cpp(fn.params[index].type.get(), true), fn.params[index].name);
        if (index + 1 < fn.params.size()) out += ", ";
    }
    out += ") {\n";
    const bool previous_async = m_emitting_async_method;
    m_emitting_async_method = fn.is_async;
    for (const auto& block : fn.blocks) {
        for (const auto& statement : block.statements) emit_statement_cpp(statement, out, 1);
    }
    m_emitting_async_method = previous_async;
    out += "}\n\n";
}

void CppEmitter::emit_exposed_property_bridge(const IRClass& cls, std::string& out) {
    const auto is_exposed = [](const IRProperty& property) {
        return std::find(property.decorators.begin(), property.decorators.end(), "expose") != property.decorators.end();
    };
    const auto is_number = [](const Type* type) {
        return type && (type->is_integer() || type->is_float());
    };

    out += std::format(
        "bool {}::set_exposed_property(const std::string& name, const ExposedPropertyValue& value) {{\n",
        cls.name
    );
    for (const IRProperty& property : cls.properties) {
        if (!is_exposed(property)) continue;
        out += std::format("    if (name == \"{}\") {{\n", property.name);
        if (is_number(property.type.get())) {
            out += std::format(
                "        if (const auto* typed_value = std::get_if<double>(&value)) {{ {} = static_cast<{}>(*typed_value); return true; }}\n",
                property.name,
                type_to_cpp(property.type.get())
            );
        } else if (property.type && property.type->is_bool()) {
            out += std::format(
                "        if (const auto* typed_value = std::get_if<bool>(&value)) {{ {} = *typed_value; return true; }}\n",
                property.name
            );
        } else if (property.type && property.type->is_string()) {
            out += std::format(
                "        if (const auto* typed_value = std::get_if<std::string>(&value)) {{ {} = *typed_value; return true; }}\n",
                property.name
            );
        } else if (property.type && property.type->kind == TypeKind::Vec2) {
            out += std::format(
                "        if (const auto* typed_value = std::get_if<glm::vec2>(&value)) {{ {} = *typed_value; return true; }}\n",
                property.name
            );
        } else if (property.type && property.type->kind == TypeKind::Entity) {
            out += std::format(
                "        if (const auto* typed_value = std::get_if<EngineScript::Entity>(&value)) {{ {} = *typed_value; return true; }}\n",
                property.name
            );
        }
        out += "        return false;\n    }\n";
    }
    out += "    return false;\n}\n\n";

    out += std::format(
        "bool {}::get_exposed_property(const std::string& name, ExposedPropertyValue& value) const {{\n",
        cls.name
    );
    for (const IRProperty& property : cls.properties) {
        if (!is_exposed(property)) continue;
        out += std::format("    if (name == \"{}\") {{\n", property.name);
        if (is_number(property.type.get())) {
            out += std::format("        value = static_cast<double>({}); return true;\n", property.name);
        } else if (property.type && property.type->is_bool()) {
            out += std::format("        value = {}; return true;\n", property.name);
        } else if (property.type && property.type->is_string()) {
            out += std::format("        value = {}; return true;\n", property.name);
        } else if (property.type && property.type->kind == TypeKind::Vec2) {
            out += std::format("        value = {}; return true;\n", property.name);
        } else if (property.type && property.type->kind == TypeKind::Entity) {
            out += std::format("        value = {}; return true;\n", property.name);
        } else {
            out += "        return false;\n";
        }
        out += "    }\n";
    }
    out += "    return false;\n}\n\n";
}

void CppEmitter::emit_method_source(const IRClass& cls, const IRMethod& m, std::string& out) {
    std::string ret = type_to_cpp(m.return_type.get());
    out += template_declaration(cls.generic_params);
    out += template_declaration(m.generic_params);
    out += std::format("{} {}::{}(", ret, specialized_name(cls.name, cls.generic_params), m.name);
    for (size_t i = 0; i < m.params.size(); ++i) {
        const auto& p = m.params[i];
        out += std::format("{} {}", type_to_cpp(p.type.get(), true), p.name);
        if (i + 1 < m.params.size()) out += ", ";
    }
    out += ") {\n";

    const bool previous_async_state = m_emitting_async_method;
    m_emitting_async_method = m.is_async;

    for (const auto& block : m.blocks) {
        for (const auto& st : block.statements) {
            emit_statement_cpp(st, out, 1);
        }
    }

    m_emitting_async_method = previous_async_state;

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

    emit_operation_statement_cpp(st.op.get(), out, indent);
}

void CppEmitter::emit_operation_statement_cpp(const IROperation* op, std::string& out, int indent) {
    if (!op) return;
    const std::string ind(indent * 4, ' ');

    if (auto declaration = dynamic_cast<const IRVarDecl*>(op)) {
        const std::string qualifier = declaration->is_const ? "const " : "";
        out += std::format("{}{}{} {}", ind, qualifier, type_to_cpp(declaration->declared_type.get()), declaration->name);
        if (declaration->initializer) {
            if (declaration->declared_type && declaration->declared_type->kind == TypeKind::Interface &&
                declaration->initializer->result_type && declaration->initializer->result_type->kind == TypeKind::Struct) {
                out += std::format(" = EngineScript::boxInterface<{}, {}>({})",
                    declaration->declared_type->name, declaration->initializer->result_type->name,
                    emit_operation_cpp(declaration->initializer.get()));
            } else {
                out += " = " + emit_operation_cpp(declaration->initializer.get());
            }
        }
        out += ";\n";
    } else if (auto assign = dynamic_cast<const IRAssign*>(op)) {
        out += std::format(
            "{}{} {} {};\n",
            ind,
            identifier_to_cpp(assign->target_name),
            token_type_to_string(assign->assignment_op),
            emit_operation_cpp(assign->value.get())
        );
    } else if (auto m_set = dynamic_cast<const IRMemberSet*>(op)) {
        out += std::format(
            "{}{}{}{} {} {};\n",
            ind,
            emit_operation_cpp(m_set->target.get()),
            member_access_operator(m_set->target.get()),
            m_set->member_name,
            token_type_to_string(m_set->assignment_op),
            emit_operation_cpp(m_set->value.get())
        );
    } else if (auto index_set = dynamic_cast<const IRIndexSet*>(op)) {
        const bool checked_sequence = index_set->target && index_set->target->result_type &&
            (index_set->target->result_type->kind == TypeKind::Array ||
             index_set->target->result_type->kind == TypeKind::FixedArray);
        out += checked_sequence
            ? std::format("{}EngineScript::checkedIndex({}, {}) = {};\n", ind,
                emit_operation_cpp(index_set->target.get()), emit_operation_cpp(index_set->index.get()),
                emit_operation_cpp(index_set->value.get()))
            : std::format("{}{}[{}] = {};\n", ind, emit_operation_cpp(index_set->target.get()),
                emit_operation_cpp(index_set->index.get()), emit_operation_cpp(index_set->value.get()));
    } else if (auto branch = dynamic_cast<const IRIf*>(op)) {
        const std::string condition = emit_operation_cpp(branch->condition.get());
        const bool already_parenthesized = condition.size() >= 2 &&
            condition.front() == '(' && condition.back() == ')';
        out += already_parenthesized
            ? std::format("{}if {} {{\n", ind, condition)
            : std::format("{}if ({}) {{\n", ind, condition);
        for (const auto& nested : branch->then_operations) {
            emit_operation_statement_cpp(nested.get(), out, indent + 1);
        }
        out += ind + "}";
        if (!branch->else_operations.empty()) {
            out += " else {\n";
            for (const auto& nested : branch->else_operations) {
                emit_operation_statement_cpp(nested.get(), out, indent + 1);
            }
            out += ind + "}";
        }
        out += "\n";
    } else if (auto ret = dynamic_cast<const IRReturn*>(op)) {
        if (ret->value) {
            out += std::format("{}{} {};\n", ind, m_emitting_async_method ? "co_return" : "return", emit_operation_cpp(ret->value.get()));
        } else {
            out += std::format("{}{};\n", ind, m_emitting_async_method ? "co_return" : "return");
        }
    } else if (auto loop = dynamic_cast<const IRWhile*>(op)) {
        out += std::format("{}while ({}) {{\n", ind, emit_operation_cpp(loop->condition.get()));
        for (const auto& nested : loop->body) emit_operation_statement_cpp(nested.get(), out, indent + 1);
        out += ind + "}\n";
    } else if (auto loop = dynamic_cast<const IRFor*>(op)) {
        out += ind + "for (";
        if (const auto* declaration = dynamic_cast<const IRVarDecl*>(loop->initializer.get())) {
            out += std::format("{} {}", type_to_cpp(declaration->declared_type.get()), declaration->name);
            if (declaration->initializer) out += " = " + emit_operation_cpp(declaration->initializer.get());
        } else if (loop->initializer) {
            out += emit_operation_cpp(loop->initializer.get());
        }
        out += "; ";
        if (loop->condition) out += emit_operation_cpp(loop->condition.get());
        out += "; ";
        if (loop->update) out += emit_operation_cpp(loop->update.get());
        out += ") {\n";
        for (const auto& nested : loop->body) emit_operation_statement_cpp(nested.get(), out, indent + 1);
        out += ind + "}\n";
    } else if (auto loop = dynamic_cast<const IRForIn*>(op)) {
        out += std::format("{}for (const auto& {} : {}) {{\n", ind, loop->variable_name,
            emit_operation_cpp(loop->collection.get()));
        for (const auto& nested : loop->body) emit_operation_statement_cpp(nested.get(), out, indent + 1);
        out += ind + "}\n";
    } else if (dynamic_cast<const IRBreak*>(op)) {
        out += ind + "break;\n";
    } else if (dynamic_cast<const IRContinue*>(op)) {
        out += ind + "continue;\n";
    } else if (auto opt = dynamic_cast<const IROptionalChain*>(op)) {
        out += std::format("{}if ({}) {{ {}->{}(", ind, emit_operation_cpp(opt->target.get()), emit_operation_cpp(opt->target.get()), opt->member_name);
        for (size_t i = 0; i < opt->method_args.size(); ++i) {
            out += emit_operation_cpp(opt->method_args[i].get());
            if (i + 1 < opt->method_args.size()) out += ", ";
        }
        out += "); }\n";
    } else {
        const bool launches_task = op->result_type && op->result_type->kind == TypeKind::Task;
        const auto* call = dynamic_cast<const IRCall*>(op);
        const bool calls_async_task_method = call && m_async_task_methods.contains(call->callee_name);
        if (launches_task || calls_async_task_method) {
            out += std::format("{}this->launch({});\n", ind, emit_operation_cpp(op));
        } else {
            out += std::format("{}{};\n", ind, emit_operation_cpp(op));
        }
    }
}

std::string CppEmitter::emit_operation_cpp(const IROperation* op) {
    if (!op) return "";

    if (auto c = dynamic_cast<const IRConstant*>(op)) {
        if (c->is_null) return "nullptr";
        if (c->result_type && c->result_type->is_bool()) return c->bool_val ? "true" : "false";
        if (c->result_type && c->result_type->is_string()) return std::format("\"{}\"", c->string_val);
        if (c->result_type && c->result_type->is_float()) {
            std::ostringstream number;
            number << std::setprecision(9) << c->float_val;
            std::string literal = number.str();
            if (literal.find_first_of(".eE") == std::string::npos) literal += ".0";
            return literal + "f";
        }
        if (c->result_type && c->result_type->is_integer()) return std::to_string(c->int_val);
        return "0";
    }

    if (auto v = dynamic_cast<const IRVarRef*>(op)) {
        return identifier_to_cpp(v->name);
    }

    if (dynamic_cast<const IRThisRef*>(op)) return "this";

    if (auto bin = dynamic_cast<const IRBinary*>(op)) {
        if (bin->op == TokenType::QuestionQuestion) {
            const std::string left = emit_operation_cpp(bin->left.get());
            const std::string right = emit_operation_cpp(bin->right.get());
            if (bin->left && bin->left->result_type &&
                (bin->left->result_type->kind == TypeKind::Class || bin->left->result_type->kind == TypeKind::Interface)) {
                return std::format("({0} ? {0} : {1})", left, right);
            }
            return std::format("({}).value_or({})", left, right);
        }
        return std::format("({} {} {})", emit_operation_cpp(bin->left.get()), token_type_to_string(bin->op), emit_operation_cpp(bin->right.get()));
    }

    if (auto un = dynamic_cast<const IRUnary*>(op)) {
        return std::format("({}{})", token_type_to_string(un->op), emit_operation_cpp(un->operand.get()));
    }

    if (auto conversion = dynamic_cast<const IRNumericConvert*>(op)) {
        if (!conversion->operand) return "0";
        if (conversion->operand->result_type &&
            conversion->operand->result_type->kind == conversion->target_kind) {
            return emit_operation_cpp(conversion->operand.get());
        }
        const NumericTypeInfo* target = numeric_type_info(conversion->target_kind);
        return target
            ? std::format("EngineScript::numericConvert<{}>({})", target->cpp_name,
                emit_operation_cpp(conversion->operand.get()))
            : "0";
    }

    if (auto conversion = dynamic_cast<const IRStringConvert*>(op)) {
        return conversion->operand
            ? std::format("EngineScript::StringOps::from_numeric({})",
                emit_operation_cpp(conversion->operand.get()))
            : "std::string{}";
    }

    if (auto assignment = dynamic_cast<const IRAssign*>(op)) {
        return std::format("{} {} {}", identifier_to_cpp(assignment->target_name),
            token_type_to_string(assignment->assignment_op), emit_operation_cpp(assignment->value.get()));
    }

    if (auto member_set = dynamic_cast<const IRMemberSet*>(op)) {
        return std::format("{}{}{} {} {}", emit_operation_cpp(member_set->target.get()),
            member_access_operator(member_set->target.get()), member_set->member_name,
            token_type_to_string(member_set->assignment_op), emit_operation_cpp(member_set->value.get()));
    }

    if (auto index_set = dynamic_cast<const IRIndexSet*>(op)) {
        const bool checked_sequence = index_set->target && index_set->target->result_type &&
            (index_set->target->result_type->kind == TypeKind::Array ||
             index_set->target->result_type->kind == TypeKind::FixedArray);
        return checked_sequence
            ? std::format("EngineScript::checkedIndex({}, {}) = {}", emit_operation_cpp(index_set->target.get()),
                emit_operation_cpp(index_set->index.get()), emit_operation_cpp(index_set->value.get()))
            : std::format("{}[{}] = {}", emit_operation_cpp(index_set->target.get()),
                emit_operation_cpp(index_set->index.get()), emit_operation_cpp(index_set->value.get()));
    }

    if (auto construction = dynamic_cast<const IRValueConstruct*>(op)) {
        std::string result = construction->target_kind == TypeKind::Vec2 ? "glm::vec2(" :
            (construction->target_kind == TypeKind::Vec3 ? "glm::vec3(" : "glm::vec4(");
        for (std::size_t index = 0; index < construction->args.size(); ++index) {
            result += emit_operation_cpp(construction->args[index].get());
            if (index + 1 < construction->args.size()) result += ", ";
        }
        return result + ")";
    }

    if (auto construction = dynamic_cast<const IRClassConstruct*>(op)) {
        std::string constructed_name = construction->class_name;
        if (construction->result_type && !construction->result_type->type_args.empty()) {
            constructed_name += '<';
            for (std::size_t index = 0; index < construction->result_type->type_args.size(); ++index) {
                if (index > 0) constructed_name += ", ";
                constructed_name += type_to_cpp(construction->result_type->type_args[index].get());
            }
            constructed_name += '>';
        }
        std::string result = std::format("std::make_shared<{}>(", constructed_name);
        for (std::size_t index = 0; index < construction->args.size(); ++index) {
            result += emit_operation_cpp(construction->args[index].get());
            if (index + 1 < construction->args.size()) result += ", ";
        }
        return result + ")";
    }

    if (auto construction = dynamic_cast<const IRWeakConstruct*>(op)) {
        return type_to_cpp(construction->result_type.get()) + "{" + emit_operation_cpp(construction->value.get()) + "}";
    }

    if (auto literal = dynamic_cast<const IRArrayLiteral*>(op)) {
        std::string result = type_to_cpp(literal->result_type.get()) + "{";
        for (std::size_t index = 0; index < literal->elements.size(); ++index) {
            result += emit_operation_cpp(literal->elements[index].get());
            if (index + 1 < literal->elements.size()) result += ", ";
        }
        return result + "}";
    }

    if (auto literal = dynamic_cast<const IRCollectionLiteral*>(op)) {
        std::string result = type_to_cpp(literal->result_type.get()) + "{";
        if (literal->collection_kind == TypeKind::Map) {
            for (std::size_t index = 0; index < literal->map_entries.size(); ++index) {
                result += "{" + emit_operation_cpp(literal->map_entries[index].first.get()) + ", " +
                    emit_operation_cpp(literal->map_entries[index].second.get()) + "}";
                if (index + 1 < literal->map_entries.size()) result += ", ";
            }
        } else {
            for (std::size_t index = 0; index < literal->elements.size(); ++index) {
                result += emit_operation_cpp(literal->elements[index].get());
                if (index + 1 < literal->elements.size()) result += ", ";
            }
        }
        return result + "}";
    }

    if (auto literal = dynamic_cast<const IRStructLiteral*>(op)) {
        if (ApiCatalog::instance().has_available_type(literal->struct_name)) {
            std::string result = "EngineScript::" + literal->struct_name + "{";
            for (std::size_t index = 0; index < literal->fields.size(); ++index) {
                result += "." + literal->fields[index].first + " = " + emit_operation_cpp(literal->fields[index].second.get());
                if (index + 1 < literal->fields.size()) result += ", ";
            }
            return result + "}";
        }
        const std::string struct_type = type_to_cpp(literal->result_type.get());
        std::string result = "([&]() { " + struct_type + " value{}; ";
        for (const auto& [name, value] : literal->fields) {
            result += "value." + name + " = " + emit_operation_cpp(value.get()) + "; ";
        }
        return result + "return value; }())";
    }

    if (auto closure = dynamic_cast<const IRClosure*>(op)) {
        std::string result = "[=](";
        for (std::size_t index = 0; index < closure->params.size(); ++index) {
            result += type_to_cpp(closure->params[index].type.get()) + " " + closure->params[index].name;
            if (index + 1 < closure->params.size()) result += ", ";
        }
        result += ") {";
        if (closure->body_expression) result += " return " + emit_operation_cpp(closure->body_expression.get()) + "; ";
        for (const auto& nested : closure->body_operations) emit_operation_statement_cpp(nested.get(), result, 1);
        return result + "}";
    }

    if (auto call = dynamic_cast<const IRCall*>(op)) {
        const bool injected_callable = m_injected_members.contains(call->callee_name);
        std::string s;
        if (call->callee_name == "delay" || call->callee_name == "realDelay" ||
            call->callee_name == "nextFrame" || call->callee_name == "fixedUpdate" ||
            call->callee_name == "instantiate" || call->callee_name == "findEntities") {
            s = "this->" + identifier_to_cpp(call->callee_name) + "(";
        } else if (injected_callable) {
            s = "(*" + identifier_to_cpp(call->callee_name) + ")(";
        } else {
            s = identifier_to_cpp(call->callee_name);
            if (!call->type_arguments.empty()) {
                s += '<';
                for (std::size_t index = 0; index < call->type_arguments.size(); ++index) {
                    if (index > 0) s += ", ";
                    s += type_to_cpp(call->type_arguments[index].get());
                }
                s += '>';
            }
            s += '(';
        }
        for (size_t i = 0; i < call->args.size(); ++i) {
            s += emit_operation_cpp(call->args[i].get());
            if (i + 1 < call->args.size()) s += ", ";
        }
        s += ")";
        return s;
    }

    if (auto idx = dynamic_cast<const IRIndexGet*>(op)) {
        if (idx->target && idx->target->result_type && idx->target->result_type->kind == TypeKind::Map) {
            return "EngineScript::checkedLookup(" + emit_operation_cpp(idx->target.get()) + ", " +
                emit_operation_cpp(idx->index.get()) + ")";
        }
        return "EngineScript::checkedIndex(" + emit_operation_cpp(idx->target.get()) + ", " +
            emit_operation_cpp(idx->index.get()) + ")";
    }

    if (auto optional = dynamic_cast<const IROptionalChain*>(op)) {
        const std::string target = emit_operation_cpp(optional->target.get());
        std::string access = target + member_access_operator(optional->target.get()) + optional->member_name;
        if (optional->is_method_call) {
            access += '(';
            for (std::size_t index = 0; index < optional->method_args.size(); ++index) {
                if (index > 0) access += ", ";
                access += emit_operation_cpp(optional->method_args[index].get());
            }
            access += ')';
        }
        if (!optional->result_type || optional->result_type->is_void()) return access;
        return std::format("({0} ? {1}{{{2}}} : {1}{{}})", target,
            type_to_cpp(optional->result_type.get()), access);
    }

    if (const auto* event_emit = dynamic_cast<const IRTypedEventEmit*>(op)) {
        return emit_operation_cpp(event_emit->target.get()) + member_access_operator(event_emit->target.get()) +
            "emit(" + emit_operation_cpp(event_emit->event.get()) + ")";
    }
    if (const auto* event_next = dynamic_cast<const IRTypedEventNext*>(op)) {
        return emit_operation_cpp(event_next->target.get()) + member_access_operator(event_next->target.get()) +
            "next<" + event_next->event_type + ">()";
    }
    if (const auto* asset_load = dynamic_cast<const IRAssetLoad*>(op)) {
        const std::string asset_type = ApiCatalog::instance().has_available_type(asset_load->asset_type)
            ? "EngineScript::" + asset_load->asset_type : asset_load->asset_type;
        return emit_operation_cpp(asset_load->target.get()) + member_access_operator(asset_load->target.get()) +
            "load<" + asset_type + ">(" + emit_operation_cpp(asset_load->path.get()) + ")";
    }
    if (const auto* job_run = dynamic_cast<const IRJobRun*>(op)) {
        return emit_operation_cpp(job_run->target.get()) + member_access_operator(job_run->target.get()) +
            "run(" + emit_operation_cpp(job_run->work.get()) + ")";
    }

    if (auto mc = dynamic_cast<const IRMethodCall*>(op)) {
        if (mc->target && mc->target->result_type) {
            const auto kind = mc->target->result_type->kind;
            const std::string target = emit_operation_cpp(mc->target.get());
            if (kind == TypeKind::Array && mc->method_name == "push") {
                return target + ".push_back(" + (mc->args.empty() ? std::string{} : emit_operation_cpp(mc->args[0].get())) + ")";
            }
            if (kind == TypeKind::Array && mc->method_name == "contains") {
                return "EngineScript::containsValue(" + target + ", " +
                    (mc->args.empty() ? std::string{} : emit_operation_cpp(mc->args[0].get())) + ")";
            }
            if (kind == TypeKind::Array && mc->method_name == "remove") {
                return "EngineScript::removeValue(" + target + ", " +
                    (mc->args.empty() ? std::string{} : emit_operation_cpp(mc->args[0].get())) + ")";
            }
            if (kind == TypeKind::Array && mc->method_name == "pop") {
                return "EngineScript::popValue(" + target + ")";
            }
            if ((kind == TypeKind::Array || kind == TypeKind::Set || kind == TypeKind::Map) &&
                mc->method_name == "clear") {
                return target + ".clear()";
            }
            if (kind == TypeKind::Set && mc->method_name == "add") {
                return target + ".insert(" + (mc->args.empty() ? std::string{} : emit_operation_cpp(mc->args[0].get())) + ")";
            }
            if (kind == TypeKind::Set && mc->method_name == "contains") {
                return target + ".contains(" + (mc->args.empty() ? std::string{} : emit_operation_cpp(mc->args[0].get())) + ")";
            }
            if (kind == TypeKind::Set && mc->method_name == "remove") {
                return "(" + target + ".erase(" +
                    (mc->args.empty() ? std::string{} : emit_operation_cpp(mc->args[0].get())) + ") != 0)";
            }
            if (kind == TypeKind::Map && mc->method_name == "containsKey") {
                return target + ".contains(" + (mc->args.empty() ? std::string{} : emit_operation_cpp(mc->args[0].get())) + ")";
            }
            if (kind == TypeKind::Map && mc->method_name == "set" && mc->args.size() == 2) {
                return "([&]() { " + target + "[" + emit_operation_cpp(mc->args[0].get()) + "] = " +
                    emit_operation_cpp(mc->args[1].get()) + "; }())";
            }
            if (kind == TypeKind::Map && mc->method_name == "get") {
                return "EngineScript::checkedLookup(" + target + ", " +
                    (mc->args.empty() ? std::string{} : emit_operation_cpp(mc->args[0].get())) + ")";
            }
            if (kind == TypeKind::Map && mc->method_name == "remove") {
                return "(" + target + ".erase(" +
                    (mc->args.empty() ? std::string{} : emit_operation_cpp(mc->args[0].get())) + ") != 0)";
            }
            if (kind == TypeKind::Map && mc->method_name == "keys") {
                return "EngineScript::mapKeys(" + target + ")";
            }
            if (kind == TypeKind::Map && mc->method_name == "values") {
                return "EngineScript::mapValues(" + target + ")";
            }
            if (kind == TypeKind::Weak && mc->method_name == "lock") return target + ".lock()";
        }
        if (mc->target && mc->target->result_type && mc->target->result_type->kind == TypeKind::String) {
            std::string target_str = emit_operation_cpp(mc->target.get());
            if (mc->method_name == "includes" || mc->method_name == "contains") {
                std::string arg = !mc->args.empty() ? emit_operation_cpp(mc->args[0].get()) : "\"\"";
                return std::format("EngineScript::StringOps::includes({}, {})", target_str, arg);
            }
            if (mc->method_name == "startsWith" || mc->method_name == "starts_with") {
                std::string arg = !mc->args.empty() ? emit_operation_cpp(mc->args[0].get()) : "\"\"";
                return std::format("EngineScript::StringOps::starts_with({}, {})", target_str, arg);
            }
            if (mc->method_name == "endsWith" || mc->method_name == "ends_with") {
                std::string arg = !mc->args.empty() ? emit_operation_cpp(mc->args[0].get()) : "\"\"";
                return std::format("EngineScript::StringOps::ends_with({}, {})", target_str, arg);
            }
            if (mc->method_name == "indexOf" || mc->method_name == "index_of") {
                std::string arg = !mc->args.empty() ? emit_operation_cpp(mc->args[0].get()) : "\"\"";
                return std::format("EngineScript::StringOps::index_of({}, {})", target_str, arg);
            }
            if (mc->method_name == "toLowerCase" || mc->method_name == "to_lower") {
                return std::format("EngineScript::StringOps::to_lower({})", target_str);
            }
            if (mc->method_name == "toUpperCase" || mc->method_name == "to_upper") {
                return std::format("EngineScript::StringOps::to_upper({})", target_str);
            }
            if (mc->method_name == "length" || mc->method_name == "size") {
                return std::format("static_cast<int>({}.size())", target_str);
            }
        }

        if (mc->target && mc->target->result_type && mc->target->result_type->name == "Result" &&
            (mc->method_name == "success" || mc->method_name == "failure") &&
            mc->result_type && mc->result_type->kind == TypeKind::Result) {
            std::string s = type_to_cpp(mc->result_type.get()) + "::" + mc->method_name + "(";
            for (size_t i = 0; i < mc->args.size(); ++i) {
                s += emit_operation_cpp(mc->args[i].get());
                if (i + 1 < mc->args.size()) s += ", ";
            }
            return s + ")";
        }

        std::string s = emit_operation_cpp(mc->target.get()) + member_access_operator(mc->target.get()) + mc->method_name;
        if (!mc->type_arguments.empty()) {
            s += "<";
            for (size_t i = 0; i < mc->type_arguments.size(); ++i) {
                if (i != 0) s += ", ";
                s += ApiCatalog::instance().has_available_type(mc->type_arguments[i])
                    ? "EngineScript::" + mc->type_arguments[i]
                    : mc->type_arguments[i];
            }
            s += ">";
        }
        s += "(";
        for (size_t i = 0; i < mc->args.size(); ++i) {
            s += emit_operation_cpp(mc->args[i].get());
            if (i + 1 < mc->args.size()) s += ", ";
        }
        s += ")";
        return s;
    }

    if (auto mg = dynamic_cast<const IRMemberGet*>(op)) {
        if (mg->target && mg->target->result_type &&
            (mg->target->result_type->kind == TypeKind::Array ||
             mg->target->result_type->kind == TypeKind::FixedArray ||
             mg->target->result_type->kind == TypeKind::Map ||
             mg->target->result_type->kind == TypeKind::Set) &&
            (mg->member_name == "length" || mg->member_name == "size")) {
            return "static_cast<int32_t>(" + emit_operation_cpp(mg->target.get()) + ".size())";
        }
        if (mg->target && mg->target->result_type && mg->target->result_type->kind == TypeKind::String) {
            if (mg->member_name == "length" || mg->member_name == "size") {
                return std::format("static_cast<int>({}.size())", emit_operation_cpp(mg->target.get()));
            }
        }
        if (mg->target && mg->target->result_type && mg->target->result_type->kind == TypeKind::Enum) {
            return emit_operation_cpp(mg->target.get()) + "::" + mg->member_name;
        }
        if (mg->target && mg->target->result_type && mg->target->result_type->kind == TypeKind::Interface) {
            return emit_operation_cpp(mg->target.get()) + "->get_" + mg->member_name + "()";
        }
        return emit_operation_cpp(mg->target.get()) + member_access_operator(mg->target.get()) + mg->member_name;
    }

    if (auto aw = dynamic_cast<const IRAwait*>(op)) {
        return std::format("co_await {}", emit_operation_cpp(aw->operand.get()));
    }

    return "";
}

std::string CppEmitter::member_access_operator(const IROperation* target) const {
    if (target && target->result_type && target->result_type->is_nullable &&
        target->result_type->kind != TypeKind::Class && target->result_type->kind != TypeKind::Interface) {
        return "->";
    }
    if (target && target->result_type &&
        (target->result_type->kind == TypeKind::Class || target->result_type->kind == TypeKind::Interface)) {
        const ApiTypeDescriptor* type = ApiCatalog::instance().find_available_type(target->result_type->name);
        if (!type || !type->value_type) return "->";
    }
    return ".";
}

std::string CppEmitter::identifier_to_cpp(const std::string& identifier) const {
    return m_injected_members.contains(identifier) ? "m_" + identifier : identifier;
}

} // namespace EngineScript
