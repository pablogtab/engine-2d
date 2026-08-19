#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace Engine {

class ComponentRegistry;

class SceneSchema {
public:
    static constexpr std::uint32_t CURRENT_VERSION = 2;

    [[nodiscard]] static nlohmann::json component_versions(const ComponentRegistry& registry);
    [[nodiscard]] static bool migrate_to_current(
        nlohmann::json& root,
        const ComponentRegistry& registry,
        std::string& error
    );
    [[nodiscard]] static bool validate_structure(
        const nlohmann::json& root,
        const ComponentRegistry& registry,
        std::string& error
    );
};

} // namespace Engine
