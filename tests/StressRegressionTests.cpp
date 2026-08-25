#include "debug/StressScenarios.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>

int main() {
    for (const auto& scenario : Engine::built_in_stress_scenarios()) {
        Engine::StressScenarioResult pass = Engine::run_stress_scenario(scenario, 1280, 720, 2);
        std::string error;
        if (!Engine::validate_stress_budget(scenario, pass, error)) {
            std::cerr << scenario.name << ": " << error << '\n';
        }
        assert(Engine::validate_stress_budget(scenario, pass, error));
        pass.draw_calls = scenario.max_draw_calls + 1;
        assert(!Engine::validate_stress_budget(scenario, pass, error));
        assert(!error.empty());
    }
    std::vector<std::pair<Engine::StressScenario, Engine::StressScenarioResult>> results;
    for (const auto& scenario : Engine::built_in_stress_scenarios()) {
        results.emplace_back(scenario, Engine::run_stress_scenario(scenario));
    }
    assert(results.size() == 4);
    assert(results[1].second.physics_bodies > 0 && results[1].second.contacts > 0);
    assert(results[2].second.particles > 0 && results[2].second.scripts > 0);
    assert(results[3].second.lights > 0 && results[3].second.post_process_passes > 0);
    std::string error;
    const auto path = std::filesystem::temp_directory_path() / "engine2d-stress-results.json";
    assert(Engine::export_stress_results(path.string(), results, error));
    assert(std::filesystem::exists(path));
    std::filesystem::remove(path);
}
