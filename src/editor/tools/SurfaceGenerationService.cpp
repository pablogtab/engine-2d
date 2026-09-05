#include "SurfaceGenerationService.hpp"

#include "stb/stb_image.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Engine {

namespace {

struct SourceImage {
    int width{0};
    int height{0};
    float weight{1.0f};
    bool allow_flip_x{true};
    bool allow_flip_y{true};
    bool allow_quarter_turns{false};
    std::vector<std::uint8_t> rgba;
};

struct Candidate {
    int source{0};
    int source_x{0};
    int source_y{0};
    int rotation{0};
    bool flip_x{false};
    bool flip_y{false};
    float luminance_delta{0.0f};
    float overlap_error{0.0f};
    float score{0.0f};
};

struct Placement {
    int output_x{0};
    int output_y{0};
    int source{0};
    int source_x{0};
    int source_y{0};
};

std::size_t pixel_offset(int width, int x, int y) {
    return (static_cast<std::size_t>(y) * width + x) * 4;
}

float luminance(const std::uint8_t* pixel) {
    return (0.2126f * pixel[0] + 0.7152f * pixel[1] + 0.0722f * pixel[2]) / 255.0f;
}

std::array<std::uint8_t, 4> candidate_pixel(
    const SourceImage& source,
    const Candidate& candidate,
    int patch_size,
    int x,
    int y
) {
    if (candidate.flip_x) x = patch_size - 1 - x;
    if (candidate.flip_y) y = patch_size - 1 - y;

    int sample_x = x;
    int sample_y = y;
    switch (candidate.rotation & 3) {
        case 1: sample_x = y; sample_y = patch_size - 1 - x; break;
        case 2: sample_x = patch_size - 1 - x; sample_y = patch_size - 1 - y; break;
        case 3: sample_x = patch_size - 1 - y; sample_y = x; break;
        default: break;
    }

    const std::size_t offset = pixel_offset(
        source.width,
        candidate.source_x + sample_x,
        candidate.source_y + sample_y
    );
    std::array<std::uint8_t, 4> result{};
    for (int channel = 0; channel < 3; ++channel) {
        const float adjusted = static_cast<float>(source.rgba[offset + channel]) +
            candidate.luminance_delta * 255.0f;
        result[channel] = static_cast<std::uint8_t>(std::clamp(adjusted, 0.0f, 255.0f));
    }
    result[3] = source.rgba[offset + 3];
    return result;
}

float color_error(const std::uint8_t* existing, const std::array<std::uint8_t, 4>& candidate) {
    float error = 0.0f;
    for (int channel = 0; channel < 3; ++channel) {
        const float delta = (static_cast<float>(existing[channel]) - candidate[channel]) / 255.0f;
        error += delta * delta;
    }
    return error / 3.0f;
}

bool load_sources(
    const SurfaceGenerationSettings& settings,
    std::vector<SourceImage>& sources,
    std::string& error
) {
    if (!SurfaceGenerationService::validate_settings(settings, error)) return false;

    float total_weight = 0.0f;
    for (const SurfaceSourceSpec& spec : settings.sources) {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load(spec.path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) {
            error = "Could not load source texture: " + spec.path.string();
            return false;
        }
        if (width < settings.patch_size || height < settings.patch_size) {
            stbi_image_free(pixels);
            error = "Source texture is smaller than the configured patch: " + spec.path.string();
            return false;
        }
        SourceImage source;
        source.width = width;
        source.height = height;
        source.weight = spec.weight;
        source.allow_flip_x = spec.allow_flip_x;
        source.allow_flip_y = spec.allow_flip_y;
        source.allow_quarter_turns = spec.allow_quarter_turns;
        source.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(pixels);
        sources.push_back(std::move(source));
        total_weight += spec.weight;
    }
    return total_weight > 0.0f;
}

bool validate_parameters(const SurfaceGenerationSettings& settings, std::string& error) {
    if (settings.sources.empty() || settings.sources.size() > 6) {
        error = "Surface generation requires between one and six source textures";
        return false;
    }
    if (settings.width < 32 || settings.height < 32 ||
        settings.width > 8192 || settings.height > 8192) {
        error = "Output dimensions must be between 32 and 8192 pixels";
        return false;
    }
    if (settings.patch_size < 16 || settings.patch_size > 1024) {
        error = "Patch size must be between 16 and 1024 pixels";
        return false;
    }
    if (settings.overlap < 1 || settings.overlap >= settings.patch_size) {
        error = "Overlap must be positive and smaller than the patch size";
        return false;
    }
    if (settings.candidate_count < 1 || settings.candidate_count > 256) {
        error = "Candidate count must be between 1 and 256";
        return false;
    }

    if (settings.repetition_radius < 0 || settings.repetition_radius > 8 ||
        !std::isfinite(settings.luminance_normalization) ||
        settings.luminance_normalization < 0.0f || settings.luminance_normalization > 1.0f) {
        error = "Repeat radius must be 0..8 and luminance match must be finite and 0..1";
        return false;
    }
    double total_weight = 0.0;
    for (const SurfaceSourceSpec& spec : settings.sources) {
        if (!std::isfinite(spec.weight) || spec.weight <= 0.0f || spec.path.empty()) {
            error = "Every source texture must have a path and a finite positive weight";
            return false;
        }
        total_weight += spec.weight;
    }
    if (total_weight > std::numeric_limits<float>::max()) {
        error = "Source weights exceed the supported range";
        return false;
    }
    return true;
}

int choose_source(const std::vector<SourceImage>& sources, std::mt19937& random) {
    float total = 0.0f;
    for (const SourceImage& source : sources) total += source.weight;
    std::uniform_real_distribution<float> distribution(0.0f, total);
    const float pick = distribution(random);
    float accumulated = 0.0f;
    for (std::size_t index = 0; index < sources.size(); ++index) {
        accumulated += sources[index].weight;
        if (pick <= accumulated) return static_cast<int>(index);
    }
    return static_cast<int>(sources.size() - 1);
}

Candidate sample_candidate(
    const std::vector<SourceImage>& sources,
    const SurfaceGenerationSettings& settings,
    std::mt19937& random
) {
    Candidate candidate;
    candidate.source = choose_source(sources, random);
    const SourceImage& source = sources[candidate.source];
    std::uniform_int_distribution<int> x_distribution(0, source.width - settings.patch_size);
    std::uniform_int_distribution<int> y_distribution(0, source.height - settings.patch_size);
    candidate.source_x = x_distribution(random);
    candidate.source_y = y_distribution(random);
    candidate.flip_x = source.allow_flip_x && ((random() & 1u) != 0u);
    candidate.flip_y = source.allow_flip_y && ((random() & 1u) != 0u);
    candidate.rotation = source.allow_quarter_turns ? static_cast<int>(random() % 4u) : 0;
    return candidate;
}

void evaluate_candidate(
    Candidate& candidate,
    const SourceImage& source,
    const SurfaceGenerationSettings& settings,
    const std::vector<std::uint8_t>& output,
    int output_x,
    int output_y,
    int patch_width,
    int patch_height,
    int left_overlap,
    int top_overlap,
    const std::vector<Placement>& placements
) {
    double existing_luminance = 0.0;
    double source_luminance = 0.0;
    std::size_t overlap_pixels = 0;
    for (int y = 0; y < patch_height; ++y) {
        for (int x = 0; x < patch_width; ++x) {
            if (x >= left_overlap && y >= top_overlap) continue;
            const auto sample = candidate_pixel(source, candidate, settings.patch_size, x, y);
            const std::size_t output_offset = pixel_offset(settings.width, output_x + x, output_y + y);
            existing_luminance += luminance(output.data() + output_offset);
            source_luminance += luminance(sample.data());
            ++overlap_pixels;
        }
    }
    if (overlap_pixels > 0) {
        candidate.luminance_delta = static_cast<float>(
            (existing_luminance - source_luminance) / overlap_pixels
        ) * std::clamp(settings.luminance_normalization, 0.0f, 1.0f);
    }

    double error_sum = 0.0;
    for (int y = 0; y < patch_height; ++y) {
        for (int x = 0; x < patch_width; ++x) {
            if (x >= left_overlap && y >= top_overlap) continue;
            const auto sample = candidate_pixel(source, candidate, settings.patch_size, x, y);
            const std::size_t output_offset = pixel_offset(settings.width, output_x + x, output_y + y);
            error_sum += color_error(output.data() + output_offset, sample);
        }
    }
    candidate.overlap_error = overlap_pixels > 0
        ? static_cast<float>(error_sum / overlap_pixels)
        : 0.0f;
    candidate.score = candidate.overlap_error;

    if (settings.repetition_radius <= 0) return;
    const float output_limit = static_cast<float>(
        settings.repetition_radius * (settings.patch_size - settings.overlap)
    );
    for (const Placement& placement : placements) {
        if (placement.source != candidate.source) continue;
        const float output_distance = std::hypot(
            static_cast<float>(placement.output_x - output_x),
            static_cast<float>(placement.output_y - output_y)
        );
        if (output_distance > output_limit) continue;
        const float source_distance = std::hypot(
            static_cast<float>(placement.source_x - candidate.source_x),
            static_cast<float>(placement.source_y - candidate.source_y)
        );
        if (source_distance < settings.patch_size) {
            candidate.score += 0.15f * (1.0f - source_distance / settings.patch_size);
        }
    }
}

std::vector<int> vertical_seam(
    const Candidate& candidate,
    const SourceImage& source,
    const SurfaceGenerationSettings& settings,
    const std::vector<std::uint8_t>& output,
    int output_x,
    int output_y,
    int overlap,
    int height
) {
    if (overlap <= 0) return {};
    std::vector<float> costs(static_cast<std::size_t>(overlap) * height);
    std::vector<int> parents(costs.size(), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < overlap; ++x) {
            const auto sample = candidate_pixel(source, candidate, settings.patch_size, x, y);
            const std::size_t output_offset = pixel_offset(settings.width, output_x + x, output_y + y);
            const float local = color_error(output.data() + output_offset, sample);
            const std::size_t index = static_cast<std::size_t>(y) * overlap + x;
            costs[index] = local;
            if (y == 0) continue;
            int best_x = x;
            float best = costs[static_cast<std::size_t>(y - 1) * overlap + x];
            for (int previous_x = std::max(0, x - 1); previous_x <= std::min(overlap - 1, x + 1); ++previous_x) {
                const float previous = costs[static_cast<std::size_t>(y - 1) * overlap + previous_x];
                if (previous < best) { best = previous; best_x = previous_x; }
            }
            costs[index] += best;
            parents[index] = best_x;
        }
    }
    int best_x = 0;
    for (int x = 1; x < overlap; ++x) {
        if (costs[static_cast<std::size_t>(height - 1) * overlap + x] <
            costs[static_cast<std::size_t>(height - 1) * overlap + best_x]) best_x = x;
    }
    std::vector<int> seam(height, 0);
    for (int y = height - 1; y >= 0; --y) {
        seam[y] = best_x;
        best_x = parents[static_cast<std::size_t>(y) * overlap + best_x];
    }
    return seam;
}

