#pragma once

#include "Symbol.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>

namespace EngineScript {

enum class ScopeKind {
    Global,
    Module,
    Class,
    Function,
    Block
};

class Scope : public std::enable_shared_from_this<Scope> {
public:
    explicit Scope(ScopeKind kind, std::shared_ptr<Scope> parent = nullptr)
        : m_kind(kind), m_parent(std::move(parent)) {}

    ScopeKind kind() const { return m_kind; }
    std::shared_ptr<Scope> parent() const { return m_parent; }

    bool define(std::shared_ptr<Symbol> symbol) {
        if (!symbol) return false;
        if (m_symbols.find(symbol->name) != m_symbols.end()) {
            return false; // Duplicate symbol in this scope
        }
        m_symbols[symbol->name] = symbol;
        return true;
    }

    std::shared_ptr<Symbol> lookup_local(const std::string& name) const {
        auto it = m_symbols.find(name);
        if (it != m_symbols.end()) {
            return it->second;
        }
        return nullptr;
    }

    std::shared_ptr<Symbol> lookup(const std::string& name) const {
        auto local = lookup_local(name);
        if (local) return local;
        if (m_parent) return m_parent->lookup(name);
        return nullptr;
    }

    const std::unordered_map<std::string, std::shared_ptr<Symbol>>& all_symbols() const {
        return m_symbols;
    }

    void add_child(std::shared_ptr<Scope> child) {
        m_children.push_back(child);
    }

    const std::vector<std::shared_ptr<Scope>>& children() const {
        return m_children;
    }

private:
    ScopeKind m_kind;
    std::shared_ptr<Scope> m_parent;
    std::unordered_map<std::string, std::shared_ptr<Symbol>> m_symbols;
    std::vector<std::shared_ptr<Scope>> m_children;
};

} // namespace EngineScript
