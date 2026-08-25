#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

#include "enginescript/core/SourceManager.hpp"
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/lsp/LanguageServer.hpp"
#include "enginescript/parser/Parser.hpp"

using namespace EngineScript;

namespace {

void parse_buffer(SourceManager& sources, std::uint32_t index, const std::string& source) {
    DiagnosticEngine diagnostics;
    const std::string path = "Source/Fuzz" + std::to_string(index) + ".e2s";
    const FileId file = sources.register_source(path, source);
    Lexer lexer(file, source, diagnostics);
    Parser parser(file, path, lexer.tokenize_all(), diagnostics);
    static_cast<void>(parser.parse_module());
}

} // namespace

int main() {
    std::cout << "[TEST] Running EngineScriptRecoveryFuzzTests..." << std::flush;

    const std::array<std::string, 18> recovery_corpus{{
        "",
        "@",
        "import {} from \"./Missing\";",
        "import { Value as } from \"./Values\";",
        "export class",
        "export class Broken extends {",
        "struct Value<T extends> { field: T; }",
        "enum Mode { A = 1, B = \"mixed\" }",
        "function run<T extends Send>(value: T): T { return",
        "function f(): Void { while (true) { break;",
        "function f(): Void { for (let item in ) {} }",
        "function f(): Void { let map = Map { \"a\" => }; }",
        "function f(): Void { let fixed: FixedArray<Float, 0> = FixedArray [; }",
        "function f(): Void { let closure = (x: Int) => ; }",
        "function f(value: String?): String { return value?.",
        "@behaviour export class B extends Behaviour { constructor(@component private body: ) {} }",
        "function f(): Void { let x = \"unterminated",
        "\x00\x01\x02 class \xff"
    }};

    SourceManager sources("/recovery-fuzz");
    std::uint32_t index = 0;
    for (const std::string& source : recovery_corpus) parse_buffer(sources, index++, source);

    std::mt19937 generator(0xE2C2020u);
    static constexpr std::string_view alphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
        "@{}[]()<>,.;:+-*/?=!&|_ \\n\\t\\\"";
    std::uniform_int_distribution<std::size_t> length_distribution(0, 384);
    std::uniform_int_distribution<std::size_t> character_distribution(0, alphabet.size() - 1);
    for (std::uint32_t sample = 0; sample < 64; ++sample) {
        std::string source;
        const std::size_t length = length_distribution(generator);
        source.reserve(length);
        for (std::size_t character = 0; character < length; ++character) {
            source.push_back(alphabet[character_distribution(generator)]);
        }
        parse_buffer(sources, index++, source);
    }

    // Exercise the full incremental LSP recovery path on representative
    // incomplete documents. Responses must remain valid JSON-RPC payloads.
    LanguageServer server("/recovery-lsp");
    const std::string uri = "file:///recovery-lsp/Source/Incomplete.e2s";
    std::uint32_t version = 1;
    for (const std::string& source : recovery_corpus) {
        const std::string response = version == 1
            ? server.on_did_open(uri, source, version)
            : server.on_did_change(uri, source, version);
        assert(!response.empty());
        ++version;
    }

    std::cout << "[PASS] EngineScriptRecoveryFuzzTests passed!\n";
    return 0;
}
