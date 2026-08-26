#include "ShadowSilhouette.hpp"

#include "Texture.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>

namespace Engine {
namespace {

struct GridPoint {
    int x{0};
    int y{0};
    bool operator==(const GridPoint&) const = default;
};

std::uint64_t point_key(GridPoint point) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(point.x)) << 32) |
        static_cast<std::uint32_t>(point.y);
}

std::uint64_t edge_key(GridPoint from, GridPoint to) {
    std::uint64_t value = 1469598103934665603ull;
    for (const std::uint64_t part : {point_key(from), point_key(to)}) {
        value ^= part;
        value *= 1099511628211ull;
    }
    return value;
}

float polygon_area(const std::vector<GridPoint>& polygon) {
    float area = 0.0f;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const GridPoint a = polygon[index];
        const GridPoint b = polygon[(index + 1) % polygon.size()];
        area += static_cast<float>(a.x * b.y - b.x * a.y);
    }
    return area * 0.5f;
}

float point_line_distance(glm::vec2 point, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 edge = b - a;
    const float length_squared = glm::dot(edge, edge);
    if (length_squared <= 0.000001f) return glm::length(point - a);
    const float amount = std::clamp(glm::dot(point - a, edge) / length_squared, 0.0f, 1.0f);
    return glm::length(point - (a + edge * amount));
}

void simplify_closed(std::vector<glm::vec2>& points, float tolerance) {
    if (points.size() <= 3 || tolerance <= 0.0f) return;
    bool changed = true;
    while (changed && points.size() > 3) {
        changed = false;
        for (std::size_t index = 0; index < points.size() && points.size() > 3;) {
            const std::size_t previous = (index + points.size() - 1) % points.size();
            const std::size_t next = (index + 1) % points.size();
            if (point_line_distance(points[index], points[previous], points[next]) <= tolerance) {
                points.erase(points.begin() + static_cast<std::ptrdiff_t>(index));
                changed = true;
            } else {
                ++index;
            }
        }
    }
}

std::uint64_t cache_key(
    const Texture& texture,
    const SDL_FRect& source,
    float alpha_threshold,
    float tolerance_pixels
) {
    std::uint64_t value = texture.content_hash();
    const int fields[]{
        static_cast<int>(std::floor(source.x)), static_cast<int>(std::floor(source.y)),
        static_cast<int>(std::ceil(source.w)), static_cast<int>(std::ceil(source.h)),
        static_cast<int>(std::round(std::clamp(alpha_threshold, 0.0f, 1.0f) * 255.0f)),
        static_cast<int>(std::round(std::max(0.0f, tolerance_pixels) * 16.0f))
    };
    for (const int field : fields) {
        value ^= static_cast<std::uint32_t>(field);
        value *= 1099511628211ull;
    }
    return value;
}

} // namespace

