#pragma once

#include "SourceLocation.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <optional>
#include <string_view>

namespace EngineScript {

struct SourceFile {
    FileId id = INVALID_FILE_ID;
    std::string path;              // Project-relative path
    std::string absolute_path;     // Full filesystem path
    std::string content;
    std::vector<uint32_t> line_offsets; // Byte offsets of the start of each line (0-indexed line numbers)
    bool is_virtual = false;
    uint32_t version = 0;          // Document version for LSP edits
};

class SourceManager {
public:
    explicit SourceManager(std::string project_root = "");

    void set_project_root(std::string project_root);
    const std::string& project_root() const { return m_project_root; }

    FileId register_source(std::string path, std::string content, bool is_virtual = false);
    FileId load_file(const std::string& absolute_or_relative_path);
    bool update_virtual_file(FileId file_id, std::string new_content, uint32_t version = 0);
    bool update_virtual_file(const std::string& path, std::string new_content, uint32_t version = 0);

    const SourceFile* get_file(FileId id) const;
    const SourceFile* get_file(const std::string& path) const;
    FileId find_file_id(const std::string& path) const;

    SourceLocation get_location(FileId file_id, uint32_t byte_offset) const;
    SourceSpan get_span(FileId file_id, uint32_t start_offset, uint32_t end_offset) const;

    std::string get_line_content(FileId file_id, uint32_t line_number) const;
    std::string get_snippet(const SourceSpan& span, size_t context_lines = 1) const;

    std::string normalize_path(const std::string& raw_path) const;
    bool is_within_project(const std::string& raw_path) const;
    std::optional<std::string> resolve_import(const std::string& importing_file, const std::string& import_target) const;

    const std::vector<SourceFile>& all_files() const { return m_files; }

private:
    void compute_line_offsets(SourceFile& file);

    std::string m_project_root;
    std::vector<SourceFile> m_files;
    std::unordered_map<std::string, FileId> m_path_to_id;
};

} // namespace EngineScript
