#include "AutosaveRecovery.hpp"

#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace Engine {
namespace {
constexpr int AUTOSAVE_VERSION = 1;

std::string stable_key(const std::string& value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::string fingerprint(const std::string& value) {
    return stable_key(value);
}
} // namespace

AutosaveRecovery::AutosaveRecovery(std::string directory) : m_directory(std::move(directory)) {}

bool AutosaveRecovery::write_snapshot(const std::string& identity, const std::string& source_path,
    const std::string& scene_name, const std::string& snapshot_json) const {
    if (identity.empty() || scene_name.empty() || snapshot_json.empty()) {
        ENGINE_LOG_WARN("[AUTOSAVE] Refusing incomplete recovery snapshot");
        return false;
    }
    const std::string source_content = source_path.empty() ? "" : FileSystem::read_text(source_path);
    const bool source_exists = !source_path.empty() && std::filesystem::exists(source_path);
    const nlohmann::json record{{"version", AUTOSAVE_VERSION}, {"identity", identity},
        {"source_path", source_path}, {"source_exists", source_exists},
        {"source_fingerprint", source_exists ? fingerprint(source_content) : ""},
        {"scene_name", scene_name}, {"snapshot_json", snapshot_json}};
    return FileSystem::write_text_atomically(path_for_identity(identity), record.dump());
}

std::vector<AutosaveRecord> AutosaveRecovery::discover() const {
    std::vector<AutosaveRecord> records;
    std::error_code error;
    if (!std::filesystem::exists(m_directory, error)) return records;
    for (const auto& entry : std::filesystem::directory_iterator(m_directory, error)) {
        if (error) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".autosave") continue;
        try {
            const nlohmann::json json = nlohmann::json::parse(FileSystem::read_text(entry.path().string()));
            if (json.value("version", 0) != AUTOSAVE_VERSION || !json.contains("identity") ||
                !json.contains("scene_name") || !json.contains("snapshot_json") ||
                !json["identity"].is_string() || !json["scene_name"].is_string() || !json["snapshot_json"].is_string()) {
                ENGINE_LOG_WARN("[AUTOSAVE] Ignoring malformed recovery snapshot '{}'", entry.path().string());
                continue;
            }
            AutosaveRecord record{
                .identity = json["identity"].get<std::string>(),
                .source_path = json.value("source_path", ""),
                .scene_name = json["scene_name"].get<std::string>(),
                .snapshot_json = json["snapshot_json"].get<std::string>(),
                .recovery_path = entry.path().string(),
                .source_conflicted = false
            };
            if (!record.source_path.empty() && json.value("source_exists", false)) {
                const std::string current = FileSystem::read_text(record.source_path);
                record.source_conflicted = !std::filesystem::exists(record.source_path) ||
                    fingerprint(current) != json.value("source_fingerprint", "");
            }
            if (!record.identity.empty() && !record.scene_name.empty() && !record.snapshot_json.empty()) records.push_back(std::move(record));
        } catch (const std::exception& exception) {
            ENGINE_LOG_WARN("[AUTOSAVE] Ignoring unreadable recovery snapshot '{}': {}", entry.path().string(), exception.what());
        }
    }
    return records;
}

bool AutosaveRecovery::remove(const std::string& identity) const {
    if (identity.empty()) return true;
    std::error_code error;
    std::filesystem::remove(path_for_identity(identity), error);
    if (error) {
        ENGINE_LOG_ERROR("[AUTOSAVE] Failed removing recovery snapshot: {}", error.message());
        return false;
    }
    return true;
}

std::string AutosaveRecovery::path_for_identity(const std::string& identity) const {
    return (std::filesystem::path(m_directory) / (stable_key(identity) + ".autosave")).string();
}
} // namespace Engine
