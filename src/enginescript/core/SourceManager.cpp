#include "SourceManager.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace EngineScript {

SourceManager::SourceManager(std::string project_root)
    : m_project_root(std::move(project_root)) {
    if (!m_project_root.empty()) {
        m_project_root = std::filesystem::path(m_project_root).lexically_normal().string();
    }
}

void SourceManager::set_project_root(std::string project_root) {
    m_project_root = std::filesystem::path(project_root).lexically_normal().string();
}

std::string SourceManager::normalize_path(const std::string& raw_path) const {
    std::filesystem::path p(raw_path);
    p = p.lexically_normal();
    if (!m_project_root.empty() && p.is_absolute()) {
        std::filesystem::path root(m_project_root);
        auto rel = std::filesystem::relative(p, root);
        if (!rel.empty() && rel.native().find("..") == std::string::npos) {
            return rel.generic_string();
        }
    }
    return p.generic_string();
}

bool SourceManager::is_within_project(const std::string& raw_path) const {
    if (m_project_root.empty()) return true;
    std::filesystem::path p(raw_path);
    if (!p.is_absolute()) {
        p = std::filesystem::path(m_project_root) / p;
    }
    p = p.lexically_normal();
    std::filesystem::path root(m_project_root);
    auto rel = std::filesystem::relative(p, root);
    return !rel.empty() && rel.native().find("..") == std::string::npos;
}

std::optional<std::string> SourceManager::resolve_import(const std::string& importing_file, const std::string& import_target) const {
    std::filesystem::path base_dir;
    const SourceFile* imp = get_file(importing_file);
    if (imp) {
        base_dir = std::filesystem::path(imp->path).parent_path();
    } else {
        base_dir = std::filesystem::path(importing_file).parent_path();
    }

    std::filesystem::path target_path = base_dir / import_target;
    if (!target_path.has_extension()) {
        target_path.replace_extension(".e2s");
    }
    target_path = target_path.lexically_normal();

    std::string norm = target_path.generic_string();
    if (!is_within_project(norm)) {
        return std::nullopt; // Escapes project boundary
    }
    return norm;
}

void SourceManager::compute_line_offsets(SourceFile& file) {
    file.line_offsets.clear();
    file.line_offsets.push_back(0); // Line 1 starts at byte 0

    const std::string& c = file.content;
    for (uint32_t i = 0; i < c.size(); ++i) {
        if (c[i] == '\n') {
            file.line_offsets.push_back(i + 1);
        } else if (c[i] == '\r') {
            if (i + 1 < c.size() && c[i + 1] == '\n') {
                file.line_offsets.push_back(i + 2);
                ++i;
            } else {
                file.line_offsets.push_back(i + 1);
            }
        }
    }
}

FileId SourceManager::register_source(std::string path, std::string content, bool is_virtual) {
    std::string norm_path = normalize_path(path);
    auto it = m_path_to_id.find(norm_path);
    if (it != m_path_to_id.end()) {
        // Update existing
        FileId id = it->second;
        SourceFile& f = m_files[id - 1];
        f.content = std::move(content);
        f.is_virtual = is_virtual;
        compute_line_offsets(f);
        return id;
    }

    FileId new_id = static_cast<FileId>(m_files.size() + 1);
    SourceFile sf;
    sf.id = new_id;
    sf.path = norm_path;
    if (!m_project_root.empty() && !std::filesystem::path(norm_path).is_absolute()) {
        sf.absolute_path = (std::filesystem::path(m_project_root) / norm_path).lexically_normal().string();
    } else {
        sf.absolute_path = std::filesystem::path(norm_path).lexically_normal().string();
    }
    sf.content = std::move(content);
    sf.is_virtual = is_virtual;
    compute_line_offsets(sf);

    m_files.push_back(std::move(sf));
    m_path_to_id[norm_path] = new_id;
    return new_id;
}

