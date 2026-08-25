#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <unordered_set>

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
    bool begin_instantiation(const std::string& template_name, const std::vector<std::string>& type_args);
    void end_instantiation(const std::string& template_name, const std::vector<std::string>& type_args);
    size_t instantiation_count() const { return m_instantiations.size(); }
    const std::vector<GenericInstantiation>& all_instantiations() const { return m_instantiations; }

    void clear() {
        m_instantiations.clear();
        m_instantiation_map.clear();
        m_active_instantiations.clear();
    }

private:
    std::string mangle_name(const std::string& template_name, const std::vector<std::string>& type_args) const;
    static std::string instantiation_key(const std::string& template_name, const std::vector<std::string>& type_args);

    std::vector<GenericInstantiation> m_instantiations;
    std::unordered_map<std::string, std::string> m_instantiation_map;
    std::unordered_set<std::string> m_active_instantiations;
};

} // namespace EngineScript
