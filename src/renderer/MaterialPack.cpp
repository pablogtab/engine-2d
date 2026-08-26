#include "MaterialPack.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>

namespace Engine {

namespace {

constexpr std::uint64_t FnvOffset = 1469598103934665603ull;
constexpr std::uint64_t FnvPrime = 1099511628211ull;

void hash_value(std::uint64_t& hash, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= static_cast<std::uint8_t>(value >> (byte * 8));
        hash *= FnvPrime;
    }
}

std::uint64_t hash_pixels(const std::vector<std::uint32_t>& pixels) {
    std::uint64_t hash = FnvOffset;
    for (const std::uint32_t pixel : pixels) hash_value(hash, pixel);
    return hash;
}

std::string key_text(std::uint64_t key) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << key;
    return stream.str();
}

bool valid_region(const Texture& texture, const MaterialPackRegion& region) {
    if (region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0) return false;
    return region.x <= texture.get_width() - region.width &&
        region.y <= texture.get_height() - region.height;
}

MaterialPackRegion auxiliary_region(
    const Texture& texture,
    const MaterialPackRegion& albedo_region
) {
    if (valid_region(texture, albedo_region)) return albedo_region;
    if (texture.get_width() == albedo_region.width && texture.get_height() == albedo_region.height) {
        return {0, 0, albedo_region.width, albedo_region.height};
    }
    return {};
}

bool copy_region(
    const Texture& source,
    const MaterialPackRegion& region,
    std::vector<std::uint32_t>& destination,
    int destination_width,
    int offset_x,
    int offset_y
) {
    const auto pixels = source.pixels();
    if (!valid_region(source, region) || pixels.size() !=
        static_cast<std::size_t>(source.get_width()) * source.get_height()) return false;
    for (int y = 0; y < region.height; ++y) {
        const std::size_t source_offset = static_cast<std::size_t>(region.y + y) * source.get_width() + region.x;
        const std::size_t destination_offset = static_cast<std::size_t>(offset_y + y) * destination_width + offset_x;
        std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(source_offset), region.width,
                    destination.begin() + static_cast<std::ptrdiff_t>(destination_offset));
    }
    return true;
}

void hash_texture_region(
    std::uint64_t& hash,
    const Texture* texture,
    const MaterialPackRegion& albedo_region,
    std::uint32_t fallback
) {
    if (!texture) {
        hash_value(hash, fallback);
        return;
    }
    const MaterialPackRegion region = auxiliary_region(*texture, albedo_region);
    hash_value(hash, static_cast<std::uint64_t>(texture->get_width()));
    hash_value(hash, static_cast<std::uint64_t>(texture->get_height()));
    hash_value(hash, static_cast<std::uint64_t>(region.x));
    hash_value(hash, static_cast<std::uint64_t>(region.y));
    if (!valid_region(*texture, region) || texture->pixels().empty()) {
        hash_value(hash, std::numeric_limits<std::uint64_t>::max());
        return;
    }
    hash_value(hash, texture->content_hash());
}

} // namespace

MaterialPackPixels MaterialPackBuilder::build(
    const MaterialPackRequest& request,
    int max_dimension
) {
    MaterialPackPixels result;
    if (!request.albedo || max_dimension <= 0 ||
        !valid_region(*request.albedo, request.albedo_region) ||
        request.albedo->pixels().empty()) return result;

    const int width = request.albedo_region.width;
    const int height = request.albedo_region.height;
    if (width > max_dimension / 2 || height > max_dimension / 2) {
        result.status = MaterialPackStatus::Unsupported;
        return result;
    }

    const auto auxiliary_is_valid = [&](const Texture* texture) {
        return !texture || valid_region(*texture, auxiliary_region(*texture, request.albedo_region));
    };
    if (!auxiliary_is_valid(request.normal) || !auxiliary_is_valid(request.emissive) ||
        !auxiliary_is_valid(request.specular)) {
        result.status = MaterialPackStatus::Unsupported;
        return result;
    }

    result.width = width * 2;
    result.height = height * 2;
    const std::size_t pixel_count = static_cast<std::size_t>(result.width) * result.height;
    result.pixels.assign(pixel_count, Black);

    if (!copy_region(*request.albedo, request.albedo_region, result.pixels, result.width, 0, 0)) {
        result.pixels.clear(); result.width = 0; result.height = 0;
        return result;
    }

    const auto fill_or_copy = [&](Texture* texture, std::uint32_t fallback, int offset_x, int offset_y) {
        if (texture) {
            return copy_region(*texture, auxiliary_region(*texture, request.albedo_region),
                               result.pixels, result.width, offset_x, offset_y);
        }
        for (int y = 0; y < height; ++y) {
            const auto begin = result.pixels.begin() + static_cast<std::ptrdiff_t>(
                static_cast<std::size_t>(offset_y + y) * result.width + offset_x);
            std::fill_n(begin, width, fallback);
        }
        return true;
    };
    if (!fill_or_copy(request.normal, FlatNormal, width, 0) ||
        !fill_or_copy(request.emissive, Black, 0, height) ||
        !fill_or_copy(request.specular, Black, width, height)) {
        result.pixels.clear(); result.width = 0; result.height = 0;
        return result;
    }
    result.status = MaterialPackStatus::Ready;
    return result;
}

