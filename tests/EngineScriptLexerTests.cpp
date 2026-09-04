#include <cassert>
#include <iostream>
#include "enginescript/lexer/Lexer.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptLexerTests...\n";

    DiagnosticEngine diags;

    // Test 1: Full tokenization of platformer sample
    std::string sample = R"(
        @behaviour
        export class PlatformerPlayer extends Behaviour {
            @expose({ min: 0.0, max: 1000.0 })
            walkSpeed: Float = 220.0;

            async respawn(): Task<Void> {
                await delay(500.ms);
                animator?.play("idle");
            }
        }
    )";

    Lexer lexer(1, sample, diags);
    auto tokens = lexer.tokenize_all();

    assert(!diags.has_errors());
    assert(tokens.size() > 20);
    assert(tokens[0].type == TokenType::At);
    assert(tokens[1].type == TokenType::Identifier && tokens[1].text == "behaviour");
    assert(tokens[2].type == TokenType::Export);
    assert(tokens[3].type == TokenType::Class);

    // Test 2: Duration literals
    std::string dur_code = "500.ms 2.seconds 10.s 5.min 1.hours";
    Lexer dur_lexer(2, dur_code, diags);
    auto dur_tokens = dur_lexer.tokenize_all();

    assert(dur_tokens[0].type == TokenType::DurationLiteral);
    assert(dur_tokens[0].duration_seconds == 0.5);

    assert(dur_tokens[1].type == TokenType::DurationLiteral);
    assert(dur_tokens[1].duration_seconds == 2.0);

    assert(dur_tokens[2].type == TokenType::DurationLiteral);
    assert(dur_tokens[2].duration_seconds == 10.0);

    assert(dur_tokens[3].type == TokenType::DurationLiteral);
    assert(dur_tokens[3].duration_seconds == 300.0);

    assert(dur_tokens[4].type == TokenType::DurationLiteral);
    assert(dur_tokens[4].duration_seconds == 3600.0);

    // Test 3: Hex and binary integers
    std::string num_code = "0xFF 0b1010 42 3.1415";
    Lexer num_lexer(3, num_code, diags);
    auto num_tokens = num_lexer.tokenize_all();

    assert(num_tokens[0].type == TokenType::IntegerLiteral && num_tokens[0].int_value == 255);
    assert(num_tokens[1].type == TokenType::IntegerLiteral && num_tokens[1].int_value == 10);
    assert(num_tokens[2].type == TokenType::IntegerLiteral && num_tokens[2].int_value == 42);
    assert(num_tokens[3].type == TokenType::FloatLiteral && num_tokens[3].float_value > 3.14);

    // Test 4: String escapes
    std::string str_code = R"("Hello\nWorld\t\"Escaped\"")";
    Lexer str_lexer(4, str_code, diags);
    auto str_tokens = str_lexer.tokenize_all();
    assert(str_tokens[0].type == TokenType::StringLiteral);
    assert(str_tokens[0].string_value == "Hello\nWorld\t\"Escaped\"");

    // Test 5: Comments
    std::string comm_code = "let x = 1; // single line\n/* multi\nline */ let y = 2;";
    Lexer comm_lexer(5, comm_code, diags);
    auto comm_tokens = comm_lexer.tokenize_all();
    assert(comm_tokens[0].type == TokenType::Let);
    assert(comm_tokens[5].type == TokenType::Let);

    // Test 6: Lexical error recovery
    DiagnosticEngine err_diags;
    std::string bad_code = "let # $ @bad \"unterminated";
    Lexer bad_lexer(6, bad_code, err_diags);
    auto bad_tokens = bad_lexer.tokenize_all();
    assert(err_diags.has_errors());
    assert(err_diags.error_count() >= 2);
    assert(bad_tokens.back().is_eof());

    // Test 7: Multiline interpolated strings preserve text, escapes and expression boundaries.
    DiagnosticEngine template_diags;
    const std::string template_code = "`line 1\\n${score:02}\\n\\${literal} \\`tick\\``";
    Lexer template_lexer(7, template_code, template_diags);
    const auto template_tokens = template_lexer.tokenize_all();
    assert(!template_diags.has_errors());
    assert(template_tokens.size() == 10);
    assert(template_tokens[0].type == TokenType::TemplateStart);
    assert(template_tokens[1].type == TokenType::TemplateText);
    assert(template_tokens[1].string_value == "line 1\n");
    assert(template_tokens[2].type == TokenType::TemplateExprStart);
    assert(template_tokens[3].type == TokenType::Identifier);
    assert(template_tokens[4].type == TokenType::Colon);
    assert(template_tokens[5].type == TokenType::IntegerLiteral);
    assert(template_tokens[6].type == TokenType::TemplateExprEnd);
    assert(template_tokens[7].type == TokenType::TemplateText);
    assert(template_tokens[7].string_value == "\n${literal} `tick`");
    assert(template_tokens[template_tokens.size() - 2].type == TokenType::TemplateEnd);

    std::cout << "[PASS] EngineScriptLexerTests passed!\n";
    return 0;
}
