#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace EngineScript {

struct GenericInstantiation {
    std::string template_name;
    std::vector<std::string> type_args;
    std::string mangled_name;
};

class GenericsManager {
public:
    GenericsManager() = default;

    std::string request_instantiation(const std::string& template_name, const std::vector<std::string>& type_args);
    size_t instantiation_count() const { return m_instantiations.size(); }
    const std::vector<GenericInstantiation>& all_instantiations() const { return m_instantiations; }

    void clear() {
        m_instantiations.clear();
        m_instantiation_map.clear();
    }

private:
    std::string mangle_name(const std::string& template_name, const std::vector<std::string>& type_args) const;

    std::vector<GenericInstantiation> m_instantiations;
    std::unordered_map<std::string, std::string> m_instantiation_map;
};

} // namespace EngineScript
