#include "BinarySerializer.hpp"
#include <array>

namespace Engine {

// ---------------------------------------------------------------------------
// CRC32 Lookup Table & Implementation
// ---------------------------------------------------------------------------
static constexpr std::array<uint32_t, 256> generate_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j) {
            crc = (crc & 1) ? (0xEDB88320 ^ (crc >> 1)) : (crc >> 1);
        }
        table[i] = crc;
    }
    return table;
}

static constexpr auto s_crc32_table = generate_crc32_table();

uint32_t Checksum::crc32(const uint8_t* data, size_t length) {
    if (!data || length == 0) return 0;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        uint8_t lookup_index = static_cast<uint8_t>((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ s_crc32_table[lookup_index];
    }
    return crc ^ 0xFFFFFFFF;
}

// ---------------------------------------------------------------------------
// Base64 Implementation
// ---------------------------------------------------------------------------
static constexpr char s_base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string Base64::encode(const uint8_t* data, size_t length) {
    if (!data || length == 0) return "";

    std::string ret;
    ret.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        uint32_t octet_a = data[i];
        uint32_t octet_b = (i + 1 < length) ? data[i + 1] : 0;
        uint32_t octet_c = (i + 2 < length) ? data[i + 2] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        ret.push_back(s_base64_chars[(triple >> 18) & 0x3F]);
        ret.push_back(s_base64_chars[(triple >> 12) & 0x3F]);
        ret.push_back((i + 1 < length) ? s_base64_chars[(triple >> 6) & 0x3F] : '=');
        ret.push_back((i + 2 < length) ? s_base64_chars[triple & 0x3F] : '=');
    }
    return ret;
}

std::vector<uint8_t> Base64::decode(std::string_view base64_str) {
    auto b64_index = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    std::vector<uint8_t> ret;
    ret.reserve(base64_str.size() * 3 / 4);

    uint32_t buffer = 0;
    int bits = 0;

    for (char c : base64_str) {
        if (c == '=') break;
        int val = b64_index(c);
        if (val < 0) continue; // Skip whitespace / newline / unrecognized
        buffer = (buffer << 6) | static_cast<uint32_t>(val);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            ret.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }

    return ret;
}

// ---------------------------------------------------------------------------
// BinaryWriter
// ---------------------------------------------------------------------------
void BinaryWriter::write_string(std::string_view str) {
    write_u32(static_cast<uint32_t>(str.size()));
    if (!str.empty()) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(str.data());
        m_buffer.insert(m_buffer.end(), ptr, ptr + str.size());
    }
}

void BinaryWriter::write_vec2(const glm::vec2& v) {
    write_float(v.x);
    write_float(v.y);
}

void BinaryWriter::write_vec3(const glm::vec3& v) {
    write_float(v.x);
    write_float(v.y);
    write_float(v.z);
}

void BinaryWriter::write_vec4(const glm::vec4& v) {
    write_float(v.x);
    write_float(v.y);
    write_float(v.z);
    write_float(v.w);
}

void BinaryWriter::write_raw(const void* data, size_t size) {
    if (data && size > 0) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
        m_buffer.insert(m_buffer.end(), ptr, ptr + size);
    }
}

// ---------------------------------------------------------------------------
// BinaryReader
// ---------------------------------------------------------------------------
BinaryReader::BinaryReader(const uint8_t* data, size_t size)
    : m_data(data), m_size(size), m_cursor(0) {}

BinaryReader::BinaryReader(const std::vector<uint8_t>& buffer)
    : m_owned_buffer(buffer), m_data(m_owned_buffer.data()), m_size(m_owned_buffer.size()), m_cursor(0) {}

BinaryReader::BinaryReader(std::vector<uint8_t>&& buffer)
    : m_owned_buffer(std::move(buffer)), m_data(m_owned_buffer.data()), m_size(m_owned_buffer.size()), m_cursor(0) {}

BinaryReader::BinaryReader(const BinaryReader& other)
    : m_owned_buffer(other.m_owned_buffer),
      m_data(other.m_owned_buffer.empty() ? other.m_data : m_owned_buffer.data()),
      m_size(other.m_size),
      m_cursor(other.m_cursor) {}

BinaryReader::BinaryReader(BinaryReader&& other) noexcept
    : m_owned_buffer(std::move(other.m_owned_buffer)),
      m_data(m_owned_buffer.empty() ? other.m_data : m_owned_buffer.data()),
      m_size(other.m_size),
      m_cursor(other.m_cursor) {
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_cursor = 0;
}

BinaryReader& BinaryReader::operator=(const BinaryReader& other) {
    if (this != &other) {
        m_owned_buffer = other.m_owned_buffer;
        m_data = m_owned_buffer.empty() ? other.m_data : m_owned_buffer.data();
        m_size = other.m_size;
        m_cursor = other.m_cursor;
    }
    return *this;
}

BinaryReader& BinaryReader::operator=(BinaryReader&& other) noexcept {
    if (this != &other) {
        m_owned_buffer = std::move(other.m_owned_buffer);
        m_data = m_owned_buffer.empty() ? other.m_data : m_owned_buffer.data();
        m_size = other.m_size;
        m_cursor = other.m_cursor;
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_cursor = 0;
    }
    return *this;
}

uint8_t  BinaryReader::read_u8()  { return _read_pod<uint8_t>(); }
int8_t   BinaryReader::read_i8()  { return _read_pod<int8_t>(); }
uint16_t BinaryReader::read_u16() { return _read_pod<uint16_t>(); }
int16_t  BinaryReader::read_i16() { return _read_pod<int16_t>(); }
uint32_t BinaryReader::read_u32() { return _read_pod<uint32_t>(); }
int32_t  BinaryReader::read_i32() { return _read_pod<int32_t>(); }
uint64_t BinaryReader::read_u64() { return _read_pod<uint64_t>(); }
int64_t  BinaryReader::read_i64() { return _read_pod<int64_t>(); }

float    BinaryReader::read_float()  { return _read_pod<float>(); }
double   BinaryReader::read_double() { return _read_pod<double>(); }
bool     BinaryReader::read_bool()   { return read_u8() != 0; }

std::string BinaryReader::read_string() {
    uint32_t length = read_u32();
    if (length == 0 || m_cursor + length > m_size) {
        return "";
    }
    std::string str(reinterpret_cast<const char*>(m_data + m_cursor), length);
    m_cursor += length;
    return str;
}

glm::vec2 BinaryReader::read_vec2() {
    float x = read_float();
    float y = read_float();
    return glm::vec2(x, y);
}

glm::vec3 BinaryReader::read_vec3() {
    float x = read_float();
    float y = read_float();
    float z = read_float();
    return glm::vec3(x, y, z);
}

glm::vec4 BinaryReader::read_vec4() {
    float x = read_float();
    float y = read_float();
    float z = read_float();
    float w = read_float();
    return glm::vec4(x, y, z, w);
}

bool BinaryReader::read_raw(void* out_data, size_t size) {
    if (!out_data || size == 0 || m_cursor + size > m_size) {
        return false;
    }
    std::memcpy(out_data, m_data + m_cursor, size);
    m_cursor += size;
    return true;
}

} // namespace Engine
