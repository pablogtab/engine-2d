#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <cstdint>

namespace Engine {

// Content-agnostic correctness budgets. Hardware timings are recorded by the
// profiler but intentionally do not participate in portable pass/fail checks.
struct StressScenario {
    std::string name;
    std::size_t entities{0};
    std::size_t sprites{0};
    std::size_t physics_bodies{0};
    std::size_t contacts{0};
    std::size_t particles{0};
    std::size_t scripts{0};
    std::size_t lights{0};
    std::size_t post_process_passes{0};
    std::size_t render_targets{0};
    std::size_t max_draw_calls{0};
    std::size_t max_allocations{0};
};

struct StressScenarioResult {
    std::size_t entities{0};
    std::size_t sprites{0};
    std::size_t draw_calls{0};
    std::size_t physics_bodies{0};
    std::size_t contacts{0};
    std::size_t particles{0};
    std::size_t scripts{0};
    std::size_t lights{0};
    std::size_t post_process_passes{0};
    std::size_t render_targets{0};
    std::size_t allocations{0};
    bool teardown_clean{true};
    double cpu_milliseconds{0.0};
    std::string gpu_timing{"unavailable"};
    std::string platform;
    std::string configuration;
    int width{0};
    int height{0};
};

[[nodiscard]] bool validate_stress_budget(const StressScenario& scenario,
                                          const StressScenarioResult& result,
                                          std::string& error);
[[nodiscard]] const std::vector<StressScenario>& built_in_stress_scenarios();
[[nodiscard]] StressScenarioResult run_stress_scenario(
    const StressScenario& scenario,
    int width = 1920,
    int height = 1080,
    std::size_t iterations = 3
);
[[nodiscard]] bool export_stress_results(
    const std::string& path,
    const std::vector<std::pair<StressScenario, StressScenarioResult>>& results,
    std::string& error
);

} // namespace Engine