std::vector<glm::vec2> extract_alpha_silhouette(
    std::span<const std::uint8_t> alpha,
    int texture_width,
    int texture_height,
    const SDL_FRect& source,
    float alpha_threshold,
    float tolerance_pixels
) {
    if (texture_width <= 0 || texture_height <= 0 ||
        alpha.size() < static_cast<std::size_t>(texture_width) * texture_height) return {};
    const int left = std::clamp(static_cast<int>(std::floor(source.x)), 0, texture_width - 1);
    const int top = std::clamp(static_cast<int>(std::floor(source.y)), 0, texture_height - 1);
    const int width = std::clamp(static_cast<int>(std::ceil(source.w)), 1, texture_width - left);
    const int height = std::clamp(static_cast<int>(std::ceil(source.h)), 1, texture_height - top);
    const std::uint8_t threshold = static_cast<std::uint8_t>(
        std::round(std::clamp(alpha_threshold, 0.0f, 1.0f) * 255.0f)
    );
    const auto opaque = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height &&
            alpha[static_cast<std::size_t>(top + y) * texture_width + left + x] > threshold;
    };

    std::map<std::uint64_t, std::vector<GridPoint>> outgoing;
    std::size_t edge_count = 0;
    const auto add_edge = [&](GridPoint from, GridPoint to) {
        outgoing[point_key(from)].push_back(to);
        ++edge_count;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!opaque(x, y)) continue;
            if (!opaque(x, y - 1)) add_edge({x, y}, {x + 1, y});
            if (!opaque(x + 1, y)) add_edge({x + 1, y}, {x + 1, y + 1});
            if (!opaque(x, y + 1)) add_edge({x + 1, y + 1}, {x, y + 1});
            if (!opaque(x - 1, y)) add_edge({x, y + 1}, {x, y});
        }
    }
    if (outgoing.empty()) return {};
    for (auto& [_, targets] : outgoing) {
        std::sort(targets.begin(), targets.end(), [](GridPoint left, GridPoint right) {
            return left.x != right.x ? left.x < right.x : left.y < right.y;
        });
    }

    std::unordered_set<std::uint64_t> used;
    std::vector<GridPoint> largest;
    for (const auto& [from_key, targets] : outgoing) {
        GridPoint start{static_cast<int>(from_key >> 32), static_cast<int>(from_key)};
        for (const GridPoint initial_to : targets) {
            if (used.contains(edge_key(start, initial_to))) continue;
            std::vector<GridPoint> loop{start};
            GridPoint from = start;
            GridPoint to = initial_to;
            for (std::size_t guard = 0; guard <= edge_count; ++guard) {
                const std::uint64_t current_edge = edge_key(from, to);
                if (!used.insert(current_edge).second) break;
                loop.push_back(to);
                if (to == start) break;
                const auto next = outgoing.find(point_key(to));
                bool found = false;
                if (next != outgoing.end()) {
                    for (const GridPoint candidate : next->second) {
                        if (!used.contains(edge_key(to, candidate))) {
                            from = to;
                            to = candidate;
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) break;
            }
            if (loop.size() >= 4 && loop.back() == loop.front()) loop.pop_back();
            if (loop.size() >= 3 && std::abs(polygon_area(loop)) > std::abs(polygon_area(largest))) {
                largest = std::move(loop);
            }
        }
    }
    std::vector<glm::vec2> result;
    result.reserve(largest.size());
    for (const GridPoint point : largest) {
        result.emplace_back(static_cast<float>(point.x) / width, static_cast<float>(point.y) / height);
    }
    simplify_closed(result, std::max(0.0f, tolerance_pixels) / std::max(width, height));
    return result;
}

std::vector<glm::vec2> extract_alpha_silhouette(
    const Texture& texture,
    const SDL_FRect& source,
    float alpha_threshold,
    float tolerance_pixels
) {
    std::vector<std::uint8_t> alpha(
        static_cast<std::size_t>(texture.get_width()) * texture.get_height()
    );
    for (int y = 0; y < texture.get_height(); ++y) {
        for (int x = 0; x < texture.get_width(); ++x) {
            alpha[static_cast<std::size_t>(y) * texture.get_width() + x] = texture.alpha_at(x, y);
        }
    }
    return extract_alpha_silhouette(
        alpha, texture.get_width(), texture.get_height(), source,
        alpha_threshold, tolerance_pixels
    );
}

const std::vector<glm::vec2>& ShadowSilhouetteCache::get(
    const Texture& texture,
    const SDL_FRect& source,
    float alpha_threshold,
    float tolerance_pixels
) {
    const std::uint64_t key = cache_key(texture, source, alpha_threshold, tolerance_pixels);
    auto [found, inserted] = m_entries.try_emplace(key);
    if (inserted) {
        found->second.vertices = extract_alpha_silhouette(
            texture, source, alpha_threshold, tolerance_pixels
        );
    }
    found->second.last_used_frame = m_frame;
    return found->second.vertices;
}

void ShadowSilhouetteCache::begin_frame() {
    ++m_frame;
    if (m_entries.size() <= 256) return;
    for (auto entry = m_entries.begin(); entry != m_entries.end();) {
        if (entry->second.last_used_frame + 120 < m_frame) entry = m_entries.erase(entry);
        else ++entry;
    }
}

} // namespace Engine
