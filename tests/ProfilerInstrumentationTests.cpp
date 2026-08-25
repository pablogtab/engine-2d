#include "debug/FrameProfiler.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace Engine;

int main() {
    FrameProfiler profiler;
    profiler.begin_frame();
    profiler.increment("ignored");
    profiler.end_frame();
    assert(profiler.frames().empty());

    profiler.set_enabled(true);
    profiler.begin_frame();
    profiler.increment("scripts", 2);
    { auto outer = profiler.scope("frame"); { auto inner = profiler.scope("physics"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }
    profiler.end_frame();
    assert(profiler.frames().size() == 1);
    assert(profiler.frames().back().scopes.size() == 2);
    assert(profiler.frames().back().scopes[0].depth == 1);
    assert(profiler.frames().back().counters.at("scripts") == 2);
    assert(profiler.statistics(60).maximum >= profiler.statistics(60).minimum);
    for (int frame = 0; frame < 305; ++frame) {
        profiler.begin_frame();
        profiler.increment("draw_calls", static_cast<std::uint64_t>(frame));
        profiler.end_frame();
    }
    assert(profiler.frames().size() == FrameProfiler::k_max_frames);
    assert(profiler.frames().back().counters.at("draw_calls") == 304);
    std::string error;
    const auto path = std::filesystem::temp_directory_path() / "engine2d-profiler-test.json";
    assert(profiler.export_json(path.string(), error));
    assert(std::filesystem::exists(path));
    std::filesystem::remove(path);
}
