#include "Generics.hpp"
#include <sstream>
#include <algorithm>

namespace EngineScript {

std::string GenericsManager::mangle_name(const std::string& template_name, const std::vector<std::string>& type_args) const {
    std::ostringstream ss;
    ss << template_name << "_";
    for (size_t i = 0; i < type_args.size(); ++i) {
        std::string clean = type_args[i];
        for (char& c : clean) {
            if (c == '<' || c == '>' || c == ',' || c == ' ' || c == '?') c = '_';
        }
        ss << clean;
        if (i + 1 < type_args.size()) ss << "_";
    }
    return ss.str();
}

std::string GenericsManager::instantiation_key(const std::string& template_name, const std::vector<std::string>& type_args) {
    std::string key = template_name + "<";
    for (size_t i = 0; i < type_args.size(); ++i) {
        key += type_args[i];
        if (i + 1 < type_args.size()) key += ",";
    }
    key += ">";
    return key;
}

bool GenericsManager::begin_instantiation(const std::string& template_name, const std::vector<std::string>& type_args) {
    const std::string key = instantiation_key(template_name, type_args);
    if (m_active_instantiations.contains(key)) {
        m_last_failure = GenericInstantiationFailure::Cycle;
        m_failed_instantiation = key;
        return false;
    }
    if (m_active_instantiations.size() >= m_limits.max_active_depth) {
        m_last_failure = GenericInstantiationFailure::DepthLimit;
        m_failed_instantiation = key;
        return false;
    }
    m_active_instantiations.emplace(key);
    return true;
}

void GenericsManager::end_instantiation(const std::string& template_name, const std::vector<std::string>& type_args) {
    m_active_instantiations.erase(instantiation_key(template_name, type_args));
}

std::string GenericsManager::request_instantiation(const std::string& template_name, const std::vector<std::string>& type_args) {
    return try_request_instantiation(template_name, type_args).value_or(std::string{});
}

std::optional<std::string> GenericsManager::try_request_instantiation(
    const std::string& template_name,
    const std::vector<std::string>& type_args
) {
    const std::string key = instantiation_key(template_name, type_args);

    auto it = m_instantiation_map.find(key);
    if (it != m_instantiation_map.end()) {
        return it->second;
    }
    if (m_instantiations.size() >= m_limits.max_instantiations) {
        m_last_failure = GenericInstantiationFailure::CountLimit;
        m_failed_instantiation = key;
        return std::nullopt;
    }

    std::string mangled = mangle_name(template_name, type_args);
    GenericInstantiation inst;
    inst.template_name = template_name;
    inst.type_args = type_args;
    inst.mangled_name = mangled;

    m_instantiations.push_back(inst);
    std::sort(m_instantiations.begin(), m_instantiations.end(), [](const auto& left, const auto& right) {
        if (left.template_name != right.template_name) return left.template_name < right.template_name;
        return left.type_args < right.type_args;
    });
    m_instantiation_map[key] = mangled;
    return mangled;
}

void GenericsManager::record_type(const std::shared_ptr<Type>& type) {
    if (!type) return;
    for (const auto& argument : type->type_args) record_type(argument);
    if (type->type_args.empty()) return;
    std::vector<std::string> arguments;
    arguments.reserve(type->type_args.size() + (type->fixed_array_size ? 1U : 0U));
    for (const auto& argument : type->type_args) {
        const auto resolved = resolve_type_name(argument);
        if (!resolved) return;
        arguments.push_back(*resolved);
    }
    if (type->fixed_array_size) arguments.push_back(std::to_string(*type->fixed_array_size));
    static_cast<void>(try_request_instantiation(type->name, arguments));
}

std::optional<std::string> GenericsManager::resolve_type_name(const std::shared_ptr<Type>& type) const {
    if (!type) return std::nullopt;
    if (type->kind == TypeKind::TypeParameter) {
        const auto found = m_type_bindings.find(type->name);
        return found == m_type_bindings.end() ? std::nullopt : std::optional<std::string>{found->second};
    }
    if (type->type_args.empty()) return type->to_string();

    std::string result = type->name;
    if (result.empty()) {
        result = type->to_string();
        if (const std::size_t generic = result.find('<'); generic != std::string::npos) result.resize(generic);
    }
    result += '<';
    for (std::size_t index = 0; index < type->type_args.size(); ++index) {
        const auto argument = resolve_type_name(type->type_args[index]);
        if (!argument) return std::nullopt;
        if (index != 0) result += ", ";
        result += *argument;
    }
    if (type->fixed_array_size) {
        if (!type->type_args.empty()) result += ", ";
        result += std::to_string(*type->fixed_array_size);
    }
    result += '>';
    if (type->is_nullable) result += '?';
    return result;
}

void GenericsManager::expand_function(const std::string& name, const std::vector<std::string>& type_args) {
    const auto function = m_generic_functions.find(name);
    if (function == m_generic_functions.end() || function->second->generic_params.size() != type_args.size()) return;
    const std::string key = instantiation_key(name, type_args);
    if (m_expanded_instantiations.contains(key)) return;
    if (!begin_instantiation(name, type_args)) return;

    const auto previous_bindings = m_type_bindings;
    for (std::size_t index = 0; index < type_args.size(); ++index) {
        m_type_bindings[function->second->generic_params[index].name] = type_args[index];
    }
    analyze_blocks(function->second->blocks);
    m_type_bindings = previous_bindings;
    end_instantiation(name, type_args);
    if (!has_failed()) m_expanded_instantiations.emplace(key);
}

void GenericsManager::analyze_blocks(const std::vector<IRBlock>& blocks) {
    for (const IRBlock& block : blocks) {
        for (const IRStatement& statement : block.statements) analyze_operation(statement.op.get());
    }
}

void GenericsManager::analyze_operation(const IROperation* operation) {
    if (!operation) return;
    record_type(operation->result_type);
    const auto analyze_all = [this](const auto& operations) {
        for (const auto& child : operations) analyze_operation(child.get());
    };

    if (const auto* value = dynamic_cast<const IRVarDecl*>(operation)) {
        record_type(value->declared_type);
        analyze_operation(value->initializer.get());
    } else if (const auto* value = dynamic_cast<const IRAssign*>(operation)) {
        analyze_operation(value->value.get());
    } else if (const auto* value = dynamic_cast<const IRBinary*>(operation)) {
        analyze_operation(value->left.get());
        analyze_operation(value->right.get());
    } else if (const auto* value = dynamic_cast<const IRUnary*>(operation)) {
        analyze_operation(value->operand.get());
    } else if (const auto* value = dynamic_cast<const IRNumericConvert*>(operation)) {
        analyze_operation(value->operand.get());
    } else if (const auto* value = dynamic_cast<const IRStringConvert*>(operation)) {
        analyze_operation(value->operand.get());
    } else if (const auto* value = dynamic_cast<const IRValueConstruct*>(operation)) {
        analyze_all(value->args);
    } else if (const auto* value = dynamic_cast<const IRClassConstruct*>(operation)) {
        analyze_all(value->args);
    } else if (const auto* value = dynamic_cast<const IRWeakConstruct*>(operation)) {
        analyze_operation(value->value.get());
    } else if (const auto* value = dynamic_cast<const IRArrayLiteral*>(operation)) {
        analyze_all(value->elements);
    } else if (const auto* value = dynamic_cast<const IRCollectionLiteral*>(operation)) {
        analyze_all(value->elements);
        for (const auto& [key, mapped] : value->map_entries) {
            analyze_operation(key.get());
            analyze_operation(mapped.get());
        }
    } else if (const auto* value = dynamic_cast<const IRStructLiteral*>(operation)) {
        for (const auto& [name, field] : value->fields) {
            static_cast<void>(name);
            analyze_operation(field.get());
        }
    } else if (const auto* value = dynamic_cast<const IRClosure*>(operation)) {
        for (const auto& parameter : value->params) record_type(parameter.type);
        analyze_all(value->body_operations);
        analyze_operation(value->body_expression.get());
    } else if (const auto* value = dynamic_cast<const IRTypedEventEmit*>(operation)) {
        analyze_operation(value->target.get());
        analyze_operation(value->event.get());
    } else if (const auto* value = dynamic_cast<const IRTypedEventNext*>(operation)) {
        analyze_operation(value->target.get());
    } else if (const auto* value = dynamic_cast<const IRAssetLoad*>(operation)) {
        analyze_operation(value->target.get());
        analyze_operation(value->path.get());
    } else if (const auto* value = dynamic_cast<const IRJobRun*>(operation)) {
        analyze_operation(value->target.get());
        analyze_operation(value->work.get());
    } else if (const auto* value = dynamic_cast<const IRCall*>(operation)) {
        for (const auto& argument : value->type_arguments) record_type(argument);
        if (!value->type_arguments.empty()) {
            std::vector<std::string> arguments;
            arguments.reserve(value->type_arguments.size());
            for (const auto& argument : value->type_arguments) {
                const auto resolved = resolve_type_name(argument);
                if (!resolved) {
                    arguments.clear();
                    break;
                }
                arguments.push_back(*resolved);
            }
            if (!arguments.empty() && try_request_instantiation(value->callee_name, arguments)) {
                expand_function(value->callee_name, arguments);
            }
        }
        analyze_all(value->args);
    } else if (const auto* value = dynamic_cast<const IRMethodCall*>(operation)) {
        analyze_operation(value->target.get());
        if (!value->type_arguments.empty()) {
            static_cast<void>(try_request_instantiation(value->method_name, value->type_arguments));
        }
        analyze_all(value->args);
    } else if (const auto* value = dynamic_cast<const IRMemberGet*>(operation)) {
        analyze_operation(value->target.get());
    } else if (const auto* value = dynamic_cast<const IRMemberSet*>(operation)) {
        analyze_operation(value->target.get());
        analyze_operation(value->value.get());
    } else if (const auto* value = dynamic_cast<const IROptionalChain*>(operation)) {
        analyze_operation(value->target.get());
        analyze_all(value->method_args);
    } else if (const auto* value = dynamic_cast<const IRIndexGet*>(operation)) {
        analyze_operation(value->target.get());
        analyze_operation(value->index.get());
    } else if (const auto* value = dynamic_cast<const IRIndexSet*>(operation)) {
        analyze_operation(value->target.get());
        analyze_operation(value->index.get());
        analyze_operation(value->value.get());
    } else if (const auto* value = dynamic_cast<const IRReturn*>(operation)) {
        analyze_operation(value->value.get());
    } else if (const auto* value = dynamic_cast<const IRIf*>(operation)) {
        analyze_operation(value->condition.get());
        analyze_all(value->then_operations);
        analyze_all(value->else_operations);
    } else if (const auto* value = dynamic_cast<const IRWhile*>(operation)) {
        analyze_operation(value->condition.get());
        analyze_all(value->body);
    } else if (const auto* value = dynamic_cast<const IRFor*>(operation)) {
        analyze_operation(value->initializer.get());
        analyze_operation(value->condition.get());
        analyze_operation(value->update.get());
        analyze_all(value->body);
    } else if (const auto* value = dynamic_cast<const IRForIn*>(operation)) {
        analyze_operation(value->collection.get());
        analyze_all(value->body);
    } else if (const auto* value = dynamic_cast<const IRAwait*>(operation)) {
        analyze_operation(value->operand.get());
    }
}

void GenericsManager::register_module(const IRModule& module) {
    for (const IRFunction& value : module.functions) {
        if (!value.generic_params.empty()) m_generic_functions[value.name] = &value;
    }
}

void GenericsManager::scan_module(const IRModule& module) {
    const auto record_parameters = [this](const auto& parameters) {
        for (const auto& parameter : parameters) record_type(parameter.type);
    };
    const auto record_methods = [this, &record_parameters](const auto& methods) {
        for (const auto& method : methods) {
            record_type(method.return_type);
            record_parameters(method.params);
            analyze_blocks(method.blocks);
        }
    };
    for (const IRClass& value : module.classes) {
        record_parameters(value.constructor_params);
        analyze_blocks(value.constructor_blocks);
        for (const IRProperty& property : value.properties) {
            record_type(property.type);
            analyze_operation(property.initializer.get());
        }
        record_methods(value.methods);
    }
    for (const IRStruct& value : module.structs) {
        for (const IRProperty& property : value.properties) {
            record_type(property.type);
            analyze_operation(property.initializer.get());
        }
        record_methods(value.methods);
    }
    for (const IRInterface& value : module.interfaces) {
        for (const IRInterfaceMember& member : value.members) {
            record_type(member.type);
            record_parameters(member.params);
        }
    }
    for (const IRFunction& value : module.functions) {
        record_type(value.return_type);
        record_parameters(value.params);
        analyze_blocks(value.blocks);
    }
}

void GenericsManager::analyze_module(const IRModule& module) {
    register_module(module);
    scan_module(module);
}

void GenericsManager::analyze_modules(const std::vector<IRModule>& modules) {
    m_generic_functions.clear();
    for (const IRModule& module : modules) register_module(module);
    for (const IRModule& module : modules) scan_module(module);
}

} // namespace EngineScript
