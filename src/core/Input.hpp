#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include "../scene/Camera2D.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace Engine {

// Key Code Alias
using KeyCode = SDL_Scancode;

enum class MouseButton {
    LEFT = SDL_BUTTON_LEFT,
    MIDDLE = SDL_BUTTON_MIDDLE,
    RIGHT = SDL_BUTTON_RIGHT
};

enum class GamepadButton {
    A = SDL_GAMEPAD_BUTTON_SOUTH,
    B = SDL_GAMEPAD_BUTTON_EAST,
    X = SDL_GAMEPAD_BUTTON_WEST,
    Y = SDL_GAMEPAD_BUTTON_NORTH,
    BACK = SDL_GAMEPAD_BUTTON_BACK,
    START = SDL_GAMEPAD_BUTTON_START,
    LEFT_STICK = SDL_GAMEPAD_BUTTON_LEFT_STICK,
    RIGHT_STICK = SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    LEFT_SHOULDER = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    RIGHT_SHOULDER = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    DPAD_UP = SDL_GAMEPAD_BUTTON_DPAD_UP,
    DPAD_DOWN = SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    DPAD_LEFT = SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    DPAD_RIGHT = SDL_GAMEPAD_BUTTON_DPAD_RIGHT
};

enum class GamepadAxis {
    LEFT_X = SDL_GAMEPAD_AXIS_LEFTX,
    LEFT_Y = SDL_GAMEPAD_AXIS_LEFTY,
    RIGHT_X = SDL_GAMEPAD_AXIS_RIGHTX,
    RIGHT_Y = SDL_GAMEPAD_AXIS_RIGHTY,
    TRIGGER_LEFT = SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    TRIGGER_RIGHT = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
};

struct ActionBinding {
    std::vector<KeyCode> keys;
    std::vector<MouseButton> mouse_buttons;
    std::vector<GamepadButton> gamepad_buttons;
};

struct AxisBinding {
    KeyCode neg_key{SDL_SCANCODE_UNKNOWN};
    KeyCode pos_key{SDL_SCANCODE_UNKNOWN};
    GamepadAxis gamepad_axis{GamepadAxis::LEFT_X};
    bool invert_gamepad{false};
};

class Input {
public:
    static void init();
    static void shutdown();
    static void update(); // Called at start of frame to update previous states
    static void process_event(const SDL_Event& event);

    // Keyboard Queries
    static bool is_key_pressed(KeyCode key);
    static bool is_key_just_pressed(KeyCode key);
    static bool is_key_just_released(KeyCode key);

    // Mouse Queries
    static bool is_mouse_pressed(MouseButton button);
    static bool is_mouse_just_pressed(MouseButton button);
    static bool is_mouse_just_released(MouseButton button);
    static glm::vec2 get_mouse_position();
    static glm::vec2 get_mouse_delta();
    static glm::vec2 get_mouse_scroll();
    static glm::vec2 get_mouse_world_position(const Camera2D& camera);

    // Action Mappings ("Jump", "Attack", "Pause")
    static void map_action(const std::string& action_name, const ActionBinding& binding);
    static bool is_action_pressed(const std::string& action_name);
    static bool is_action_just_pressed(const std::string& action_name);
    static bool is_action_just_released(const std::string& action_name);

    // Axis Mappings ("Horizontal", "Vertical") -> returns [-1.0f, +1.0f]
    static void map_axis(const std::string& axis_name, const AxisBinding& binding);
    static float get_axis(const std::string& axis_name);

    // Gamepad Status
    static bool is_gamepad_connected();
    static float get_gamepad_axis_value(GamepadAxis axis);

private:
    static std::unordered_map<KeyCode, bool> s_current_keys;
    static std::unordered_map<KeyCode, bool> s_previous_keys;

    static std::unordered_map<uint8_t, bool> s_current_mouse;
    static std::unordered_map<uint8_t, bool> s_previous_mouse;

    static glm::vec2 s_mouse_pos;
    static glm::vec2 s_mouse_delta;
    static glm::vec2 s_mouse_scroll;

    static std::unordered_map<std::string, ActionBinding> s_actions;
    static std::unordered_map<std::string, AxisBinding> s_axes;

    static SDL_Gamepad* s_gamepad;
    static float s_deadzone;
};

} // namespace Engine
