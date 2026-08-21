#pragma once

#include "IEditorCommand.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

class AssetDatabase;

// Editor-only filesystem commands. They treat an asset and its .meta sidecar
// as one unit, so AssetHandle identity survives rename/move and undo/redo.
class CreateAssetCommand final : public IEditorCommand {
public:
    CreateAssetCommand(AssetDatabase* database, std::filesystem::path path, std::string content);
    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override;
    [[nodiscard]] EditorCommandScope get_scope() const override { return EditorCommandScope::EditorSession; }

private:
    AssetDatabase* m_database{nullptr};
    std::filesystem::path m_path;
    std::string m_content;
    std::string m_metadata;
    bool m_initialized{false};
};

class ModifyAssetCommand final : public IEditorCommand {
public:
    ModifyAssetCommand(
        AssetDatabase* database,
        std::filesystem::path path,
        std::string before,
        std::string after,
        std::string description = "Modify Asset"
    );
    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override { return m_description; }
    [[nodiscard]] EditorCommandScope get_scope() const override { return EditorCommandScope::EditorSession; }

private:
    bool apply(const std::string& content);

    AssetDatabase* m_database{nullptr};
    std::filesystem::path m_path;
    std::string m_before;
    std::string m_after;
    std::string m_description;
};

class MoveAssetCommand final : public IEditorCommand {
public:
    MoveAssetCommand(AssetDatabase* database, std::filesystem::path source, std::filesystem::path destination);
    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override;
    [[nodiscard]] EditorCommandScope get_scope() const override { return EditorCommandScope::EditorSession; }

private:
    bool move_pair(const std::filesystem::path& from, const std::filesystem::path& to) const;

    AssetDatabase* m_database{nullptr};
    std::filesystem::path m_source;
    std::filesystem::path m_destination;
};

class DeleteAssetCommand final : public IEditorCommand {
public:
    DeleteAssetCommand(AssetDatabase* database, std::filesystem::path path);
    bool execute() override;
    bool undo() override;
    [[nodiscard]] std::string get_description() const override;
    [[nodiscard]] EditorCommandScope get_scope() const override { return EditorCommandScope::EditorSession; }

private:
    AssetDatabase* m_database{nullptr};
    std::filesystem::path m_path;
    std::vector<std::uint8_t> m_source_bytes;
    std::vector<std::uint8_t> m_metadata_bytes;
    bool m_had_metadata{false};
    bool m_captured{false};
};

} // namespace Engine
