#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace Engine {

// ---------------------------------------------------------------------------
// Checksum Utility (CRC32)
// ---------------------------------------------------------------------------
class Checksum {
public:
    static uint32_t crc32(const uint8_t* data, size_t length);
    static uint32_t crc32(const std::vector<uint8_t>& buffer) {
        return crc32(buffer.data(), buffer.size());
    }
};

// ---------------------------------------------------------------------------
// Base64 Codec
// ---------------------------------------------------------------------------
class Base64 {
public:
    static std::string encode(const uint8_t* data, size_t length);
    static std::string encode(const std::vector<uint8_t>& buffer) {
        return encode(buffer.data(), buffer.size());
    }
    static std::vector<uint8_t> decode(std::string_view base64_str);
};

// ---------------------------------------------------------------------------
// BinaryWriter
// High performance memory byte buffer stream for writing primitive types,
// strings, and GLM vectors.
// ---------------------------------------------------------------------------
class BinaryWriter {
public:
    BinaryWriter() = default;
    explicit BinaryWriter(size_t initial_capacity) { m_buffer.reserve(initial_capacity); }

    void write_u8(uint8_t val)   { _write_pod(val); }
    void write_i8(int8_t val)    { _write_pod(val); }
    void write_u16(uint16_t val) { _write_pod(val); }
    void write_i16(int16_t val)  { _write_pod(val); }
    void write_u32(uint32_t val) { _write_pod(val); }
    void write_i32(int32_t val)  { _write_pod(val); }
    void write_u64(uint64_t val) { _write_pod(val); }
    void write_i64(int64_t val)  { _write_pod(val); }

    void write_float(float val)   { _write_pod(val); }
    void write_double(double val) { _write_pod(val); }
    void write_bool(bool val)     { write_u8(val ? 1 : 0); }

    void write_string(std::string_view str);
    void write_vec2(const glm::vec2& v);
    void write_vec3(const glm::vec3& v);
    void write_vec4(const glm::vec4& v);

    void write_raw(const void* data, size_t size);

    [[nodiscard]] const uint8_t* get_data() const { return m_buffer.data(); }
    [[nodiscard]] size_t get_size() const { return m_buffer.size(); }
    [[nodiscard]] const std::vector<uint8_t>& get_buffer() const { return m_buffer; }
    [[nodiscard]] std::vector<uint8_t>& get_buffer() { return m_buffer; }

    void clear() { m_buffer.clear(); }

private:
    template<typename T>
    void _write_pod(const T& val) {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable for _write_pod");
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&val);
        m_buffer.insert(m_buffer.end(), ptr, ptr + sizeof(T));
    }

    std::vector<uint8_t> m_buffer;
};

// ---------------------------------------------------------------------------
// BinaryReader
// Safe sequential reader with bounds checking over a binary memory buffer.
// ---------------------------------------------------------------------------
class BinaryReader {
public:
    BinaryReader() = default;
    BinaryReader(const uint8_t* data, size_t size);
    explicit BinaryReader(const std::vector<uint8_t>& buffer);
    explicit BinaryReader(std::vector<uint8_t>&& buffer);

    BinaryReader(const BinaryReader& other);
    BinaryReader(BinaryReader&& other) noexcept;
    BinaryReader& operator=(const BinaryReader& other);
    BinaryReader& operator=(BinaryReader&& other) noexcept;

    [[nodiscard]] uint8_t  read_u8();
    [[nodiscard]] int8_t   read_i8();
    [[nodiscard]] uint16_t read_u16();
    [[nodiscard]] int16_t  read_i16();
    [[nodiscard]] uint32_t read_u32();
    [[nodiscard]] int32_t  read_i32();
    [[nodiscard]] uint64_t read_u64();
    [[nodiscard]] int64_t  read_i64();

    [[nodiscard]] float    read_float();
    [[nodiscard]] double   read_double();
    [[nodiscard]] bool     read_bool();

    [[nodiscard]] std::string read_string();
    [[nodiscard]] glm::vec2   read_vec2();
    [[nodiscard]] glm::vec3   read_vec3();
    [[nodiscard]] glm::vec4   read_vec4();

    bool read_raw(void* out_data, size_t size);

    [[nodiscard]] size_t get_position() const { return m_cursor; }
    void set_position(size_t pos) { m_cursor = std::min(pos, m_size); }
    [[nodiscard]] size_t get_size() const { return m_size; }
    [[nodiscard]] size_t get_remaining_bytes() const { return (m_cursor < m_size) ? (m_size - m_cursor) : 0; }
    [[nodiscard]] bool is_eof() const { return m_cursor >= m_size; }
    [[nodiscard]] bool has_bytes(size_t count) const { return m_cursor + count <= m_size; }

private:
    template<typename T>
    T _read_pod() {
        static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable for _read_pod");
        if (!m_data || m_cursor + sizeof(T) > m_size) {
            return T{};
        }
        T val;
        std::memcpy(&val, m_data + m_cursor, sizeof(T));
        m_cursor += sizeof(T);
        return val;
    }

    std::vector<uint8_t> m_owned_buffer;
    const uint8_t* m_data{nullptr};
    size_t m_size{0};
    size_t m_cursor{0};
};

} // namespace Engine
