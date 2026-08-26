#pragma once

#include "Texture.hpp"
#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

enum class MaterialPackStatus {
    Ready,
    Unsupported,
    Failed
};

struct MaterialPackRegion {
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    constexpr bool operator==(const MaterialPackRegion&) const = default;
};

struct MaterialPackPixels {
    MaterialPackStatus status{MaterialPackStatus::Failed};
    int width{0};
    int height{0};
    std::vector<std::uint32_t> pixels;
};

struct MaterialPackRequest {
    Texture* albedo{nullptr};
    Texture* normal{nullptr};
    Texture* emissive{nullptr};
    Texture* specular{nullptr};
    MaterialPackRegion albedo_region{};
};

struct MaterialPackResult {
    MaterialPackStatus status{MaterialPackStatus::Failed};
    Texture* texture{nullptr};
    SDL_FRect albedo_rect{};
    std::uint64_t key{0};

    [[nodiscard]] bool ready() const {
        return status == MaterialPackStatus::Ready && texture != nullptr;
    }
};

struct MaterialPackCacheMetrics {
    std::size_t hits{0};
    std::size_t misses{0};
    std::size_t failures{0};
    std::size_t evictions{0};
    std::size_t disk_hits{0};
    std::size_t disk_writes{0};
    std::size_t resident_entries{0};
    std::size_t resident_bytes{0};
};

class MaterialPackBuilder {
public:
    static constexpr std::uint32_t Version = 1;
    static constexpr std::uint32_t FlatNormal = 0x8080ffffu;
    static constexpr std::uint32_t Black = 0x000000ffu;

    [[nodiscard]] static MaterialPackPixels build(
        const MaterialPackRequest& request,
        int max_dimension = 8192
    );

    [[nodiscard]] static std::uint64_t content_key(const MaterialPackRequest& request);
};

class MaterialPackCache {
public:
    explicit MaterialPackCache(
        SDL_Renderer* renderer,
        int max_dimension = 8192,
        std::size_t memory_budget = 64u * 1024u * 1024u
    ) : m_renderer(renderer), m_max_dimension(max_dimension), m_memory_budget(memory_budget) {}

    [[nodiscard]] MaterialPackResult request(const MaterialPackRequest& request);
    void clear();
    void set_max_dimension(int value);
    void set_memory_budget(std::size_t bytes);
    void set_disk_cache_directory(std::filesystem::path directory);

    [[nodiscard]] int max_dimension() const { return m_max_dimension; }
    [[nodiscard]] std::size_t memory_budget() const { return m_memory_budget; }
    [[nodiscard]] const MaterialPackCacheMetrics& metrics() const { return m_metrics; }
    [[nodiscard]] const std::filesystem::path& disk_cache_directory() const { return m_disk_cache_directory; }

private:
    struct Entry {
        std::unique_ptr<Texture> texture;
        SDL_FRect albedo_rect{};
        std::size_t byte_size{0};
        std::uint64_t last_used{0};
    };

    SDL_Renderer* m_renderer{nullptr};
    int m_max_dimension{8192};
    std::size_t m_memory_budget{64u * 1024u * 1024u};
    std::uint64_t m_use_counter{0};
    std::filesystem::path m_disk_cache_directory;
    std::unordered_map<std::uint64_t, Entry> m_entries;
    MaterialPackCacheMetrics m_metrics{};

    [[nodiscard]] std::unique_ptr<Texture> load_from_disk(
        std::uint64_t key,
        int expected_width,
        int expected_height
    );
    void persist_to_disk(std::uint64_t key, const MaterialPackPixels& packed);
    void evict_until_fits(std::size_t byte_size);
};

} // namespace Engine