std::vector<int> horizontal_seam(
    const Candidate& candidate,
    const SourceImage& source,
    const SurfaceGenerationSettings& settings,
    const std::vector<std::uint8_t>& output,
    int output_x,
    int output_y,
    int overlap,
    int width
) {
    if (overlap <= 0) return {};
    std::vector<float> costs(static_cast<std::size_t>(overlap) * width);
    std::vector<int> parents(costs.size(), 0);
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < overlap; ++y) {
            const auto sample = candidate_pixel(source, candidate, settings.patch_size, x, y);
            const std::size_t output_offset = pixel_offset(settings.width, output_x + x, output_y + y);
            const float local = color_error(output.data() + output_offset, sample);
            const std::size_t index = static_cast<std::size_t>(x) * overlap + y;
            costs[index] = local;
            if (x == 0) continue;
            int best_y = y;
            float best = costs[static_cast<std::size_t>(x - 1) * overlap + y];
            for (int previous_y = std::max(0, y - 1); previous_y <= std::min(overlap - 1, y + 1); ++previous_y) {
                const float previous = costs[static_cast<std::size_t>(x - 1) * overlap + previous_y];
                if (previous < best) { best = previous; best_y = previous_y; }
            }
            costs[index] += best;
            parents[index] = best_y;
        }
    }
    int best_y = 0;
    for (int y = 1; y < overlap; ++y) {
        if (costs[static_cast<std::size_t>(width - 1) * overlap + y] <
            costs[static_cast<std::size_t>(width - 1) * overlap + best_y]) best_y = y;
    }
    std::vector<int> seam(width, 0);
    for (int x = width - 1; x >= 0; --x) {
        seam[x] = best_y;
        best_y = parents[static_cast<std::size_t>(x) * overlap + best_y];
    }
    return seam;
}

