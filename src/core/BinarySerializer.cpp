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
    std::string ret;
    ret.reserve(((length + 2) / 3) * 4);

    size_t i = 0;
    while (i < length) {
        uint32_t octet_a = i < length ? data[i++] : 0;
        uint32_t octet_b = i < length ? data[i++] : 0;
        uint32_t octet_c = i < length ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        ret.push_back(s_base64_chars[(triple >> 18) & 0x3F]);
        ret.push_back(s_base64_chars[(triple >> 12) & 0x3F]);
        ret.push_back((i > length + 1) ? '=' : s_base64_chars[(triple >> 6) & 0x3F]);
        ret.push_back((i > length) ? '=' : s_base64_chars[triple & 0x3F]);
    }
    return ret;
}

std::vector<uint8_t> Base64::decode(std::string_view base64_str) {
    auto is_base64 = [](char c) -> bool {
        return (isalnum(c) || (c == '+') || (c == '/'));
    };

    auto b64_index = [](char c) -> uint8_t {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return 0;
    };

    std::vector<uint8_t> ret;
    int in_len = static_cast<int>(base64_str.size());
    int i = 0, in_ = 0;
    uint8_t char_array_4[4], char_array_3[3];

    while (in_len-- && (base64_str[in_] != '=') && is_base64(base64_str[in_])) {
        char_array_4[i++] = base64_str[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                char_array_4[i] = b64_index(char_array_4[i]);
            }
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; i < 3; i++) ret.push_back(char_array_3[i]);
            i = 0;
        }
    }

    if (i) {
        for (int j = i; j < 4; j++) char_array_4[j] = 0;
        for (int j = 0; j < 4; j++) char_array_4[j] = b64_index(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (int j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
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
    : m_data(buffer.data()), m_size(buffer.size()), m_cursor(0) {}

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
