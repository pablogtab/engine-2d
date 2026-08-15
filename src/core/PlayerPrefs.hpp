#pragma once

#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Engine {

// ---------------------------------------------------------------------------
// PlayerPrefs
// In-memory key-value store for user configurations, audio volumes, keybindings,
// and gameplay flags, persisted to "save://preferences.json".
// ---------------------------------------------------------------------------
class PlayerPrefs {
public:
    static void init(const std::string& filepath = "save://preferences.json");

    // Setters
    static void set_int(std::string_view key, int value);
    static void set_float(std::string_view key, float value);
    static void set_bool(std::string_view key, bool value);
    static void set_string(std::string_view key, std::string_view value);
    static void set_vec2(std::string_view key, const glm::vec2& value);
    static void set_vec3(std::string_view key, const glm::vec3& value);

    // Getters (with fallback default values)
    [[nodiscard]] static int         get_int(std::string_view key, int default_value = 0);
    [[nodiscard]] static float       get_float(std::string_view key, float default_value = 0.0f);
    [[nodiscard]] static bool        get_bool(std::string_view key, bool default_value = false);
    [[nodiscard]] static std::string get_string(std::string_view key, std::string_view default_value = "");
    [[nodiscard]] static glm::vec2   get_vec2(std::string_view key, const glm::vec2& default_value = glm::vec2(0.0f));
    [[nodiscard]] static glm::vec3   get_vec3(std::string_view key, const glm::vec3& default_value = glm::vec3(0.0f));

    // Management
    [[nodiscard]] static bool has_key(std::string_view key);
    static void delete_key(std::string_view key);
    static void delete_all();

    // Disk sync
    static bool save();
    static bool load();

private:
    static std::string s_filepath;
    static nlohmann::json s_prefs;
    static bool s_dirty;
    static bool s_initialized;
};

} // namespace Engine
