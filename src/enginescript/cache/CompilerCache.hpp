#pragma once

#include "../core/LanguageVersion.hpp"

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

namespace EngineScript {

struct CacheEntry {
    std::string module_path;
    std::string content_hash;
    std::string public_signature_hash;
    std::string sdk_hash;
    uint32_t language_version = ENGINE_SCRIPT_LANGUAGE_VERSION;
    std::string compiler_cache_id = ENGINE_SCRIPT_COMPILER_CACHE_ID;
    std::string generated_header;
    std::string generated_source;
    std::string generated_header_filename;
    std::string generated_source_filename;
    std::string metadata_json;
    uint64_t timestamp = 0;
};

struct CacheStats {
    size_t hits = 0;
    size_t misses = 0;
    size_t evictions = 0;
};

class CompilerCache {
public:
    explicit CompilerCache(std::string cache_dir = "");

    void store(const CacheEntry& entry);
    const CacheEntry* lookup(const std::string& module_path, const std::string& content_hash, const std::string& sdk_hash) const;
    void invalidate(const std::string& module_path);
    void clear();

    const CacheStats& stats() const { return m_stats; }
    void reset_stats() { m_stats = CacheStats{}; }

    static std::string hash_content(const std::string& content);

private:
    std::string m_cache_dir;
    std::unordered_map<std::string, CacheEntry> m_entries;
    mutable CacheStats m_stats;
};

} // namespace EngineScript