std::uint64_t MaterialPackBuilder::content_key(const MaterialPackRequest& request) {
    std::uint64_t hash = FnvOffset;
    hash_value(hash, Version);
    hash_value(hash, static_cast<std::uint64_t>(request.albedo_region.x));
    hash_value(hash, static_cast<std::uint64_t>(request.albedo_region.y));
    hash_value(hash, static_cast<std::uint64_t>(request.albedo_region.width));
    hash_value(hash, static_cast<std::uint64_t>(request.albedo_region.height));
    hash_texture_region(hash, request.albedo, request.albedo_region, 0);
    hash_texture_region(hash, request.normal, request.albedo_region, FlatNormal);
    hash_texture_region(hash, request.emissive, request.albedo_region, Black);
    hash_texture_region(hash, request.specular, request.albedo_region, Black);
    return hash;
}

MaterialPackResult MaterialPackCache::request(const MaterialPackRequest& request) {
    const std::uint64_t key = MaterialPackBuilder::content_key(request);
    if (const auto found = m_entries.find(key); found != m_entries.end()) {
        ++m_metrics.hits;
        found->second.last_used = ++m_use_counter;
        return {MaterialPackStatus::Ready, found->second.texture.get(), found->second.albedo_rect, key};
    }
    ++m_metrics.misses;
    const int expected_width = request.albedo_region.width * 2;
    const int expected_height = request.albedo_region.height * 2;
    if (request.albedo_region.width > m_max_dimension / 2 ||
        request.albedo_region.height > m_max_dimension / 2) {
        ++m_metrics.failures;
        return {MaterialPackStatus::Unsupported, nullptr, {}, key};
    }
    if (expected_width > 0 && expected_height > 0) {
        if (auto disk_texture = load_from_disk(key, expected_width, expected_height)) {
            const std::size_t byte_size = static_cast<std::size_t>(expected_width) * expected_height * sizeof(std::uint32_t);
            if (byte_size <= m_memory_budget) {
                evict_until_fits(byte_size);
                const SDL_FRect rect{0.0f, 0.0f,
                    static_cast<float>(request.albedo_region.width),
                    static_cast<float>(request.albedo_region.height)};
                Entry entry{std::move(disk_texture), rect, byte_size, ++m_use_counter};
                Texture* texture_ptr = entry.texture.get();
                m_metrics.resident_bytes += byte_size;
                m_entries.emplace(key, std::move(entry));
                m_metrics.resident_entries = m_entries.size();
                ++m_metrics.disk_hits;
                return {MaterialPackStatus::Ready, texture_ptr, rect, key};
            }
        }
    }
    MaterialPackPixels packed = MaterialPackBuilder::build(request, m_max_dimension);
    if (packed.status != MaterialPackStatus::Ready) {
        ++m_metrics.failures;
        return {packed.status, nullptr, {}, key};
    }
    const std::size_t byte_size = packed.pixels.size() * sizeof(std::uint32_t);
    if (byte_size > m_memory_budget) {
        ++m_metrics.failures;
        return {MaterialPackStatus::Unsupported, nullptr, {}, key};
    }
    auto texture = std::make_unique<Texture>(m_renderer, packed.width, packed.height, packed.pixels.data());
    if (!texture->get_sdl_texture()) {
        ++m_metrics.failures;
        return {MaterialPackStatus::Failed, nullptr, {}, key};
    }
    persist_to_disk(key, packed);
    const SDL_FRect rect{0.0f, 0.0f,
        static_cast<float>(request.albedo_region.width),
        static_cast<float>(request.albedo_region.height)};
    evict_until_fits(byte_size);
    Entry entry{std::move(texture), rect, byte_size, ++m_use_counter};
    Texture* texture_ptr = entry.texture.get();
    m_metrics.resident_bytes += entry.byte_size;
    m_entries.emplace(key, std::move(entry));
    m_metrics.resident_entries = m_entries.size();
    return {MaterialPackStatus::Ready, texture_ptr, rect, key};
}

void MaterialPackCache::clear() {
    m_entries.clear();
    m_metrics.resident_entries = 0;
    m_metrics.resident_bytes = 0;
}

