#include "Input.hpp"
#include "Log.hpp"
#include <algorithm>

namespace Engine {

std::unordered_map<KeyCode, bool> Input::s_current_keys;
std::unordered_map<KeyCode, bool> Input::s_previous_keys;

std::unordered_map<uint8_t, bool> Input::s_current_mouse;
std::unordered_map<uint8_t, bool> Input::s_previous_mouse;

glm::vec2 Input::s_mouse_pos{0.0f, 0.0f};
glm::vec2 Input::s_mouse_delta{0.0f, 0.0f};
glm::vec2 Input::s_mouse_scroll{0.0f, 0.0f};

std::unordered_map<std::string, ActionBinding> Input::s_actions;
std::unordered_map<std::string, AxisBinding> Input::s_axes;

SDL_Gamepad* Input::s_gamepad = nullptr;
float Input::s_deadzone = 8000.0f; // Standard SDL3 gamepad deadzone

void Input::init() {
    s_current_keys.clear();
    s_previous_keys.clear();
    s_current_mouse.clear();
    s_previous_mouse.clear();
    s_mouse_pos = glm::vec2(0.0f);
    s_mouse_delta = glm::vec2(0.0f);
    s_mouse_scroll = glm::vec2(0.0f);

    // Auto-detect and open first connected gamepad
    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    if (count > 0 && gamepads != nullptr) {
        s_gamepad = SDL_OpenGamepad(gamepads[0]);
        if (s_gamepad) {
            ENGINE_LOG_INFO("Gamepad connected: '{}'", SDL_GetGamepadName(s_gamepad));
        }
        SDL_free(gamepads);
    }

    // Default Mappings
    map_action("Jump", ActionBinding{
        { SDL_SCANCODE_SPACE, SDL_SCANCODE_W },
        {},
        { GamepadButton::A }
    });

    map_action("Attack", ActionBinding{
        { SDL_SCANCODE_J },
        { MouseButton::LEFT },
        { GamepadButton::X }
    });

    map_axis("Horizontal", AxisBinding{
        SDL_SCANCODE_A, SDL_SCANCODE_D, GamepadAxis::LEFT_X, false
    });

    map_axis("Vertical", AxisBinding{
        SDL_SCANCODE_S, SDL_SCANCODE_W, GamepadAxis::LEFT_Y, true // Invert Y so Up is positive
    });

    ENGINE_LOG_INFO("Engine InputManager initialized.");
}

void Input::shutdown() {
    if (s_gamepad) {
        SDL_CloseGamepad(s_gamepad);
        s_gamepad = nullptr;
    }
}

void Input::update() {
    s_previous_keys = s_current_keys;
    s_previous_mouse = s_current_mouse;
    s_mouse_delta = glm::vec2(0.0f);
    s_mouse_scroll = glm::vec2(0.0f);
}

void Input::process_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            s_current_keys[event.key.scancode] = true;
            break;

        case SDL_EVENT_KEY_UP:
            s_current_keys[event.key.scancode] = false;
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            s_current_mouse[event.button.button] = true;
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            s_current_mouse[event.button.button] = false;
            break;

        case SDL_EVENT_MOUSE_MOTION:
            s_mouse_pos.x = event.motion.x;
            s_mouse_pos.y = event.motion.y;
            s_mouse_delta.x += event.motion.xrel;
            s_mouse_delta.y += event.motion.yrel;
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            s_mouse_scroll.x = event.wheel.x;
            s_mouse_scroll.y = event.wheel.y;
            break;

        case SDL_EVENT_GAMEPAD_ADDED:
            if (!s_gamepad) {
                s_gamepad = SDL_OpenGamepad(event.gdevice.which);
                if (s_gamepad) {
                    ENGINE_LOG_INFO("Gamepad connected: '{}'", SDL_GetGamepadName(s_gamepad));
                }
            }
            break;

        case SDL_EVENT_GAMEPAD_REMOVED:
            if (s_gamepad && SDL_GetGamepadID(s_gamepad) == event.gdevice.which) {
                ENGINE_LOG_INFO("Gamepad disconnected.");
                SDL_CloseGamepad(s_gamepad);
                s_gamepad = nullptr;
            }
            break;
    }
}

bool Input::is_key_pressed(KeyCode key) {
    auto it = s_current_keys.find(key);
    return it != s_current_keys.end() && it->second;
}

bool Input::is_key_just_pressed(KeyCode key) {
    bool curr = is_key_pressed(key);
    auto it = s_previous_keys.find(key);
    bool prev = (it != s_previous_keys.end()) ? it->second : false;
    return curr && !prev;
}

bool Input::is_key_just_released(KeyCode key) {
    bool curr = is_key_pressed(key);
    auto it = s_previous_keys.find(key);
    bool prev = (it != s_previous_keys.end()) ? it->second : false;
    return !curr && prev;
}