float correlation_for_shift(
    const std::vector<std::uint8_t>& rgba,
    int width,
    int height,
    int shift_x,
    int shift_y
) {
    const int sample_step = std::max(1, std::min(width, height) / 192);
    double sum_a = 0.0;
    double sum_b = 0.0;
    double sum_aa = 0.0;
    double sum_bb = 0.0;
    double sum_ab = 0.0;
    std::size_t count = 0;
    for (int y = 0; y + shift_y < height; y += sample_step) {
        for (int x = 0; x + shift_x < width; x += sample_step) {
            const float a = luminance(rgba.data() + pixel_offset(width, x, y));
            const float b = luminance(rgba.data() + pixel_offset(width, x + shift_x, y + shift_y));
            sum_a += a; sum_b += b; sum_aa += a * a; sum_bb += b * b; sum_ab += a * b;
            ++count;
        }
    }
    if (count < 2) return 0.0f;
    const double numerator = count * sum_ab - sum_a * sum_b;
    const double denominator = std::sqrt(
        std::max(0.0, count * sum_aa - sum_a * sum_a) *
        std::max(0.0, count * sum_bb - sum_b * sum_b)
    );
    return denominator > 1.0e-9 ? static_cast<float>(numerator / denominator) : 0.0f;
}

float repetition_score(const std::vector<std::uint8_t>& rgba, int width, int height, int stride) {
    float result = 0.0f;
    for (int multiplier = 1; multiplier <= 4; ++multiplier) {
        const int shift = stride * multiplier;
        if (shift < width) result = std::max(result, correlation_for_shift(rgba, width, height, shift, 0));
        if (shift < height) result = std::max(result, correlation_for_shift(rgba, width, height, 0, shift));
    }
    return std::clamp(result, 0.0f, 1.0f);
}

