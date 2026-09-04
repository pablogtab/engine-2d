#include "Lexer.hpp"
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace EngineScript {

static const std::unordered_map<std::string_view, TokenType> s_keywords = {
    {"class",       TokenType::Class},
    {"struct",      TokenType::Struct},
    {"interface",   TokenType::Interface},
    {"enum",        TokenType::Enum},
    {"function",    TokenType::Function},
    {"constructor", TokenType::Constructor},
    {"let",         TokenType::Let},
    {"const",       TokenType::Const},
    {"export",      TokenType::Export},
    {"import",      TokenType::Import},
    {"from",        TokenType::From},
    {"as",          TokenType::As},
    {"extends",     TokenType::Extends},
    {"implements",  TokenType::Implements},
    {"override",    TokenType::Override},
    {"static",      TokenType::Static},
    {"async",       TokenType::Async},
    {"await",       TokenType::Await},
    {"private",     TokenType::Private},
    {"protected",   TokenType::Protected},
    {"public",      TokenType::Public},
    {"if",          TokenType::If},
    {"else",        TokenType::Else},
    {"while",       TokenType::While},
    {"for",         TokenType::For},
    {"guard",       TokenType::Guard},
    {"match",       TokenType::Match},
    {"in",          TokenType::In},
    {"return",      TokenType::Return},
    {"break",       TokenType::Break},
    {"continue",    TokenType::Continue},
    {"true",        TokenType::True},
    {"false",       TokenType::False},
    {"null",        TokenType::Null},
    {"this",        TokenType::This}
};

Lexer::Lexer(FileId file_id, std::string_view source, DiagnosticEngine& diagnostics)
    : m_file_id(file_id), m_source(source), m_diagnostics(diagnostics) {
}

char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return m_source[m_current];
}

char Lexer::peek_next() const {
    if (m_current + 1 >= m_source.size()) return '\0';
    return m_source[m_current + 1];
}

