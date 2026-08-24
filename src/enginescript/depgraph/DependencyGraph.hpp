#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace EngineScript {

enum class EdgeKind {
    Import,
    Inheritance,
    InterfaceImplementation,
    PublicSignature,
    ImplementationBody,
    GenericInstantiation,
    Metadata,
    ABI
};

inline const char* edge_kind_to_string(EdgeKind k) {
    switch (k) {
        case EdgeKind::Import:                  return "Import";
        case EdgeKind::Inheritance:             return "Inheritance";
        case EdgeKind::InterfaceImplementation: return "InterfaceImplementation";
        case EdgeKind::PublicSignature:         return "PublicSignature";
        case EdgeKind::ImplementationBody:      return "ImplementationBody";
        case EdgeKind::GenericInstantiation:    return "GenericInstantiation";
        case EdgeKind::Metadata:                return "Metadata";
        case EdgeKind::ABI:                     return "ABI";
    }
    return "Unknown";
}

struct DependencyEdge {
    std::string from_module; // Dependee
    std::string to_module;   // Dependency
    EdgeKind kind;
};

class DependencyGraph {
public:
    void clear();
    void add_node(const std::string& module_path);
    void add_edge(const std::string& from_module, const std::string& to_module, EdgeKind kind);

    bool has_cycle(std::vector<std::string>* out_cycle = nullptr) const;
    std::vector<std::string> topological_sort() const;

    std::unordered_set<std::string> get_invalidation_closure(const std::string& changed_module, bool is_public_signature_change) const;

    const std::vector<std::string>& nodes() const { return m_nodes; }
    const std::vector<DependencyEdge>& edges() const { return m_edges; }

private:
    std::vector<std::string> m_nodes;
    std::unordered_set<std::string> m_node_set;
    std::vector<DependencyEdge> m_edges;
    std::unordered_map<std::string, std::vector<std::string>> m_adj;       // from -> list of [to] (dependencies)
    std::unordered_map<std::string, std::vector<std::string>> m_rev_adj;   // to -> list of [from] (dependents)
};

} // namespace EngineScript