std::uint64_t content_hash(const std::vector<std::uint8_t>& rgba) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::uint8_t byte : rgba) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

bool SurfaceGenerationService::validate_settings(
    const SurfaceGenerationSettings& settings, std::string& error
) {
    error.clear();
    return validate_parameters(settings, error);
}

SurfaceGenerationResult SurfaceGenerationService::generate(
    const SurfaceGenerationSettings& settings,
    SurfaceGenerationControl* control
) {
    SurfaceGenerationResult result;
    result.width = settings.width;
    result.height = settings.height;
    if (control) control->progress.store(0.0f);

    std::vector<SourceImage> sources;
    if (!load_sources(settings, sources, result.error)) return result;

    const int stride = settings.patch_size - settings.overlap;
    const int columns = (settings.width + stride - 1) / stride;
    const int rows = (settings.height + stride - 1) / stride;
    const int total_patches = columns * rows;
    result.rgba.assign(static_cast<std::size_t>(settings.width) * settings.height * 4, 0);
    result.metrics.source_usage.assign(sources.size(), 0.0f);

    std::mt19937 random(settings.seed);
    std::vector<Placement> placements;
    placements.reserve(total_patches);
    double total_overlap_error = 0.0;
    int overlap_placements = 0;
    int completed = 0;

    for (int output_y = 0; output_y < settings.height; output_y += stride) {
        for (int output_x = 0; output_x < settings.width; output_x += stride) {
            if (control && control->cancel.load()) {
                result.cancelled = true;
                result.error = "Surface generation cancelled";
                result.rgba.clear();
                return result;
            }
            const int patch_width = std::min(settings.patch_size, settings.width - output_x);
            const int patch_height = std::min(settings.patch_size, settings.height - output_y);
            const int left_overlap = output_x > 0 ? std::min(settings.overlap, patch_width) : 0;
            const int top_overlap = output_y > 0 ? std::min(settings.overlap, patch_height) : 0;

            std::vector<Candidate> candidates;
            candidates.reserve(settings.candidate_count);
            for (int index = 0; index < settings.candidate_count; ++index) {
                if (control && control->cancel.load()) {
                    result.cancelled = true;
                    result.rgba.clear();
                    return result;
                }
                Candidate candidate = sample_candidate(sources, settings, random);
                evaluate_candidate(
                    candidate, sources[candidate.source], settings, result.rgba,
                    output_x, output_y, patch_width, patch_height,
                    left_overlap, top_overlap, placements
                );
                candidates.push_back(candidate);
            }
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
                return left.score < right.score;
            });
            const float best_score = candidates.front().score;
            const float tolerance = std::max(0.0025f, best_score * 0.15f);
            std::size_t eligible = 1;
            while (eligible < candidates.size() && eligible < 8 &&
                   candidates[eligible].score <= best_score + tolerance) ++eligible;
            std::uniform_int_distribution<std::size_t> choice_distribution(0, eligible - 1);
            const Candidate chosen = candidates[choice_distribution(random)];
            const SourceImage& source = sources[chosen.source];

            const std::vector<int> left_seam = vertical_seam(
                chosen, source, settings, result.rgba,
                output_x, output_y, left_overlap, patch_height
            );
            const std::vector<int> top_seam = horizontal_seam(
                chosen, source, settings, result.rgba,
                output_x, output_y, top_overlap, patch_width
            );

            for (int y = 0; y < patch_height; ++y) {
                for (int x = 0; x < patch_width; ++x) {
                    bool use_candidate = true;
                    if (left_overlap > 0 && x < left_overlap) use_candidate = x >= left_seam[y];
                    if (top_overlap > 0 && y < top_overlap) {
                        use_candidate = use_candidate && y >= top_seam[x];
                    }
                    if (!use_candidate) continue;
                    const auto sample = candidate_pixel(source, chosen, settings.patch_size, x, y);
                    const std::size_t offset = pixel_offset(settings.width, output_x + x, output_y + y);
                    std::copy(sample.begin(), sample.end(), result.rgba.begin() + static_cast<std::ptrdiff_t>(offset));
                }
            }

            placements.push_back({output_x, output_y, chosen.source, chosen.source_x, chosen.source_y});
            result.metrics.source_usage[chosen.source] += 1.0f;
            if (left_overlap > 0 || top_overlap > 0) {
                total_overlap_error += chosen.overlap_error;
                ++overlap_placements;
            }
            ++completed;
            if (control) control->progress.store(static_cast<float>(completed) / total_patches);
        }
    }

    for (float& usage : result.metrics.source_usage) usage /= std::max(1, total_patches);
    result.metrics.average_overlap_error = overlap_placements > 0
        ? static_cast<float>(total_overlap_error / overlap_placements)
        : 0.0f;
    result.metrics.repetition_score = repetition_score(result.rgba, settings.width, settings.height, stride);
    if (result.metrics.repetition_score > 0.85f) {
        result.warnings.push_back("High periodic correlation detected; inspect the preview for repetition");
    }
    if (result.metrics.average_overlap_error > 0.035f) {
        result.warnings.push_back("Source textures have difficult seams; increase overlap or add compatible variants");
    }
    result.content_hash = content_hash(result.rgba);
    result.success = true;
    if (control) control->progress.store(1.0f);
    return result;
}

