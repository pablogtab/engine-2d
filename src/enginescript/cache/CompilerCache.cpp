#include "CompilerCache.hpp"
#include <sstream>
#include <iomanip>

namespace EngineScript {

CompilerCache::CompilerCache(std::string cache_dir)
    : m_cache_dir(std::move(cache_dir)) {
}

std::string CompilerCache::hash_content(const std::string& content) {
    // Simple 64-bit FNV-1a hash formatted as hex
    uint64_t hash = 14695981039346656037ull;
    for (char c : content) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ull;
    }
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

void CompilerCache::store(const CacheEntry& entry) {
    m_entries[entry.module_path] = entry;
}

const CacheEntry* CompilerCache::lookup(const std::string& module_path, const std::string& content_hash, const std::string& sdk_hash) const {
    auto it = m_entries.find(module_path);
    if (it != m_entries.end()) {
        if (it->second.content_hash == content_hash &&
            it->second.sdk_hash == sdk_hash &&
            it->second.language_version == ENGINE_SCRIPT_LANGUAGE_VERSION &&
            it->second.compiler_cache_id == ENGINE_SCRIPT_COMPILER_CACHE_ID) {
            m_stats.hits++;
            return &it->second;
        }
    }
    m_stats.misses++;
    return nullptr;
}

void CompilerCache::invalidate(const std::string& module_path) {
    auto it = m_entries.find(module_path);
    if (it != m_entries.end()) {
        m_entries.erase(it);
        m_stats.evictions++;
    }
}

void CompilerCache::clear() {
    m_entries.clear();
    m_stats = CacheStats{};
}

} // namespace EngineScript
