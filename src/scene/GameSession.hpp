#pragma once

#include <string>
#include <utility>

namespace Engine {

class GameSession {
public:
    enum class TransitionState { Idle, Requested, Loaded, Failed, Cancelled };

    [[nodiscard]] bool request_scene(const std::string& path, std::string& error) {
        if (path.empty()) {
            error = "Scene path must not be empty";
            return false;
        }
        m_state = TransitionState::Requested;
        m_path = path;
        m_error.clear();
        return true;
    }

    void complete_loaded() { m_state = TransitionState::Loaded; }
    void complete_failed(std::string error) { m_state = TransitionState::Failed; m_error = std::move(error); }
    void cancel() { m_state = TransitionState::Cancelled; }
    void reset() { m_state = TransitionState::Idle; m_path.clear(); m_error.clear(); }

    [[nodiscard]] TransitionState state() const { return m_state; }
    [[nodiscard]] const std::string& path() const { return m_path; }
    [[nodiscard]] const std::string& error() const { return m_error; }

private:
    TransitionState m_state{TransitionState::Idle};
    std::string m_path;
    std::string m_error;
};

} // namespace Engine
