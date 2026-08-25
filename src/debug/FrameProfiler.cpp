#include "FrameProfiler.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Engine {

FrameProfiler::Scope::Scope(FrameProfiler* profiler, std::string_view name)
    : m_profiler(profiler), m_name(name) {
    if (!m_profiler || !m_profiler->m_enabled || !m_profiler->m_frame_open) {
        m_profiler = nullptr;
        return;
    }
    m_depth = m_profiler->m_depth++;
    m_started = std::chrono::steady_clock::now();
}

FrameProfiler::Scope::~Scope() {
    if (!m_profiler) return;
    const float milliseconds = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - m_started).count();
    --m_profiler->m_depth;
    m_profiler->finish_scope(m_name, m_depth, milliseconds);
}

void FrameProfiler::begin_frame() {
    if (!m_enabled || m_frame_open) return;
    m_frame_open = true;
    m_depth = 0;
    m_current = {};
    m_current.index = m_next_index++;
    m_frame_started = std::chrono::steady_clock::now();
}

void FrameProfiler::end_frame() {
    if (!m_enabled || !m_frame_open) return;
    m_current.frame_milliseconds = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - m_frame_started).count();
    if (m_frames.size() == k_max_frames) m_frames.erase(m_frames.begin());
    m_frames.push_back(std::move(m_current));
    m_current = {};
    m_frame_open = false;
    m_depth = 0;
}

void FrameProfiler::increment(std::string_view counter, std::uint64_t value) {
    if (!m_enabled || !m_frame_open) return;
    m_current.counters[std::string(counter)] += value;
}

void FrameProfiler::finish_scope(std::string_view name, std::uint32_t depth, float milliseconds) {
    if (!m_enabled || !m_frame_open) return;
    m_current.scopes.push_back({std::string(name), milliseconds, depth});
}

ProfileStatistics FrameProfiler::statistics(std::size_t requested_frames) const {
    ProfileStatistics result;
    if (m_frames.empty()) return result;
    const std::size_t count = std::min(requested_frames, m_frames.size());
    std::vector<float> samples;
    samples.reserve(count);
    for (auto it = m_frames.end() - static_cast<std::ptrdiff_t>(count); it != m_frames.end(); ++it) {
        samples.push_back(it->frame_milliseconds);
    }
    std::sort(samples.begin(), samples.end());
    result.minimum = samples.front(); result.maximum = samples.back();
    float sum = 0.0f; for (float value : samples) sum += value;
    result.average = sum / static_cast<float>(samples.size());
    const auto percentile = [&samples](float fraction) {
        return samples[std::min(samples.size() - 1, static_cast<std::size_t>(fraction * (samples.size() - 1)))];
    };
    result.p50 = percentile(0.50f); result.p95 = percentile(0.95f); result.p99 = percentile(0.99f);
    return result;
}

bool FrameProfiler::export_json(const std::string& path, std::string& error) const {
    try {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        nlohmann::json frames = nlohmann::json::array();
        for (const ProfileFrame& frame : m_frames) {
            nlohmann::json scopes = nlohmann::json::array();
            for (const auto& scope : frame.scopes) scopes.push_back({
                {"name", scope.name}, {"milliseconds", scope.milliseconds}, {"depth", scope.depth}
            });
            frames.push_back({{"index", frame.index}, {"frame_milliseconds", frame.frame_milliseconds},
                              {"scopes", std::move(scopes)}, {"counters", frame.counters}});
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Unable to open profiler capture"; return false; }
        out << nlohmann::json{{"schema_version", 1}, {"gpu_timing", "unavailable"}, {"frames", std::move(frames)}}.dump(2);
        return static_cast<bool>(out);
    } catch (const std::exception& exception) { error = exception.what(); return false; }
}

} // namespace Engine
