#pragma once

#include <string>

namespace Engine {

enum class GameViewScaling {
    Fit,
    Fill,
    Stretch
};

struct GameViewSettings {
    int width{1920};
    int height{1080};
    GameViewScaling scaling{GameViewScaling::Fit};

    [[nodiscard]] float aspect_ratio() const {
        return static_cast<float>(width) / static_cast<float>(height);
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
