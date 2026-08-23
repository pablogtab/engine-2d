#pragma once

#include "TokenType.hpp"
#include "../core/SourceLocation.hpp"
#include <string>
#include <cstdint>

namespace EngineScript {

struct Token {
    TokenType type = TokenType::Invalid;
    std::string text;
    SourceSpan span;

    // Parsed literal values
    int64_t int_value = 0;
    double float_value = 0.0;
    std::string string_value;
    std::string duration_unit;
    double duration_seconds = 0.0;

    bool is(TokenType t) const { return type == t; }
    bool is_not(TokenType t) const { return type != t; }
    bool is_eof() const { return type == TokenType::Eof; }
    bool is_keyword() const {
        return type >= TokenType::Class && type <= TokenType::This;
    }
};

} // namespace EngineScript