bool Input::is_mouse_pressed(MouseButton button) {
    auto it = s_current_mouse.find(static_cast<uint8_t>(button));
    return it != s_current_mouse.end() && it->second;
}

bool Input::is_mouse_just_pressed(MouseButton button) {
    bool curr = is_mouse_pressed(button);
    auto it = s_previous_mouse.find(static_cast<uint8_t>(button));
    bool prev = (it != s_previous_mouse.end()) ? it->second : false;
    return curr && !prev;
}

bool Input::is_mouse_just_released(MouseButton button) {
    bool curr = is_mouse_pressed(button);
    auto it = s_previous_mouse.find(static_cast<uint8_t>(button));
    bool prev = (it != s_previous_mouse.end()) ? it->second : false;
    return !curr && prev;
}

glm::vec2 Input::get_mouse_position() {
    return s_mouse_pos;
}

glm::vec2 Input::get_mouse_delta() {
    return s_mouse_delta;
}

glm::vec2 Input::get_mouse_scroll() {
    return s_mouse_scroll;
}

glm::vec2 Input::get_mouse_world_position(const Camera2D& camera) {
    glm::vec2 viewport = camera.get_viewport_size();
    glm::vec2 screen_centered = s_mouse_pos - (viewport * 0.5f);

    float zoom = camera.get_zoom();
    float z_scale = camera.get_z_scale(0.0f); // Default Z=0 plane

    glm::vec2 world_pos = glm::vec2(
        (screen_centered.x / (zoom * z_scale)) + camera.get_position().x,
        -(screen_centered.y / (zoom * z_scale)) + camera.get_position().y
    );
    return world_pos;
}

void Input::map_action(const std::string& action_name, const ActionBinding& binding) {
    s_actions[action_name] = binding;
}

bool Input::is_action_pressed(const std::string& action_name) {
    auto it = s_actions.find(action_name);
    if (it == s_actions.end()) return false;

    const auto& binding = it->second;
    for (KeyCode k : binding.keys) {
        if (is_key_pressed(k)) return true;
    }
    for (MouseButton m : binding.mouse_buttons) {
        if (is_mouse_pressed(m)) return true;
    }
    if (s_gamepad) {
        for (GamepadButton g : binding.gamepad_buttons) {
            if (SDL_GetGamepadButton(s_gamepad, static_cast<SDL_GamepadButton>(g))) return true;
        }
    }
    return false;
}

bool Input::is_action_just_pressed(const std::string& action_name) {
    auto it = s_actions.find(action_name);
    if (it == s_actions.end()) return false;

    const auto& binding = it->second;
    for (KeyCode k : binding.keys) {
        if (is_key_just_pressed(k)) return true;
    }
    for (MouseButton m : binding.mouse_buttons) {
        if (is_mouse_just_pressed(m)) return true;
    }
    return false;
}

bool Input::is_action_just_released(const std::string& action_name) {
    auto it = s_actions.find(action_name);
    if (it == s_actions.end()) return false;

    const auto& binding = it->second;
    for (KeyCode k : binding.keys) {
        if (is_key_just_released(k)) return true;
    }
    for (MouseButton m : binding.mouse_buttons) {
        if (is_mouse_just_released(m)) return true;
    }
    return false;
}

void Input::map_axis(const std::string& axis_name, const AxisBinding& binding) {
    s_axes[axis_name] = binding;
}

float Input::get_axis(const std::string& axis_name) {
    auto it = s_axes.find(axis_name);
    if (it == s_axes.end()) return 0.0f;

    const auto& binding = it->second;
    float value = 0.0f;

    if (binding.pos_key != SDL_SCANCODE_UNKNOWN && is_key_pressed(binding.pos_key)) value += 1.0f;
    if (binding.neg_key != SDL_SCANCODE_UNKNOWN && is_key_pressed(binding.neg_key)) value -= 1.0f;

    if (value == 0.0f && s_gamepad) {
        int16_t raw_axis = SDL_GetGamepadAxis(s_gamepad, static_cast<SDL_GamepadAxis>(binding.gamepad_axis));
        if (std::abs(raw_axis) > s_deadzone) {
            value = static_cast<float>(raw_axis) / 32767.0f;
            if (binding.invert_gamepad) value = -value;
        }
    }

    return std::clamp(value, -1.0f, 1.0f);
}

bool Input::is_gamepad_connected() {
    return s_gamepad != nullptr;
}

float Input::get_gamepad_axis_value(GamepadAxis axis) {
    if (!s_gamepad) return 0.0f;
    int16_t raw_axis = SDL_GetGamepadAxis(s_gamepad, static_cast<SDL_GamepadAxis>(axis));
    if (std::abs(raw_axis) <= s_deadzone) return 0.0f;
    return static_cast<float>(raw_axis) / 32767.0f;
}

} // namespace Engine
