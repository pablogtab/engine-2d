#pragma once

#include "../renderer/RenderView.hpp"
#include <string>

namespace Engine {

struct GameViewSettings {
    int reference_width{1920};
    int reference_height{1080};
    PresentationMode presentation{PresentationMode::Fit};

    [[nodiscard]] float aspect_ratio() const {
        return static_cast<float>(reference_width) / static_cast<float>(reference_height);
    }

    bool operator==(const GameViewSettings&) const = default;
};

class ProjectSettings {
public:
    bool load(const std::string& filepath = "config/project.json");
    bool save() const;

    [[nodiscard]] const GameViewSettings& game_view() const { return m_game_view; }
    [[nodiscard]] const std::string& filepath() const { return m_filepath; }
    void set_game_view(const GameViewSettings& settings);

private:
    std::string m_filepath{"config/project.json"};
    GameViewSettings m_game_view;
};

} // namespace Engine
