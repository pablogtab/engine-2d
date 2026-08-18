#include "ProjectSettings.hpp"

#include "FileSystem.hpp"
#include "Log.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Engine {

namespace {

const char* scaling_to_string(GameViewScaling scaling) {
    switch (scaling) {
        case GameViewScaling::Fill: return "fill";
        case GameViewScaling::Stretch: return "stretch";
        case GameViewScaling::Fit: return "fit";
    }
    return "fit";
}

GameViewScaling scaling_from_string(const std::string& value) {
    if (value == "fill") return GameViewScaling::Fill;
    if (value == "stretch") return GameViewScaling::Stretch;
    return GameViewScaling::Fit;
}

} // namespace

bool ProjectSettings::load(const std::string& filepath) {
    m_filepath = filepath;
    const std::string text = FileSystem::read_text(filepath);
    if (text.empty()) return false;

    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        ENGINE_LOG_ERROR("[PROJECT SETTINGS] Invalid JSON in '{}'", filepath);
        return false;
    }

    const auto& game_view = root.contains("game_view") ? root["game_view"] : nlohmann::json::object();
    m_game_view.width = std::clamp(game_view.value("width", 1920), 64, 16384);
    m_game_view.height = std::clamp(game_view.value("height", 1080), 64, 16384);
    m_game_view.scaling = scaling_from_string(game_view.value("scaling", std::string{"fit"}));
    ENGINE_LOG_INFO(
        "[PROJECT SETTINGS] Game output: {}x{} ({})",
        m_game_view.width,
        m_game_view.height,
        scaling_to_string(m_game_view.scaling)
    );
    return true;
}

bool ProjectSettings::save() const {
    nlohmann::json root;
    root["game_view"] = {
        {"width", m_game_view.width},
        {"height", m_game_view.height},
        {"scaling", scaling_to_string(m_game_view.scaling)}
    };
    return FileSystem::write_text(m_filepath, root.dump(2) + "\n");
}

void ProjectSettings::set_game_view(const GameViewSettings& settings) {
    m_game_view.width = std::clamp(settings.width, 64, 16384);
    m_game_view.height = std::clamp(settings.height, 64, 16384);
    m_game_view.scaling = settings.scaling;
    save();
}

} // namespace Engine
