#pragma once

#include <string_view>

namespace EngineScript {

enum class TokenType {
    Eof,
    Invalid,

    // Keywords
    Class,
    Struct,
    Interface,
    Enum,
    Function,
    Constructor,
    Let,
    Const,
    Export,
    Import,
    From,
    As,
    Extends,
    Implements,
    Override,
    Async,
    Await,
    Private,
    Protected,
    Public,
    If,
    Else,
    While,
    For,
    In,
    Return,
    Break,
    Continue,
    True,
    False,
    Null,
    This,

    // Literals & Identifiers
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    DurationLiteral,
    StringLiteral,

    // Symbols & Operators
    At,                // @
    Dot,               // .
    QuestionDot,       // ?.
    QuestionQuestion,  // ??
    Question,          // ?
    Colon,             // :
    DoubleColon,       // ::
    Semicolon,         // ;
    Comma,             // ,
    Arrow,             // =>

    // Grouping
    OpenParen,         // (
    CloseParen,        // )
    OpenBrace,         // {
    CloseBrace,        // }
    OpenBracket,       // [
    CloseBracket,      // ]

    // Arithmetic
    Plus,              // +
    Minus,             // -
    Star,              // *
    Slash,             // /
    Percent,           // %

    // Assignment
    Equals,            // =
    PlusEquals,        // +=
    MinusEquals,       // -=
    StarEquals,        // *=
    SlashEquals,       // /=

    // Comparison
    EqualsEquals,      // ==
    ExclamationEquals, // !=
    Less,              // <
    LessEquals,        // <=
    Greater,           // >
    GreaterEquals,     // >=

    // Logical
    Exclamation,       // !
    AmpAmp,            // &&
    PipePipe           // ||
};

inline const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::Eof:               return "Eof";
        case TokenType::Invalid:           return "Invalid";
        case TokenType::Class:             return "class";
        case TokenType::Struct:            return "struct";
        case TokenType::Interface:         return "interface";
        case TokenType::Enum:              return "enum";
        case TokenType::Function:          return "function";
        case TokenType::Constructor:       return "constructor";
        case TokenType::Let:               return "let";
        case TokenType::Const:             return "const";
        case TokenType::Export:            return "export";
        case TokenType::Import:            return "import";
        case TokenType::From:              return "from";
        case TokenType::As:                return "as";
        case TokenType::Extends:           return "extends";
        case TokenType::Implements:        return "implements";
        case TokenType::Override:          return "override";
        case TokenType::Async:             return "async";
        case TokenType::Await:             return "await";
        case TokenType::Private:           return "private";
        case TokenType::Protected:         return "protected";
        case TokenType::Public:            return "public";
        case TokenType::If:                return "if";
        case TokenType::Else:              return "else";
        case TokenType::While:             return "while";
        case TokenType::For:               return "for";
        case TokenType::In:                return "in";
        case TokenType::Return:            return "return";
        case TokenType::Break:             return "break";
        case TokenType::Continue:          return "continue";
        case TokenType::True:              return "true";
        case TokenType::False:             return "false";
        case TokenType::Null:              return "null";
        case TokenType::This:              return "this";
        case TokenType::Identifier:        return "Identifier";
        case TokenType::IntegerLiteral:    return "IntegerLiteral";
        case TokenType::FloatLiteral:      return "FloatLiteral";
        case TokenType::DurationLiteral:   return "DurationLiteral";
        case TokenType::StringLiteral:     return "StringLiteral";
        case TokenType::At:                return "@";
        case TokenType::Dot:               return ".";
        case TokenType::QuestionDot:       return "?.";
        case TokenType::QuestionQuestion:  return "??";
        case TokenType::Question:          return "?";
        case TokenType::Colon:             return ":";
        case TokenType::DoubleColon:       return "::";
        case TokenType::Semicolon:         return ";";
        case TokenType::Comma:             return ",";
        case TokenType::Arrow:             return "=>";
        case TokenType::OpenParen:         return "(";
        case TokenType::CloseParen:        return ")";
        case TokenType::OpenBrace:         return "{";
        case TokenType::CloseBrace:        return "}";
        case TokenType::OpenBracket:       return "[";
        case TokenType::CloseBracket:      return "]";
        case TokenType::Plus:              return "+";
        case TokenType::Minus:             return "-";
        case TokenType::Star:              return "*";
        case TokenType::Slash:             return "/";
        case TokenType::Percent:           return "%";
        case TokenType::Equals:            return "=";
        case TokenType::PlusEquals:        return "+=";
        case TokenType::MinusEquals:       return "-=";
        case TokenType::StarEquals:        return "*=";
        case TokenType::SlashEquals:       return "/=";
        case TokenType::EqualsEquals:      return "==";
        case TokenType::ExclamationEquals: return "!=";
        case TokenType::Less:              return "<";
        case TokenType::LessEquals:        return "<=";
        case TokenType::Greater:           return ">";
        case TokenType::GreaterEquals:     return ">=";
        case TokenType::Exclamation:       return "!";
        case TokenType::AmpAmp:            return "&&";
        case TokenType::PipePipe:          return "||";
    }
    return "Unknown";
}

} // namespace EngineScript
