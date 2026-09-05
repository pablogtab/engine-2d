#include "editor/tools/SurfaceGenerationService.hpp"

#include <cstdlib>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void print_usage() {
    std::cerr << "Usage: SurfaceGenerateTool <output.png> <width> <height> <seed> <source.png> [source.png ...]\n";
}

bool parse_positive(const char* text, int& value) {
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || parsed <= 0 || parsed > 8192) return false;
    value = static_cast<int>(parsed);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 6 || argc > 11) {
        print_usage();
        return 2;
    }

    Engine::SurfaceGenerationSettings settings;
    if (!parse_positive(argv[2], settings.width) || !parse_positive(argv[3], settings.height)) {
        print_usage();
        return 2;
    }
    const std::string seed_text = argv[4];
    const auto [seed_end, seed_error] = std::from_chars(
        seed_text.data(), seed_text.data() + seed_text.size(), settings.seed);
    if (seed_error != std::errc{} || seed_end != seed_text.data() + seed_text.size()) {
        print_usage();
        return 2;
    }
    for (int index = 5; index < argc; ++index) {
        settings.sources.push_back({std::filesystem::path(argv[index]), 1.0f, true, true, false});
    }

    const Engine::SurfaceGenerationResult result = Engine::SurfaceGenerationService::generate(settings);
    if (!result.success) {
        std::cerr << result.error << '\n';
        return 1;
    }
    std::string error;
    if (!Engine::SurfaceGenerationService::save_png_atomic(argv[1], result, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    std::cout << "Generated " << result.width << 'x' << result.height
              << " surface, hash=" << std::hex << result.content_hash << std::dec
              << ", seam_error=" << result.metrics.average_overlap_error
              << ", repetition=" << result.metrics.repetition_score << '\n';
    for (const std::string& warning : result.warnings) std::cout << "Warning: " << warning << '\n';
    return 0;
}
