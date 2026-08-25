#pragma once

#include "../ir/TypedIR.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <unordered_set>

namespace EngineScript {

struct GenericInstantiation {
    std::string template_name;
    std::vector<std::string> type_args;
    std::string mangled_name;
};

enum class GenericInstantiationFailure {
    None,
    Cycle,
    DepthLimit,
    CountLimit
};

struct GenericInstantiationLimits {
    size_t max_instantiations = 256;
    size_t max_active_depth = 32;
};

class GenericsManager {
public:
    explicit GenericsManager(GenericInstantiationLimits limits = {}) : m_limits(limits) {}

    std::string request_instantiation(const std::string& template_name, const std::vector<std::string>& type_args);
    std::optional<std::string> try_request_instantiation(
        const std::string& template_name,
        const std::vector<std::string>& type_args
    );
    bool begin_instantiation(const std::string& template_name, const std::vector<std::string>& type_args);
    void end_instantiation(const std::string& template_name, const std::vector<std::string>& type_args);
    size_t instantiation_count() const { return m_instantiations.size(); }
    const std::vector<GenericInstantiation>& all_instantiations() const { return m_instantiations; }
    GenericInstantiationFailure last_failure() const { return m_last_failure; }
    const std::string& failed_instantiation() const { return m_failed_instantiation; }
    bool has_failed() const { return m_last_failure != GenericInstantiationFailure::None; }
    void analyze_module(const IRModule& module);
    void analyze_modules(const std::vector<IRModule>& modules);

    void clear() {
        m_instantiations.clear();
        m_instantiation_map.clear();
        m_active_instantiations.clear();
        m_expanded_instantiations.clear();
        m_generic_functions.clear();
        m_type_bindings.clear();
        m_last_failure = GenericInstantiationFailure::None;
        m_failed_instantiation.clear();
    }

private:
    std::string mangle_name(const std::string& template_name, const std::vector<std::string>& type_args) const;
    static std::string instantiation_key(const std::string& template_name, const std::vector<std::string>& type_args);
    void record_type(const std::shared_ptr<Type>& type);
    void analyze_operation(const IROperation* operation);
    void analyze_blocks(const std::vector<IRBlock>& blocks);
    void register_module(const IRModule& module);
    void scan_module(const IRModule& module);
    std::optional<std::string> resolve_type_name(const std::shared_ptr<Type>& type) const;
    void expand_function(const std::string& name, const std::vector<std::string>& type_args);

    std::vector<GenericInstantiation> m_instantiations;
    std::unordered_map<std::string, std::string> m_instantiation_map;
    std::unordered_set<std::string> m_active_instantiations;
    std::unordered_set<std::string> m_expanded_instantiations;
    std::unordered_map<std::string, const IRFunction*> m_generic_functions;
    std::unordered_map<std::string, std::string> m_type_bindings;
    GenericInstantiationLimits m_limits;
    GenericInstantiationFailure m_last_failure{GenericInstantiationFailure::None};
    std::string m_failed_instantiation;
};

} // namespace EngineScript
