#pragma once

#include "Token.hpp"
#include "../core/SourceLocation.hpp"
#include "../core/DiagnosticEngine.hpp"
#include <string_view>
#include <vector>

namespace EngineScript {

class Lexer {
public:
    Lexer(FileId file_id, std::string_view source, DiagnosticEngine& diagnostics);

    std::vector<Token> tokenize_all();
    Token next_token();

    bool has_errors() const { return m_diagnostics.has_errors(); }

private:
    char peek() const;
    char peek_next() const;
    char advance();
    bool match(char expected);
    bool is_at_end() const;

    void skip_whitespace_and_comments();
    Token make_token(TokenType type);
    Token make_token(TokenType type, std::string text);
    SourceSpan current_span(uint32_t start_offset);

    Token scan_identifier_or_keyword();
    Token scan_number();
    Token scan_string();

    FileId m_file_id;
    std::string_view m_source;
    DiagnosticEngine& m_diagnostics;

    uint32_t m_start = 0;
    uint32_t m_current = 0;
    uint32_t m_line = 1;
    uint32_t m_column = 1;
    uint32_t m_start_line = 1;
    uint32_t m_start_column = 1;
};

} // namespace EngineScript