void MaterialPackCache::set_max_dimension(int value) {
    const int clamped = std::max(value, 1);
    if (m_max_dimension == clamped) return;
    m_max_dimension = clamped;
    clear();
}

void MaterialPackCache::set_memory_budget(std::size_t bytes) {
    m_memory_budget = bytes;
    evict_until_fits(0);
    m_metrics.resident_entries = m_entries.size();
}

void MaterialPackCache::set_disk_cache_directory(std::filesystem::path directory) {
    m_disk_cache_directory = std::move(directory);
    if (m_disk_cache_directory.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(m_disk_cache_directory, error);
    if (error) m_disk_cache_directory.clear();
}

std::unique_ptr<Texture> MaterialPackCache::load_from_disk(
    std::uint64_t key,
    int expected_width,
    int expected_height
) {
    if (m_disk_cache_directory.empty()) return nullptr;
    const std::string name = key_text(key);
    std::ifstream manifest_stream(m_disk_cache_directory / (name + ".json"));
    if (!manifest_stream) return nullptr;
    nlohmann::json manifest;
    try {
        manifest_stream >> manifest;
        if (manifest.at("version").get<std::uint32_t>() != MaterialPackBuilder::Version ||
            manifest.at("key").get<std::string>() != name ||
            manifest.at("width").get<int>() != expected_width ||
            manifest.at("height").get<int>() != expected_height) return nullptr;
    } catch (const nlohmann::json::exception&) {
        return nullptr;
    }
    const std::size_t pixel_count = static_cast<std::size_t>(expected_width) * expected_height;
    std::vector<std::uint32_t> pixels(pixel_count);
    std::ifstream data_stream(m_disk_cache_directory / (name + ".rgba"), std::ios::binary);
    if (!data_stream.read(reinterpret_cast<char*>(pixels.data()),
                          static_cast<std::streamsize>(pixels.size() * sizeof(std::uint32_t)))) return nullptr;
    char extra = 0;
    if (data_stream.read(&extra, 1)) return nullptr;
    try {
        if (manifest.at("checksum").get<std::string>() != key_text(hash_pixels(pixels))) return nullptr;
    } catch (const nlohmann::json::exception&) {
        return nullptr;
    }
    auto texture = std::make_unique<Texture>(m_renderer, expected_width, expected_height, pixels.data());
    return texture->get_sdl_texture() ? std::move(texture) : nullptr;
}

void MaterialPackCache::persist_to_disk(std::uint64_t key, const MaterialPackPixels& packed) {
    if (m_disk_cache_directory.empty() || packed.status != MaterialPackStatus::Ready) return;
    const std::string name = key_text(key);
    const auto data_path = m_disk_cache_directory / (name + ".rgba");
    const auto manifest_path = m_disk_cache_directory / (name + ".json");
    const auto data_temporary = m_disk_cache_directory / (name + ".rgba.tmp");
    const auto manifest_temporary = m_disk_cache_directory / (name + ".json.tmp");
    {
        std::ofstream stream(data_temporary, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(packed.pixels.data()),
                     static_cast<std::streamsize>(packed.pixels.size() * sizeof(std::uint32_t)));
        if (!stream) return;
    }
    const nlohmann::json manifest{
        {"version", MaterialPackBuilder::Version}, {"key", name},
        {"width", packed.width}, {"height", packed.height},
        {"checksum", key_text(hash_pixels(packed.pixels))}
    };
    {
        std::ofstream stream(manifest_temporary, std::ios::trunc);
        stream << manifest.dump(2);
        if (!stream) {
            std::error_code ignored;
            std::filesystem::remove(data_temporary, ignored);
            return;
        }
    }
    std::error_code error;
    std::filesystem::rename(data_temporary, data_path, error);
    if (error) {
        std::filesystem::remove(data_path, error);
        error.clear();
        std::filesystem::rename(data_temporary, data_path, error);
    }
    if (error) return;
    std::filesystem::rename(manifest_temporary, manifest_path, error);
    if (error) {
        std::filesystem::remove(manifest_path, error);
        error.clear();
        std::filesystem::rename(manifest_temporary, manifest_path, error);
    }
    if (!error) ++m_metrics.disk_writes;
}

void MaterialPackCache::evict_until_fits(std::size_t byte_size) {
    while (m_metrics.resident_bytes + byte_size > m_memory_budget && !m_entries.empty()) {
        const auto oldest = std::min_element(m_entries.begin(), m_entries.end(), [](const auto& left, const auto& right) {
            return left.second.last_used < right.second.last_used;
        });
        m_metrics.resident_bytes -= oldest->second.byte_size;
        m_entries.erase(oldest);
        ++m_metrics.evictions;
    }
    m_metrics.resident_entries = m_entries.size();
}

} // namespace Engine
