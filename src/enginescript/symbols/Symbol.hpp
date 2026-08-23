#pragma once

#include "../core/SourceLocation.hpp"
#include "../ast/AST.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace EngineScript {

enum class SymbolKind {
    Variable,
    Parameter,
    Field,
    Method,
    Function,
    Class,
    Struct,
    Interface,
    Enum,
    EnumMember,
    TypeParameter,
    Import
};

inline const char* symbol_kind_to_string(SymbolKind k) {
    switch (k) {
        case SymbolKind::Variable:      return "Variable";
        case SymbolKind::Parameter:     return "Parameter";
        case SymbolKind::Field:         return "Field";
        case SymbolKind::Method:        return "Method";
        case SymbolKind::Function:      return "Function";
        case SymbolKind::Class:         return "Class";
        case SymbolKind::Struct:        return "Struct";
        case SymbolKind::Interface:     return "Interface";
        case SymbolKind::Enum:          return "Enum";
        case SymbolKind::EnumMember:    return "EnumMember";
        case SymbolKind::TypeParameter: return "TypeParameter";
        case SymbolKind::Import:        return "Import";
    }
    return "Unknown";
}

struct Symbol {
    uint64_t id = 0;
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    SourceSpan span;
    FileId file_id = INVALID_FILE_ID;
    AccessModifier access = AccessModifier::Public;
    bool is_exported = false;
    bool is_const = false;
    bool is_async = false;
    bool is_behaviour = false;

    std::string type_string; // String representation of declared or resolved type
    std::string signature_hash; // Hash of the public signature (for cache invalidation)

    std::shared_ptr<ASTNode> ast_node;
    std::weak_ptr<Symbol> parent_symbol;
    std::vector<std::shared_ptr<Symbol>> members; // For classes/structs/enums
};

} // namespace EngineScript
