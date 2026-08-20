#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Engine {

// Stable, project-local asset identity.  It deliberately carries no path and
// no filesystem behaviour: resolving it is AssetDatabase's responsibility.
struct AssetHandle {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool is_valid() const { return value != 0; }
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] static std::optional<AssetHandle> from_string(const std::string& text);

    constexpr bool operator==(const AssetHandle&) const = default;
};

} // namespace Engine

template<>
struct std::hash<Engine::AssetHandle> {
    std::size_t operator()(Engine::AssetHandle handle) const noexcept {
        return std::hash<std::uint64_t>{}(handle.value);
    }
};
