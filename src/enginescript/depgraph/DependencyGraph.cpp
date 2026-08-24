#include "DependencyGraph.hpp"
#include <algorithm>
#include <queue>

namespace EngineScript {

void DependencyGraph::clear() {
    m_nodes.clear();
    m_node_set.clear();
    m_edges.clear();
    m_adj.clear();
    m_rev_adj.clear();
}

void DependencyGraph::add_node(const std::string& module_path) {
    if (m_node_set.find(module_path) == m_node_set.end()) {
        m_node_set.insert(module_path);
        m_nodes.push_back(module_path);
    }
}

void DependencyGraph::add_edge(const std::string& from_module, const std::string& to_module, EdgeKind kind) {
    add_node(from_module);
    add_node(to_module);

    m_edges.push_back({from_module, to_module, kind});
    m_adj[from_module].push_back(to_module);
    m_rev_adj[to_module].push_back(from_module);
}

bool DependencyGraph::has_cycle(std::vector<std::string>* out_cycle) const {
    enum class State { Unvisited, Visiting, Visited };
    std::unordered_map<std::string, State> state;
    std::vector<std::string> stack;

    for (const auto& node : m_nodes) {
        state[node] = State::Unvisited;
    }

    auto dfs = [&](auto& self, const std::string& u) -> bool {
        state[u] = State::Visiting;
        stack.push_back(u);

        auto it = m_adj.find(u);
        if (it != m_adj.end()) {
            for (const auto& v : it->second) {
                if (state[v] == State::Visiting) {
                    if (out_cycle) {
                        out_cycle->clear();
                        auto c_it = std::find(stack.begin(), stack.end(), v);
                        if (c_it != stack.end()) {
                            out_cycle->assign(c_it, stack.end());
                            out_cycle->push_back(v);
                        }
                    }
                    return true;
                }
                if (state[v] == State::Unvisited) {
                    if (self(self, v)) return true;
                }
            }
        }

        stack.pop_back();
        state[u] = State::Visited;
        return false;
    };

    for (const auto& node : m_nodes) {
        if (state[node] == State::Unvisited) {
            if (dfs(dfs, node)) return true;
        }
    }
    return false;
}

std::vector<std::string> DependencyGraph::topological_sort() const {
    // Kahn's algorithm
    // In-degree is number of dependencies the node needs first
    std::unordered_map<std::string, int> in_degree;
    for (const auto& node : m_nodes) {
        in_degree[node] = 0;
    }

    for (const auto& [from, deps] : m_adj) {
        in_degree[from] += static_cast<int>(deps.size());
    }

    std::queue<std::string> q;
    for (const auto& node : m_nodes) {
        if (in_degree[node] == 0) {
            q.push(node);
        }
    }

    std::vector<std::string> order;
    while (!q.empty()) {
        std::string u = q.front();
        q.pop();
        order.push_back(u);

        // For all nodes that depend on u
        auto it = m_rev_adj.find(u);
        if (it != m_rev_adj.end()) {
            for (const auto& dependent : it->second) {
                in_degree[dependent]--;
                if (in_degree[dependent] == 0) {
                    q.push(dependent);
                }
            }
        }
    }

    if (order.size() < m_nodes.size()) {
        // There was a cycle, fallback to original order
        return m_nodes;
    }

    return order;
}

std::unordered_set<std::string> DependencyGraph::get_invalidation_closure(const std::string& changed_module, bool is_public_signature_change) const {
    std::unordered_set<std::string> invalid;
    invalid.insert(changed_module);

    if (!is_public_signature_change) {
        // Only internal implementation change: no dependents invalidated!
        return invalid;
    }

    // BFS along reverse adjacency (dependents)
    std::queue<std::string> q;
    q.push(changed_module);

    while (!q.empty()) {
        std::string u = q.front();
        q.pop();

        auto it = m_rev_adj.find(u);
        if (it != m_rev_adj.end()) {
            for (const auto& dep : it->second) {
                if (invalid.find(dep) == invalid.end()) {
                    invalid.insert(dep);
                    q.push(dep);
                }
            }
        }
    }

    return invalid;
}

} // namespace EngineScript
