#include "AssetFileCommands.hpp"

#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include "../../core/Log.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace Engine {
namespace {

bool write_bytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    return FileSystem::write_bytes(path.string(), bytes);
}

void rescan(AssetDatabase* database) {
    if (database) [[maybe_unused]] const auto result = database->rescan();
}

} // namespace

CreateAssetCommand::CreateAssetCommand(AssetDatabase* database, fs::path path, std::string content)
    : m_database(database), m_path(std::move(path)), m_content(std::move(content)) {}

bool CreateAssetCommand::execute() {
    std::error_code error;
    if (fs::exists(m_path, error)) return false;
    if (!m_path.parent_path().empty()) {
        fs::create_directories(m_path.parent_path(), error);
        if (error) return false;
    }
    if (!FileSystem::write_text_atomically(m_path.string(), m_content)) return false;
    if (!m_initialized) {
        if (!m_database || !m_database->import_asset(m_path).has_value()) {
            fs::remove(m_path, error);
            return false;
        }
        m_initialized = true;
    }
    rescan(m_database);
    return true;
}

bool CreateAssetCommand::undo() {
    std::error_code error;
    if (!fs::remove(m_path, error) || error) return false;
    if (m_database) static_cast<void>(m_database->remove_asset(m_path));
    rescan(m_database);
    return true;
}

std::string CreateAssetCommand::get_description() const {
    return "Create Asset " + m_path.filename().string();
}

MoveAssetCommand::MoveAssetCommand(AssetDatabase* database, fs::path source, fs::path destination)
    : m_database(database), m_source(std::move(source)), m_destination(std::move(destination)) {}

bool MoveAssetCommand::move_pair(const fs::path& from, const fs::path& to) const {
    std::error_code error;
    if (!fs::is_regular_file(from, error) || error || fs::exists(to, error)) return false;
    fs::create_directories(to.parent_path(), error);
    if (error) return false;
    fs::rename(from, to, error);
    if (error) return false;
    if (m_database && !m_database->move_asset(from, to)) {
        std::error_code rollback_error; fs::rename(to, from, rollback_error); return false;
    }
    return true;
}

bool MoveAssetCommand::execute() {
    if (!move_pair(m_source, m_destination)) return false;
    rescan(m_database);
    return true;
}

bool MoveAssetCommand::undo() {
    if (!move_pair(m_destination, m_source)) return false;
    rescan(m_database);
    return true;
}

std::string MoveAssetCommand::get_description() const {
    return "Move Asset " + m_source.filename().string();
}

DeleteAssetCommand::DeleteAssetCommand(AssetDatabase* database, fs::path path)
    : m_database(database), m_path(std::move(path)) {}

bool DeleteAssetCommand::execute() {
    std::error_code error;
    if (!m_captured) {
        if (!fs::is_regular_file(m_path, error) || error) return false;
        m_source_bytes = FileSystem::read_bytes(m_path.string());
        m_captured = true;
    }
    if (!fs::remove(m_path, error) || error) return false;
    if (m_database) static_cast<void>(m_database->remove_asset(m_path));
    rescan(m_database);
    return true;
}

bool DeleteAssetCommand::undo() {
    std::error_code error;
    if (fs::exists(m_path, error) || !write_bytes(m_path, m_source_bytes)) return false;
    rescan(m_database);
    return true;
}

std::string DeleteAssetCommand::get_description() const {
    return "Delete Asset " + m_path.filename().string();
}

} // namespace Engine
