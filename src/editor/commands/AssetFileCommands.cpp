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

CreateBinaryAssetCommand::CreateBinaryAssetCommand(
    AssetDatabase* database,
    fs::path path,
    std::vector<std::uint8_t> content
) : m_database(database), m_path(std::move(path)), m_content(std::move(content)) {}

CreateBinaryAssetCommand::~CreateBinaryAssetCommand() {
    if (m_database && m_initialized && !m_applied && !m_had_database_record) {
        static_cast<void>(m_database->discard_missing_asset_record(m_path));
    }
}

bool CreateBinaryAssetCommand::execute() {
    std::error_code error;
    if (!m_database || m_content.empty() || fs::exists(m_path, error) || error) return false;
    if (!m_initialized) m_had_database_record = m_database->find_by_path(m_path) != nullptr;
    fs::create_directories(m_path.parent_path(), error);
    if (error) return false;
    const fs::path temporary = m_path.string() + ".ai-import.tmp";
    fs::remove(temporary, error);
    error.clear();
    if (!FileSystem::write_bytes(temporary.string(), m_content)) return false;
    fs::rename(temporary, m_path, error);
    if (error) {
        fs::remove(temporary, error);
        return false;
    }
    if (!m_initialized) {
        if (!m_database->import_asset(m_path).has_value()) {
            fs::remove(m_path, error);
            return false;
        }
        m_initialized = true;
    }
    rescan(m_database);
    m_applied = true;
    return true;
}

bool CreateBinaryAssetCommand::undo() {
    std::error_code error;
    if (!fs::remove(m_path, error) || error) return false;
    static_cast<void>(m_database->remove_asset(m_path));
    rescan(m_database);
    m_applied = false;
    return true;
}

std::string CreateBinaryAssetCommand::get_description() const {
    return "Create Binary Asset " + m_path.filename().string();
}

ModifyAssetCommand::ModifyAssetCommand(
    AssetDatabase* database,
    fs::path path,
    std::string before,
    std::string after,
    std::string description,
    std::function<void()> on_before,
    std::function<void()> on_after
) : m_database(database),
    m_path(std::move(path)),
    m_before(std::move(before)),
    m_after(std::move(after)),
    m_description(std::move(description)),
    m_on_before(std::move(on_before)),
    m_on_after(std::move(on_after)) {}

bool ModifyAssetCommand::apply(const std::string& content) {
    std::error_code error;
    if (!fs::is_regular_file(m_path, error) || error) return false;
    const std::string rollback = FileSystem::read_text(m_path.string());
    if (!FileSystem::write_text_atomically(m_path.string(), content)) return false;
    if (m_database && !m_database->import_asset(m_path).has_value()) {
        static_cast<void>(FileSystem::write_text_atomically(m_path.string(), rollback));
        static_cast<void>(m_database->import_asset(m_path));
        return false;
    }
    rescan(m_database);
    return true;
}

bool ModifyAssetCommand::execute() {
    if (!apply(m_after)) return false;
    if (m_on_after) m_on_after();
    return true;
}
bool ModifyAssetCommand::undo() {
    if (!apply(m_before)) return false;
    if (m_on_before) m_on_before();
    return true;
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
