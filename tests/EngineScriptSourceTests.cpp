#include <cassert>
#include <iostream>
#include "enginescript/core/SourceManager.hpp"
#include "enginescript/core/DiagnosticEngine.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptSourceTests...\n";

    SourceManager sm("/workspace/MyGame");

    // Test 1: File registration and line indexing
    std::string code = "let a: Float = 1.0;\nlet b: Float = 2.0;\nlet c: Float = a + b;\n";
    FileId fid = sm.register_source("Source/Behaviours/Test.e2s", code);
    assert(fid != INVALID_FILE_ID);

    const SourceFile* sf = sm.get_file(fid);
    assert(sf != nullptr);
    assert(sf->path == "Source/Behaviours/Test.e2s");
    assert(sf->line_offsets.size() == 4); // Line 1, Line 2, Line 3, Line 4

    // Test 2: SourceLocation computation
    SourceLocation loc1 = sm.get_location(fid, 0);
    assert(loc1.line == 1 && loc1.column == 1);

    SourceLocation loc2 = sm.get_location(fid, 20); // Start of line 2
    assert(loc2.line == 2 && loc2.column == 1);

    // Test 3: Span merging and snippets
    SourceSpan span = sm.get_span(fid, 4, 18);
    assert(span.start_line == 1 && span.start_column == 5);
    std::string snippet = sm.get_snippet(span, 0);
    assert(!snippet.empty());
    assert(snippet.find("^^^^") != std::string::npos);

    // Test 4: Virtual document updates
    bool updated = sm.update_virtual_file(fid, "let x: Int = 42;\n", 2);
    assert(updated);
    assert(sm.get_file(fid)->content == "let x: Int = 42;\n");
    assert(sm.get_file(fid)->version == 2);

    // Test 5: Project boundary import safety check
    auto safe_import = sm.resolve_import("Source/Behaviours/Player.e2s", "../Data/Item.e2s");
    assert(safe_import.has_value());
    assert(*safe_import == "Source/Data/Item.e2s");

    auto unsafe_import = sm.resolve_import("Source/Behaviours/Player.e2s", "../../../etc/passwd");
    assert(!unsafe_import.has_value()); // Rejected escaping boundary!

    // Test 6: Diagnostics formatting
    DiagnosticEngine diag;
    diag.report_error("E1001", "Syntax error in declaration", span);
    assert(diag.has_errors());
    assert(diag.error_count() == 1);

    std::string cli_out = diag.render_cli(sm);
    assert(cli_out.find("E1001") != std::string::npos);

    std::string json_out = diag.render_json(sm);
    assert(json_out.find("\"code\": \"E1001\"") != std::string::npos);
    assert(json_out.find("\"explanation\"") != std::string::npos);
    assert(json_out.find("\"suggestedFix\"") != std::string::npos);
    assert(json_out.find("\"fixes\"") != std::string::npos);

    std::cout << "[PASS] EngineScriptSourceTests passed!\n";
    return 0;
}
