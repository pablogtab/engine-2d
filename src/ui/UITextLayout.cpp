#include "UITextLayout.hpp"

#include "../core/AssetDatabase.hpp"
#include "../renderer/Font.hpp"

#include <stb/stb_truetype.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {
namespace {

constexpr float k_max_fitted_size = 100000.0f;

struct CachedFontMetrics {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    bool valid{false};
};

struct MeasuredText {
    float width{0.0f};
    std::size_t line_count{0};
};

CachedFontMetrics* metrics_for_path(const std::string& path) {
    static std::unordered_map<std::string, std::unique_ptr<CachedFontMetrics>> cache;
    if (path.empty()) return nullptr;
    if (const auto found = cache.find(path); found != cache.end()) {
        return found->second->valid ? found->second.get() : nullptr;
    }

    auto metrics = std::make_unique<CachedFontMetrics>();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size > 0) {
            metrics->bytes.resize(static_cast<std::size_t>(size));
            if (file.read(
                    reinterpret_cast<char*>(metrics->bytes.data()), size
                )) {
                const int offset = stbtt_GetFontOffsetForIndex(metrics->bytes.data(), 0);
                metrics->valid = offset >= 0 && stbtt_InitFont(
                    &metrics->info, metrics->bytes.data(), offset
                );
            }
        }
    }
    CachedFontMetrics* result = metrics->valid ? metrics.get() : nullptr;
    cache.emplace(path, std::move(metrics));
    return result;
}

CachedFontMetrics* resolve_metrics(
    const UITextComponent& text,
    const AssetDatabase* asset_database
) {
    std::string path;
    if (text.font_handle.is_valid() && asset_database) {
        path = asset_database->resolve(text.font_handle);
    }
    if (path.empty()) {
        const auto fallback = Font::find_default_font_path();
        if (fallback) path = *fallback;
    }
    return metrics_for_path(path);
}

float character_advance(
    CachedFontMetrics* metrics,
    unsigned char character,
    float font_size
) {
    if (!metrics) return font_size * 0.5f;
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&metrics->info, character, &advance, &bearing);
    static_cast<void>(bearing);
    return static_cast<float>(advance) *
        stbtt_ScaleForPixelHeight(&metrics->info, font_size);
}

MeasuredText measure_text(
    const UITextComponent& text,
    CachedFontMetrics* metrics,
    float maximum_width,
    bool wrap
) {
    if (text.text.empty()) return {};
    MeasuredText result;
    result.line_count = 1;
    float line_width = 0.0f;
    for (const unsigned char character : text.text) {
        if (character == '\n') {
            result.width = std::max(result.width, line_width);
            line_width = 0.0f;
            ++result.line_count;
            continue;
        }
        const float advance = character_advance(metrics, character, text.font_size);
        if (wrap && maximum_width > 0.0f && line_width > 0.0f &&
            line_width + advance > maximum_width) {
            result.width = std::max(result.width, line_width);
            line_width = 0.0f;
            ++result.line_count;
        }
        line_width += advance;
    }
    result.width = std::max(result.width, line_width);
    return result;
}

float pixel_part(const UILength& length) {
    return length.unit == UILengthUnit::Pixels ? length.value : 0.0f;
}

float percentage_part(const UILength& length) {
    return length.unit == UILengthUnit::Percent ? length.value * 0.01f : 0.0f;
}

float solve_fitted_axis(float content, float fixed_insets, float percentage_insets) {
    const float denominator = std::max(1.0f - percentage_insets, 0.001f);
    return std::clamp((content + fixed_insets) / denominator, 0.0f, k_max_fitted_size);
}

} // namespace

float resolve_ui_length(const UILength& length, float reference_size) {
    return length.unit == UILengthUnit::Percent
        ? reference_size * length.value * 0.01f
        : length.value;
}

glm::vec4 resolve_ui_edge_lengths(
    const UIEdgeLengths& edges,
    const glm::vec2& rect_size
) {
    return {
        resolve_ui_length(edges.left, rect_size.x),
        resolve_ui_length(edges.top, rect_size.y),
        resolve_ui_length(edges.right, rect_size.x),
        resolve_ui_length(edges.bottom, rect_size.y)
    };
}

UIRect calculate_ui_text_content_rect(
    const UIRect& screen_rect,
    const UITextComponent& text,
    const glm::vec2& canvas_scale
) {
    const auto resolve_screen_length = [](const UILength& length, float reference, float scale) {
        return length.unit == UILengthUnit::Percent
            ? reference * length.value * 0.01f
            : length.value * scale;
    };
    const glm::vec4 margins{
        resolve_screen_length(text.margins.left, screen_rect.size.x, canvas_scale.x),
        resolve_screen_length(text.margins.top, screen_rect.size.y, canvas_scale.y),
        resolve_screen_length(text.margins.right, screen_rect.size.x, canvas_scale.x),
        resolve_screen_length(text.margins.bottom, screen_rect.size.y, canvas_scale.y)
    };
    const glm::vec4 padding{
        text.fit_padding.x * canvas_scale.x,
        text.fit_padding.y * canvas_scale.y,
        text.fit_padding.z * canvas_scale.x,
        text.fit_padding.w * canvas_scale.y
    };
    const glm::vec2 minimum{margins.x + padding.x, margins.y + padding.y};
    const glm::vec2 maximum{margins.z + padding.z, margins.w + padding.w};
    return {
        screen_rect.position + minimum,
        glm::max(screen_rect.size - minimum - maximum, glm::vec2(0.0f))
    };
}

glm::vec2 calculate_ui_text_fitted_size(
    const UITextComponent& text,
    const glm::vec2& authored_size,
    const AssetDatabase* asset_database
) {
    glm::vec2 result = authored_size;
    if (!text.fit_content_width && !text.fit_content_height) return result;

    CachedFontMetrics* metrics = resolve_metrics(text, asset_database);
    const MeasuredText unwrapped = measure_text(text, metrics, 0.0f, false);
    if (text.fit_content_width) {
        const float fixed = text.fit_padding.x + text.fit_padding.z +
            pixel_part(text.margins.left) + pixel_part(text.margins.right);
        const float percent = percentage_part(text.margins.left) +
            percentage_part(text.margins.right);
        result.x = solve_fitted_axis(unwrapped.width, fixed, percent);
    }

    const glm::vec4 resolved_margins = resolve_ui_edge_lengths(text.margins, result);
    const float available_width = std::max(
        result.x - text.fit_padding.x - text.fit_padding.z -
            resolved_margins.x - resolved_margins.z,
        0.0f
    );
    const MeasuredText wrapped = measure_text(
        text,
        metrics,
        available_width,
        text.wrap && !text.fit_content_width
    );
    if (text.fit_content_height) {
        const float text_height = static_cast<float>(wrapped.line_count) *
            text.font_size * 1.25f;
        const float fixed = text.fit_padding.y + text.fit_padding.w +
            pixel_part(text.margins.top) + pixel_part(text.margins.bottom);
        const float percent = percentage_part(text.margins.top) +
            percentage_part(text.margins.bottom);
        result.y = solve_fitted_axis(text_height, fixed, percent);
    }
    return glm::max(result, glm::vec2(0.0f));
}

} // namespace Engine
