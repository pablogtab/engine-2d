#include "Generics.hpp"
#include <sstream>

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

std::string GenericsManager::request_instantiation(const std::string& template_name, const std::vector<std::string>& type_args) {
    std::string key = template_name + "<";
    for (size_t i = 0; i < type_args.size(); ++i) {
        key += type_args[i];
        if (i + 1 < type_args.size()) key += ",";
    }
    key += ">";

    auto it = m_instantiation_map.find(key);
    if (it != m_instantiation_map.end()) {
        return it->second;
    }

    std::string mangled = mangle_name(template_name, type_args);
    GenericInstantiation inst;
    inst.template_name = template_name;
    inst.type_args = type_args;
    inst.mangled_name = mangled;

    m_instantiations.push_back(inst);
    m_instantiation_map[key] = mangled;
    return mangled;
}

} // namespace EngineScript