FileId SourceManager::load_file(const std::string& absolute_or_relative_path) {
    std::string norm_path = normalize_path(absolute_or_relative_path);
    auto it = m_path_to_id.find(norm_path);
    if (it != m_path_to_id.end()) {
        return it->second;
    }

    std::filesystem::path full_p(absolute_or_relative_path);
    if (!full_p.is_absolute() && !m_project_root.empty()) {
        full_p = std::filesystem::path(m_project_root) / full_p;
    }

    std::ifstream ifs(full_p, std::ios::in | std::ios::binary);
    if (!ifs) {
        return INVALID_FILE_ID;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    return register_source(norm_path, ss.str(), false);
}

bool SourceManager::update_virtual_file(FileId file_id, std::string new_content, uint32_t version) {
    if (file_id == INVALID_FILE_ID || file_id > m_files.size()) {
        return false;
    }
    SourceFile& f = m_files[file_id - 1];
    f.content = std::move(new_content);
    f.is_virtual = true;
    f.version = version;
    compute_line_offsets(f);
    return true;
}

bool SourceManager::update_virtual_file(const std::string& path, std::string new_content, uint32_t version) {
    FileId id = find_file_id(path);
    if (id == INVALID_FILE_ID) {
        register_source(path, std::move(new_content), true);
        return true;
    }
    return update_virtual_file(id, std::move(new_content), version);
}

const SourceFile* SourceManager::get_file(FileId id) const {
    if (id == INVALID_FILE_ID || id > m_files.size()) {
        return nullptr;
    }
    return &m_files[id - 1];
}

const SourceFile* SourceManager::get_file(const std::string& path) const {
    FileId id = find_file_id(path);
    return get_file(id);
}

FileId SourceManager::find_file_id(const std::string& path) const {
    std::string norm_path = normalize_path(path);
    auto it = m_path_to_id.find(norm_path);
    if (it != m_path_to_id.end()) {
        return it->second;
    }
    return INVALID_FILE_ID;
}

SourceLocation SourceManager::get_location(FileId file_id, uint32_t byte_offset) const {
    const SourceFile* sf = get_file(file_id);
    if (!sf) return {};

    SourceLocation loc;
    loc.file_id = file_id;
    loc.byte_offset = byte_offset;

    if (sf->line_offsets.empty()) {
        loc.line = 1;
        loc.column = byte_offset + 1;
        return loc;
    }

    // Binary search for the line offset
    auto it = std::upper_bound(sf->line_offsets.begin(), sf->line_offsets.end(), byte_offset);
    size_t line_idx = std::distance(sf->line_offsets.begin(), it) - 1;
    loc.line = static_cast<uint32_t>(line_idx + 1);

    uint32_t line_start = sf->line_offsets[line_idx];
    loc.column = (byte_offset >= line_start) ? (byte_offset - line_start + 1) : 1;
    return loc;
}

SourceSpan SourceManager::get_span(FileId file_id, uint32_t start_offset, uint32_t end_offset) const {
    SourceLocation start_loc = get_location(file_id, start_offset);
    SourceLocation end_loc = get_location(file_id, end_offset);

    SourceSpan span;
    span.file_id = file_id;
    span.start_offset = start_offset;
    span.end_offset = end_offset;
    span.start_line = start_loc.line;
    span.start_column = start_loc.column;
    span.end_line = end_loc.line;
    span.end_column = end_loc.column;
    return span;
}

std::string SourceManager::get_line_content(FileId file_id, uint32_t line_number) const {
    const SourceFile* sf = get_file(file_id);
    if (!sf || line_number == 0 || line_number > sf->line_offsets.size()) {
        return "";
    }

    uint32_t start = sf->line_offsets[line_number - 1];
    uint32_t end = (line_number < sf->line_offsets.size()) ? sf->line_offsets[line_number] : static_cast<uint32_t>(sf->content.size());

    std::string line = sf->content.substr(start, end - start);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }
    return line;
}

std::string SourceManager::get_snippet(const SourceSpan& span, size_t context_lines) const {
    const SourceFile* sf = get_file(span.file_id);
    if (!sf || !span.is_valid()) return "";

    std::ostringstream ss;
    uint32_t start_l = (span.start_line > context_lines) ? static_cast<uint32_t>(span.start_line - context_lines) : 1;
    uint32_t end_l = std::min(static_cast<uint32_t>(span.end_line + context_lines), static_cast<uint32_t>(sf->line_offsets.size()));

    for (uint32_t l = start_l; l <= end_l; ++l) {
        std::string line = get_line_content(span.file_id, l);
        ss << std::format("{:4d} | {}\n", l, line);

        if (l >= span.start_line && l <= span.end_line) {
            ss << "     | ";
            size_t col_start = (l == span.start_line) ? span.start_column : 1;
            size_t col_end = (l == span.end_line) ? span.end_column : line.size() + 1;
            if (col_end < col_start) col_end = col_start + 1;

            for (size_t c = 1; c < col_start; ++c) {
                ss << " ";
            }
            for (size_t c = col_start; c < col_end; ++c) {
                ss << "^";
            }
            ss << "\n";
        }
    }
    return ss.str();
}

} // namespace EngineScript
