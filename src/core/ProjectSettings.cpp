#include "ProjectSettings.hpp"

#include "FileSystem.hpp"
#include "Log.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Engine {

namespace {

const char* presentation_to_string(PresentationMode presentation) {
    switch (presentation) {
        case PresentationMode::Fill: return "fill";
        case PresentationMode::Stretch: return "stretch";
        case PresentationMode::Responsive: return "responsive";
        case PresentationMode::Fit: return "fit";
    }
    return "fit";
}

PresentationMode presentation_from_string(const std::string& value) {
    if (value == "fill") return PresentationMode::Fill;
    if (value == "stretch") return PresentationMode::Stretch;
    if (value == "responsive") return PresentationMode::Responsive;
    return PresentationMode::Fit;
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
    m_game_view.reference_width = std::clamp(game_view.value("reference_width", 1920), 64, 16384);
    m_game_view.reference_height = std::clamp(game_view.value("reference_height", 1080), 64, 16384);
    m_game_view.presentation = presentation_from_string(game_view.value("presentation", std::string{"fit"}));
    ENGINE_LOG_INFO(
        "[PROJECT SETTINGS] Game output: {}x{} ({})",
        m_game_view.reference_width,
        m_game_view.reference_height,
        presentation_to_string(m_game_view.presentation)
    );
    return true;
}

bool ProjectSettings::save() const {
    nlohmann::json root;
    root["game_view"] = {
        {"reference_width", m_game_view.reference_width},
        {"reference_height", m_game_view.reference_height},
        {"presentation", presentation_to_string(m_game_view.presentation)}
    };
    return FileSystem::write_text(m_filepath, root.dump(2) + "\n");
}

void ProjectSettings::set_game_view(const GameViewSettings& settings) {
    m_game_view.reference_width = std::clamp(settings.reference_width, 64, 16384);
    m_game_view.reference_height = std::clamp(settings.reference_height, 64, 16384);
    m_game_view.presentation = settings.presentation;
    save();
}

} // namespace Engine
