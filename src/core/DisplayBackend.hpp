#pragma once

#include "Display.hpp"
#include <string>
#include <vector>

namespace Engine {

class IDisplayBackend {
public:
    virtual ~IDisplayBackend() = default;

    [[nodiscard]] virtual DisplayCapabilities capabilities() const = 0;
    [[nodiscard]] virtual std::vector<DisplayInfo> available_displays() const = 0;
    [[nodiscard]] virtual std::vector<DisplayMode> available_modes(DisplayId display_id) const = 0;
    [[nodiscard]] virtual DisplayState query_state() const = 0;
    virtual bool apply(const DisplaySettings& settings, std::string& error) = 0;
};

} // namespace Engine