bool SurfaceGenerationService::save_png_atomic(
    const std::filesystem::path& output_path,
    const SurfaceGenerationResult& result,
    std::string& error
) {
    error.clear();
    if (!result.success || result.width <= 0 || result.height <= 0 ||
        result.rgba.size() != static_cast<std::size_t>(result.width) * result.height * 4) {
        error = "Surface result is incomplete";
        return false;
    }
    if (output_path.extension() != ".png") {
        error = "Surface output must use the .png extension";
        return false;
    }

    std::error_code filesystem_error;
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
    }
    if (filesystem_error) {
        error = "Could not create the output directory";
        return false;
    }

    SDL_Surface* surface = SDL_CreateSurface(result.width, result.height, SDL_PIXELFORMAT_RGBA32);
    if (!surface) {
        error = SDL_GetError();
        return false;
    }
    for (int y = 0; y < result.height; ++y) {
        std::memcpy(
            static_cast<std::uint8_t*>(surface->pixels) + static_cast<std::size_t>(y) * surface->pitch,
            result.rgba.data() + static_cast<std::size_t>(y) * result.width * 4,
            static_cast<std::size_t>(result.width) * 4
        );
    }

    // Reserve a private sibling directory. Never delete an unrelated staging file
    // or move the original away before the replacement is ready.
    static std::atomic<std::uint64_t> sequence{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto staging = std::filesystem::path(output_path.string() + ".surface-" +
        std::to_string(stamp) + "-" + std::to_string(sequence.fetch_add(1)));
    if (!std::filesystem::create_directory(staging, filesystem_error)) {
        SDL_DestroySurface(surface);
        error = "Could not reserve a temporary output directory";
        return false;
    }
    struct TemporaryOutput {
        std::filesystem::path directory;
        ~TemporaryOutput() {
            std::error_code ignored;
            std::filesystem::remove(directory / "surface.png", ignored);
            std::filesystem::remove(directory, ignored);
        }
    } cleanup{staging};
    const auto temporary = staging / "surface.png";
    const bool encoded = SDL_SavePNG(surface, temporary.string().c_str());
    SDL_DestroySurface(surface);
    if (!encoded) {
        error = SDL_GetError();
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), output_path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Could not finalize the generated PNG (Windows error " +
            std::to_string(GetLastError()) + ")";
        return false;
    }
#else
    std::filesystem::rename(temporary, output_path, filesystem_error);
    if (filesystem_error) {
        error = "Could not finalize the generated PNG: " + filesystem_error.message();
        return false;
    }
#endif
    return true;
}

} // namespace Engine
