#pragma once

#include <string>
#include <vector>

namespace Engine {

struct AutosaveRecord {
    std::string identity;
    std::string source_path;
    std::string scene_name;
    std::string snapshot_json;
    std::string recovery_path;
};

class AutosaveRecovery {
public:
    explicit AutosaveRecovery(std::string directory = "Library/Autosaves");

    [[nodiscard]] bool write_snapshot(const std::string& identity, const std::string& source_path,
        const std::string& scene_name, const std::string& snapshot_json) const;
    [[nodiscard]] std::vector<AutosaveRecord> discover() const;
    [[nodiscard]] bool remove(const std::string& identity) const;

private:
    [[nodiscard]] std::string path_for_identity(const std::string& identity) const;
    std::string m_directory;
};

} // namespace Engine
