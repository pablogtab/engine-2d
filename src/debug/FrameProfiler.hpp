#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Engine {

// Runtime-owned frame data.  The editor only reads completed frames, so the
// profiler is equally usable by the standalone player and by headless tests.
struct ProfileScopeSample {
    std::string name;
    float milliseconds{0.0f};
    std::uint32_t depth{0};
};

struct ProfileFrame {
    std::uint64_t index{0};
    float frame_milliseconds{0.0f};
    std::vector<ProfileScopeSample> scopes;
    std::unordered_map<std::string, std::uint64_t> counters;
};

struct ProfileStatistics {
    float minimum{0.0f};
    float maximum{0.0f};
    float average{0.0f};
    float p50{0.0f};
    float p95{0.0f};
    float p99{0.0f};
};

class FrameProfiler {
public:
    static constexpr std::size_t k_max_frames = 300;

    class Scope {
    public:
        Scope(FrameProfiler* profiler, std::string_view name);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        FrameProfiler* m_profiler{nullptr};
        std::string_view m_name;
        std::chrono::steady_clock::time_point m_started{};
        std::uint32_t m_depth{0};
    };

    void set_enabled(bool enabled) { m_enabled = enabled; }
    [[nodiscard]] bool enabled() const { return m_enabled; }
    void begin_frame();
    void end_frame();
    void increment(std::string_view counter, std::uint64_t value = 1);
    [[nodiscard]] Scope scope(std::string_view name) { return Scope(this, name); }
    [[nodiscard]] const std::vector<ProfileFrame>& frames() const { return m_frames; }
    [[nodiscard]] ProfileStatistics statistics(std::size_t requested_frames) const;
    [[nodiscard]] bool export_json(const std::string& path, std::string& error) const;

private:
    friend class Scope;
    void finish_scope(std::string_view name, std::uint32_t depth, float milliseconds);
    bool m_enabled{false};
    bool m_frame_open{false};
    std::uint64_t m_next_index{1};
    std::uint32_t m_depth{0};
    std::chrono::steady_clock::time_point m_frame_started{};
    ProfileFrame m_current;
    std::vector<ProfileFrame> m_frames;
};

} // namespace Engine
