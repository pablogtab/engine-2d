#include <cassert>
#include <iostream>
#include "enginescript/core/SourceManager.hpp"
#include "enginescript/lexer/Lexer.hpp"
#include "enginescript/parser/Parser.hpp"
#include "enginescript/symbols/SymbolResolver.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptResolverTests...\n";

    SourceManager sm("/workspace");
    DiagnosticEngine diags;

    // File 1: Data/Item.e2s
    std::string item_code = R"(
        export struct ItemSlot {
            itemId: Int = 0;
            count: Int = 1;
        }
    )";
    FileId f1 = sm.register_source("Source/Data/Item.e2s", item_code);
    Lexer l1(f1, item_code, diags);
    Parser p1(f1, "Source/Data/Item.e2s", l1.tokenize_all(), diags);
    auto m1 = p1.parse_module();

    // File 2: Behaviours/Inventory.e2s (imports ItemSlot)
    std::string inv_code = R"(
        import { ItemSlot } from "../Data/Item";

        export class Inventory {
            item: ItemSlot;
        }
    )";
    FileId f2 = sm.register_source("Source/Behaviours/Inventory.e2s", inv_code);
    Lexer l2(f2, inv_code, diags);
    Parser p2(f2, "Source/Behaviours/Inventory.e2s", l2.tokenize_all(), diags);
    auto m2 = p2.parse_module();

    SymbolResolver resolver(sm, diags);
    bool ok = resolver.resolve_all({m1, m2});

    assert(ok);
    assert(!diags.has_errors());

    auto inv_scope = resolver.get_module_scope("Source/Behaviours/Inventory.e2s");
    assert(inv_scope != nullptr);
    auto item_sym = inv_scope->lookup("ItemSlot");
    assert(item_sym != nullptr);
    assert(item_sym->kind == SymbolKind::Import);

    // Test 3: Public signature hashing
    std::string sig1 = resolver.compute_public_signature_hash(*m1);
    assert(!sig1.empty());

    // Test 4: Duplicate declaration detection
    DiagnosticEngine err_diags;
    std::string dup_code = R"(
        class Foo {}
        class Foo {}
    )";
    FileId f3 = sm.register_source("Source/Dup.e2s", dup_code);
    Lexer l3(f3, dup_code, err_diags);
    Parser p3(f3, "Source/Dup.e2s", l3.tokenize_all(), err_diags);
    auto m3 = p3.parse_module();
    SymbolResolver res_dup(sm, err_diags);
    res_dup.resolve_module(m3);
    assert(err_diags.has_errors());
    assert(err_diags.error_count() == 1);

    std::cout << "[PASS] EngineScriptResolverTests passed!\n";
    return 0;
}
