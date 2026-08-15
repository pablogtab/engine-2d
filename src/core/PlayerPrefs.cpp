#include "PlayerPrefs.hpp"
#include "SaveSystem.hpp"
#include "Log.hpp"

namespace Engine {

std::string PlayerPrefs::s_filepath = "save://preferences.json";
nlohmann::json PlayerPrefs::s_prefs = nlohmann::json::object();
bool PlayerPrefs::s_dirty = false;
bool PlayerPrefs::s_initialized = false;

void PlayerPrefs::init(const std::string& filepath) {
    s_filepath = filepath;
    s_initialized = true;
    load();
}

void PlayerPrefs::set_int(std::string_view key, int value) {
    s_prefs[std::string(key)] = value;
    s_dirty = true;
}

void PlayerPrefs::set_float(std::string_view key, float value) {
    s_prefs[std::string(key)] = value;
    s_dirty = true;
}

void PlayerPrefs::set_bool(std::string_view key, bool value) {
    s_prefs[std::string(key)] = value;
    s_dirty = true;
}

void PlayerPrefs::set_string(std::string_view key, std::string_view value) {
    s_prefs[std::string(key)] = std::string(value);
    s_dirty = true;
}

void PlayerPrefs::set_vec2(std::string_view key, const glm::vec2& value) {
    s_prefs[std::string(key)] = { value.x, value.y };
    s_dirty = true;
}

void PlayerPrefs::set_vec3(std::string_view key, const glm::vec3& value) {
    s_prefs[std::string(key)] = { value.x, value.y, value.z };
    s_dirty = true;
}

int PlayerPrefs::get_int(std::string_view key, int default_value) {
    std::string k(key);
    if (s_prefs.contains(k) && s_prefs[k].is_number_integer()) {
        return s_prefs[k].get<int>();
    }
    return default_value;
}

float PlayerPrefs::get_float(std::string_view key, float default_value) {
    std::string k(key);
    if (s_prefs.contains(k) && s_prefs[k].is_number()) {
        return s_prefs[k].get<float>();
    }
    return default_value;
}

bool PlayerPrefs::get_bool(std::string_view key, bool default_value) {
    std::string k(key);
    if (s_prefs.contains(k) && s_prefs[k].is_boolean()) {
        return s_prefs[k].get<bool>();
    }
    return default_value;
}

std::string PlayerPrefs::get_string(std::string_view key, std::string_view default_value) {
    std::string k(key);
    if (s_prefs.contains(k) && s_prefs[k].is_string()) {
        return s_prefs[k].get<std::string>();
    }
    return std::string(default_value);
}

glm::vec2 PlayerPrefs::get_vec2(std::string_view key, const glm::vec2& default_value) {
    std::string k(key);
    if (s_prefs.contains(k) && s_prefs[k].is_array() && s_prefs[k].size() >= 2) {
        return glm::vec2(s_prefs[k][0].get<float>(), s_prefs[k][1].get<float>());
    }
    return default_value;
}

glm::vec3 PlayerPrefs::get_vec3(std::string_view key, const glm::vec3& default_value) {
    std::string k(key);
    if (s_prefs.contains(k) && s_prefs[k].is_array() && s_prefs[k].size() >= 3) {
        return glm::vec3(s_prefs[k][0].get<float>(), s_prefs[k][1].get<float>(), s_prefs[k][2].get<float>());
    }
    return default_value;
}

bool PlayerPrefs::has_key(std::string_view key) {
    return s_prefs.contains(std::string(key));
}

void PlayerPrefs::delete_key(std::string_view key) {
    std::string k(key);
    if (s_prefs.contains(k)) {
        s_prefs.erase(k);
        s_dirty = true;
    }
}

void PlayerPrefs::delete_all() {
    s_prefs.clear();
    s_dirty = true;
}

bool PlayerPrefs::save() {
    bool ok = SaveSystem::write_json(s_filepath, s_prefs, true);
    if (ok) {
        s_dirty = false;
        ENGINE_LOG_INFO("[PLAYERPREFS] Saved preferences to '{}'", s_filepath);
    }
    return ok;
}

bool PlayerPrefs::load() {
    if (!FileSystem::exists(s_filepath)) {
        s_prefs = nlohmann::json::object();
        return false;
    }

    bool ok = SaveSystem::read_json(s_filepath, s_prefs);
    if (ok) {
        s_dirty = false;
        ENGINE_LOG_INFO("[PLAYERPREFS] Loaded preferences from '{}' (Keys: {})", s_filepath, s_prefs.size());
    }
    return ok;
}

} // namespace Engine