char Lexer::advance() {
    char c = m_source[m_current++];
    if (c == '\n') {
        m_line++;
        m_column = 1;
    } else {
        m_column++;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (is_at_end()) return false;
    if (m_source[m_current] != expected) return false;
    advance();
    return true;
}

bool Lexer::is_at_end() const {
    return m_current >= m_source.size();
}

SourceSpan Lexer::current_span(uint32_t start_offset) {
    SourceSpan span;
    span.file_id = m_file_id;
    span.start_offset = start_offset;
    span.end_offset = m_current;
    span.start_line = m_start_line;
    span.start_column = m_start_column;
    span.end_line = m_line;
    span.end_column = m_column;
    return span;
}

Token Lexer::make_token(TokenType type) {
    std::string text(m_source.substr(m_start, m_current - m_start));
    return make_token(type, std::move(text));
}

Token Lexer::make_token(TokenType type, std::string text) {
    Token tok;
    tok.type = type;
    tok.text = std::move(text);
    tok.span = current_span(m_start);
    return tok;
}

void Lexer::skip_whitespace_and_comments() {
    while (!is_at_end()) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
            case '\n':
                advance();
                break;
            case '/':
                if (peek_next() == '/') {
                    // Line comment
                    advance(); // /
                    advance(); // /
                    while (!is_at_end() && peek() != '\n') {
                        advance();
                    }
                } else if (peek_next() == '*') {
                    // Block comment
                    uint32_t comment_start = m_current;
                    uint32_t comment_line = m_line;
                    uint32_t comment_col = m_column;
                    advance(); // /
                    advance(); // *
                    bool closed = false;
                    while (!is_at_end()) {
                        if (peek() == '*' && peek_next() == '/') {
                            advance(); // *
                            advance(); // /
                            closed = true;
                            break;
                        }
                        advance();
                    }
                    if (!closed) {
                        SourceSpan span;
                        span.file_id = m_file_id;
                        span.start_offset = comment_start;
                        span.end_offset = m_current;
                        span.start_line = comment_line;
                        span.start_column = comment_col;
                        span.end_line = m_line;
                        span.end_column = m_column;
                        m_diagnostics.report_error("E0001", "Unterminated block comment", span);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

Token Lexer::scan_identifier_or_keyword() {
    while (!is_at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        advance();
    }

    std::string_view text = m_source.substr(m_start, m_current - m_start);
    auto it = s_keywords.find(text);
    if (it != s_keywords.end()) {
        return make_token(it->second);
    }
    return make_token(TokenType::Identifier);
}

Token Lexer::scan_number() {
    bool is_float = false;
    bool is_hex = false;
    bool is_bin = false;

    if (m_source[m_start] == '0') {
        if (peek() == 'x' || peek() == 'X') {
            is_hex = true;
            advance();
            while (!is_at_end() && std::isxdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        } else if (peek() == 'b' || peek() == 'B') {
            is_bin = true;
            advance();
            while (!is_at_end() && (peek() == '0' || peek() == '1')) {
                advance();
            }
        }
    }

    if (!is_hex && !is_bin) {
        while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }

        // Check for float decimal or duration dot
        if (peek() == '.' && (std::isdigit(static_cast<unsigned char>(peek_next())) || std::isalpha(static_cast<unsigned char>(peek_next())))) {
            if (std::isalpha(static_cast<unsigned char>(peek_next()))) {
                // Suffix like 500.ms or 2.seconds
                advance(); // Consume '.'
                uint32_t unit_start = m_current;
                while (!is_at_end() && std::isalpha(static_cast<unsigned char>(peek()))) {
                    advance();
                }
                std::string num_str(m_source.substr(m_start, unit_start - 1 - m_start));
                std::string unit(m_source.substr(unit_start, m_current - unit_start));
                double val = std::strtod(num_str.c_str(), nullptr);
                double seconds = 0.0;
                if (unit == "ms") {
                    seconds = val / 1000.0;
                } else if (unit == "s" || unit == "seconds" || unit == "second" || unit == "sec") {
                    seconds = val;
                } else if (unit == "min" || unit == "minutes" || unit == "minute") {
                    seconds = val * 60.0;
                } else if (unit == "h" || unit == "hours" || unit == "hour") {
                    seconds = val * 3600.0;
                } else {
                    m_diagnostics.report_error("E0004", "Unknown duration unit: " + unit, current_span(m_start));
                }
                Token tok = make_token(TokenType::DurationLiteral);
                tok.float_value = val;
                tok.duration_unit = unit;
                tok.duration_seconds = seconds;
                return tok;
            } else {
                is_float = true;
                advance(); // Consume '.'
                while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    advance();
                }

                // Check for duration suffix on float (e.g. 0.5.seconds or 1.5.s)
                if (peek() == '.' && std::isalpha(static_cast<unsigned char>(peek_next()))) {
                    advance(); // Consume '.'
                    uint32_t unit_start = m_current;
                    while (!is_at_end() && std::isalpha(static_cast<unsigned char>(peek()))) {
                        advance();
                    }
                    std::string num_str(m_source.substr(m_start, unit_start - 1 - m_start));
                    std::string unit(m_source.substr(unit_start, m_current - unit_start));
                    double val = std::strtod(num_str.c_str(), nullptr);
                    double seconds = (unit == "ms") ? (val / 1000.0) : val;
                    Token tok = make_token(TokenType::DurationLiteral);
                    tok.float_value = val;
                    tok.duration_unit = unit;
                    tok.duration_seconds = seconds;
                    return tok;
                }
            }
        }
    }

    // Check optional trailing 'f' for float
    if (!is_hex && !is_bin && (peek() == 'f' || peek() == 'F')) {
        is_float = true;
        advance();
    }

    std::string text(m_source.substr(m_start, m_current - m_start));
    if (is_float) {
        Token tok = make_token(TokenType::FloatLiteral, std::move(text));
        tok.float_value = std::strtod(tok.text.c_str(), nullptr);
        return tok;
    } else {
        Token tok = make_token(TokenType::IntegerLiteral, std::move(text));
        if (is_hex) {
            tok.int_value = std::strtoll(tok.text.c_str() + 2, nullptr, 16);
        } else if (is_bin) {
            tok.int_value = std::strtoll(tok.text.c_str() + 2, nullptr, 2);
        } else {
            tok.int_value = std::strtoll(tok.text.c_str(), nullptr, 10);
        }
        return tok;
    }
}

Token Lexer::scan_string() {
    std::string parsed;
    bool closed = false;

    while (!is_at_end()) {
        char c = peek();
        if (c == '"') {
            advance();
            closed = true;
            break;
        }
        if (c == '\n' || c == '\r') {
            break; // Unterminated string on newline
        }
        if (c == '\\') {
            advance();
            if (is_at_end()) break;
            char esc = advance();
            switch (esc) {
                case 'n':  parsed.push_back('\n'); break;
                case 't':  parsed.push_back('\t'); break;
                case 'r':  parsed.push_back('\r'); break;
                case '\\': parsed.push_back('\\'); break;
                case '"':  parsed.push_back('"'); break;
                case '0':  parsed.push_back('\0'); break;
                default:
                    parsed.push_back(esc);
                    break;
            }
        } else {
            parsed.push_back(advance());
        }
    }

    if (!closed) {
        m_diagnostics.report_error("E0002", "Unterminated string literal", current_span(m_start));
    }

    Token tok = make_token(TokenType::StringLiteral);
    tok.string_value = std::move(parsed);
    return tok;
}

Token Lexer::scan_template_text() {
    std::string parsed;
    while (!is_at_end()) {
        if (peek() == '`' || (peek() == '$' && peek_next() == '{')) break;

        if (peek() == '\\') {
            advance();
            if (is_at_end()) break;
            const char escaped = advance();
            switch (escaped) {
                case 'n': parsed.push_back('\n'); break;
                case 't': parsed.push_back('\t'); break;
                case 'r': parsed.push_back('\r'); break;
                case '\\': parsed.push_back('\\'); break;
                case '`': parsed.push_back('`'); break;
                case '$':
                    if (peek() == '{') {
                        advance();
                        parsed += "${";
                    } else {
                        parsed.push_back('$');
                    }
                    break;
                default: parsed.push_back(escaped); break;
            }
        } else {
            parsed.push_back(advance());
        }
    }

    Token token = make_token(TokenType::TemplateText);
    token.string_value = std::move(parsed);
    return token;
}

Token Lexer::next_token() {
    if (m_in_template_text) {
        m_start = m_current;
        m_start_line = m_line;
        m_start_column = m_column;

        if (is_at_end()) {
            m_in_template_text = false;
            m_diagnostics.report_error("E0005", "Unterminated interpolated string", current_span(m_start));
            return make_token(TokenType::Eof, "");
        }
        if (peek() == '`') {
            advance();
            m_in_template_text = false;
            return make_token(TokenType::TemplateEnd);
        }
        if (peek() == '$' && peek_next() == '{') {
            advance();
            advance();
            m_in_template_text = false;
            m_in_template_expression = true;
            m_template_brace_depth = 0;
            return make_token(TokenType::TemplateExprStart);
        }
        return scan_template_text();
    }

    skip_whitespace_and_comments();

    m_start = m_current;
    m_start_line = m_line;
    m_start_column = m_column;

    if (is_at_end()) {
        return make_token(TokenType::Eof, "");
    }

    char c = advance();

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return scan_identifier_or_keyword();
    }

    if (std::isdigit(static_cast<unsigned char>(c))) {
        return scan_number();
    }

    switch (c) {
        case '@': return make_token(TokenType::At);
        case '(': return make_token(TokenType::OpenParen);
        case ')': return make_token(TokenType::CloseParen);
        case '{':
            if (m_in_template_expression) ++m_template_brace_depth;
            return make_token(TokenType::OpenBrace);
        case '}':
            if (m_in_template_expression) {
                if (m_template_brace_depth == 0) {
                    m_in_template_expression = false;
                    m_in_template_text = true;
                    return make_token(TokenType::TemplateExprEnd);
                }
                --m_template_brace_depth;
            }
            return make_token(TokenType::CloseBrace);
        case '[': return make_token(TokenType::OpenBracket);
        case ']': return make_token(TokenType::CloseBracket);
        case ',': return make_token(TokenType::Comma);
        case ';': return make_token(TokenType::Semicolon);
        case ':':
            if (match(':')) return make_token(TokenType::DoubleColon);
            return make_token(TokenType::Colon);
        case '.':
            return make_token(TokenType::Dot);
        case '?':
            if (match('.')) return make_token(TokenType::QuestionDot);
            if (match('?')) return make_token(TokenType::QuestionQuestion);
            return make_token(TokenType::Question);
        case '+':
            if (match('=')) return make_token(TokenType::PlusEquals);
            return make_token(TokenType::Plus);
        case '-':
            if (match('=')) return make_token(TokenType::MinusEquals);
            return make_token(TokenType::Minus);
        case '*':
            if (match('=')) return make_token(TokenType::StarEquals);
            return make_token(TokenType::Star);
        case '/':
            if (match('=')) return make_token(TokenType::SlashEquals);
            return make_token(TokenType::Slash);
        case '%':
            return make_token(TokenType::Percent);
        case '=':
            if (match('=')) return make_token(TokenType::EqualsEquals);
            if (match('>')) return make_token(TokenType::Arrow);
            return make_token(TokenType::Equals);
        case '!':
            if (match('=')) return make_token(TokenType::ExclamationEquals);
            return make_token(TokenType::Exclamation);
        case '<':
            if (match('=')) return make_token(TokenType::LessEquals);
            return make_token(TokenType::Less);
        case '>':
            if (match('=')) return make_token(TokenType::GreaterEquals);
            return make_token(TokenType::Greater);
        case '&':
            if (match('&')) return make_token(TokenType::AmpAmp);
            break;
        case '|':
            if (match('|')) return make_token(TokenType::PipePipe);
            break;
        case '"':
            return scan_string();
        case '`':
            if (m_in_template_expression) {
                m_diagnostics.report_error("E0006", "Nested interpolated strings are not supported", current_span(m_start));
                return make_token(TokenType::Invalid);
            }
            m_in_template_text = true;
            return make_token(TokenType::TemplateStart);
    }

    // Invalid character
    m_diagnostics.report_error("E0003", std::string("Unexpected character: '") + c + "'", current_span(m_start));
    return make_token(TokenType::Invalid);
}

std::vector<Token> Lexer::tokenize_all() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = next_token();
        tokens.push_back(tok);
        if (tok.is_eof()) break;
    }
    return tokens;
}

} // namespace EngineScript
