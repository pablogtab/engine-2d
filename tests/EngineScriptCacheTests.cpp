#include <cassert>
#include <iostream>
#include "enginescript/cache/CompilerCache.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptCacheTests...\n";

    CompilerCache cache("/tmp/test_cache");

    std::string content1 = "export class Player {}";
    std::string hash1 = CompilerCache::hash_content(content1);

    CacheEntry entry1;
    entry1.module_path = "Source/Player.e2s";
    entry1.content_hash = hash1;
    entry1.sdk_hash = "engine2d_abi_v1";
    entry1.generated_header = "// Player header";
    entry1.generated_source = "// Player source";
    entry1.metadata_json = "{}";

    cache.store(entry1);

    // Test 1: Hit
    const CacheEntry* hit = cache.lookup("Source/Player.e2s", hash1, "engine2d_abi_v1");
    assert(hit != nullptr);
    assert(hit->generated_header == "// Player header");
    assert(cache.stats().hits == 1);
    assert(cache.stats().misses == 0);

    // Test 2: Miss on modified content
    std::string content2 = "export class Player { let speed: Float = 10.0; }";
    std::string hash2 = CompilerCache::hash_content(content2);
    const CacheEntry* miss = cache.lookup("Source/Player.e2s", hash2, "engine2d_abi_v1");
    assert(miss == nullptr);
    assert(cache.stats().misses == 1);

    // Test 3: Invalidation
    cache.invalidate("Source/Player.e2s");
    assert(cache.stats().evictions == 1);
    assert(cache.lookup("Source/Player.e2s", hash1, "engine2d_abi_v1") == nullptr);

    std::cout << "[PASS] EngineScriptCacheTests passed!\n";
    return 0;
}
