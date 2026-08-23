#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <format>
#include <algorithm>

namespace EngineScript {

using FileId = uint32_t;
constexpr FileId INVALID_FILE_ID = 0;

struct SourceLocation {
    FileId file_id = INVALID_FILE_ID;
    uint32_t line = 1;      // 1-indexed
    uint32_t column = 1;    // 1-indexed
    uint32_t byte_offset = 0;

    bool is_valid() const { return file_id != INVALID_FILE_ID; }

    bool operator==(const SourceLocation& other) const = default;
};

struct SourceSpan {
    FileId file_id = INVALID_FILE_ID;
    uint32_t start_offset = 0;
    uint32_t end_offset = 0;
    uint32_t start_line = 1;
    uint32_t start_column = 1;
    uint32_t end_line = 1;
    uint32_t end_column = 1;

    bool is_valid() const { return file_id != INVALID_FILE_ID; }
    uint32_t length() const { return end_offset >= start_offset ? end_offset - start_offset : 0; }

    bool operator==(const SourceSpan& other) const = default;

    SourceSpan merge(const SourceSpan& other) const {
        if (!is_valid()) return other;
        if (!other.is_valid()) return *this;
        if (file_id != other.file_id) return *this;

        SourceSpan result;
        result.file_id = file_id;
        result.start_offset = std::min(start_offset, other.start_offset);
        result.end_offset = std::max(end_offset, other.end_offset);

        if (start_offset <= other.start_offset) {
            result.start_line = start_line;
            result.start_column = start_column;
        } else {
            result.start_line = other.start_line;
            result.start_column = other.start_column;
        }

        if (end_offset >= other.end_offset) {
            result.end_line = end_line;
            result.end_column = end_column;
        } else {
            result.end_line = other.end_line;
            result.end_column = other.end_column;
        }
        return result;
    }

    std::string to_string() const {
        return std::format("{}:{}-{}:{}", start_line, start_column, end_line, end_column);
    }
};

} // namespace EngineScript
