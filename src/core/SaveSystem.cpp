#include "SaveSystem.hpp"
#include "Log.hpp"
#include <chrono>
#include <cstring>

namespace Engine {

bool SaveSystem::write_json(const std::string& path, const nlohmann::json& data, bool pretty) {
    try {
        std::string json_str = pretty ? data.dump(2) : data.dump();
        return FileSystem::write_text(path, json_str);
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[SAVESYSTEM] Error serializing JSON to '{}': {}", path, e.what());
        return false;
    }
}

bool SaveSystem::read_json(const std::string& path, nlohmann::json& out_data) {
    std::string text = FileSystem::read_text(path);
    if (text.empty()) {
        return false;
    }

    try {
        out_data = nlohmann::json::parse(text, nullptr, false);
        if (out_data.is_discarded()) {
            ENGINE_LOG_ERROR("[SAVESYSTEM] Corrupted JSON format in '{}'", path);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        ENGINE_LOG_ERROR("[SAVESYSTEM] Exception parsing JSON from '{}': {}", path, e.what());
        return false;
    }
}

std::string SaveSystem::get_slot_path(int slot_id) {
    return "save://slot_" + std::to_string(slot_id) + ".sav";
}

bool SaveSystem::slot_exists(int slot_id) {
    return FileSystem::exists(get_slot_path(slot_id));
}

bool SaveSystem::delete_slot(int slot_id) {
    return FileSystem::remove(get_slot_path(slot_id));
}

bool SaveSystem::save_slot(int slot_id, const BinaryWriter& data, bool base64_encode) {
    return save_file(get_slot_path(slot_id), data, base64_encode);
}

bool SaveSystem::load_slot(int slot_id, BinaryReader& out_reader) {
    return load_file(get_slot_path(slot_id), out_reader);
}

bool SaveSystem::save_file(const std::string& path, const BinaryWriter& data, bool base64_encode) {
    // 1. Build Header
    SaveHeader header;
    header.version = CURRENT_SAVE_VERSION;
    header.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    header.payload_size = static_cast<uint32_t>(data.get_size());
    header.checksum = Checksum::crc32(data.get_data(), data.get_size());

    // 2. Assemble Full Binary Stream: [Header] + [Payload]
    std::vector<uint8_t> full_buffer;
    full_buffer.reserve(sizeof(SaveHeader) + data.get_size());

    const uint8_t* header_ptr = reinterpret_cast<const uint8_t*>(&header);
    full_buffer.insert(full_buffer.end(), header_ptr, header_ptr + sizeof(SaveHeader));
    full_buffer.insert(full_buffer.end(), data.get_data(), data.get_data() + data.get_size());

    // 3. Write to File (Raw Binary or Base64 Text)
    bool success = false;
    if (base64_encode) {
        std::string b64 = Base64::encode(full_buffer);
        success = FileSystem::write_text(path, b64);
    } else {
        success = FileSystem::write_bytes(path, full_buffer);
    }

    if (success) {
        ENGINE_LOG_INFO("[SAVESYSTEM] Successfully saved '{}' (Payload: {} bytes, CRC32: 0x{:08X})",
                        path, header.payload_size, header.checksum);
    } else {
        ENGINE_LOG_ERROR("[SAVESYSTEM] Failed to save '{}'", path);
    }
    return success;
}

bool SaveSystem::load_file(const std::string& path, BinaryReader& out_reader) {
    if (!FileSystem::exists(path)) {
        ENGINE_LOG_WARN("[SAVESYSTEM] Save file not found: '{}'", path);
        return false;
    }

    std::vector<uint8_t> raw_bytes = FileSystem::read_bytes(path);
    if (raw_bytes.empty()) {
        ENGINE_LOG_ERROR("[SAVESYSTEM] Corrupted save file (empty): '{}'", path);
        return false;
    }

    // Check if the stream starts directly with the binary magic header "E2DS"
    bool is_raw_binary = (raw_bytes.size() >= sizeof(SaveHeader) &&
                          raw_bytes[0] == 'E' && raw_bytes[1] == '2' &&
                          raw_bytes[2] == 'D' && raw_bytes[3] == 'S');

    // If not raw binary, try decoding it as Base64 text
    if (!is_raw_binary) {
        std::string text = FileSystem::read_text(path);
        if (!text.empty()) {
            raw_bytes = Base64::decode(text);
        }
    }

    if (raw_bytes.size() < sizeof(SaveHeader)) {
        ENGINE_LOG_ERROR("[SAVESYSTEM] Corrupted save file (too small or invalid encoding): '{}'", path);
        return false;
    }

    // 1. Read & Validate Header
    SaveHeader header;
    std::memcpy(&header, raw_bytes.data(), sizeof(SaveHeader));

    if (header.magic[0] != 'E' || header.magic[1] != '2' ||
        header.magic[2] != 'D' || header.magic[3] != 'S') {
        ENGINE_LOG_ERROR("[SAVESYSTEM] Invalid save game magic header in '{}'", path);
        return false;
    }

    // 2. Validate Checksum & Integrity
    const uint8_t* payload_ptr = raw_bytes.data() + sizeof(SaveHeader);
    size_t actual_payload_size = raw_bytes.size() - sizeof(SaveHeader);

    if (actual_payload_size != header.payload_size) {
        ENGINE_LOG_ERROR("[SAVESYSTEM] Payload size mismatch in '{}' (Expected: {}, Got: {})",
                         path, header.payload_size, actual_payload_size);
        return false;
    }

    uint32_t computed_crc = Checksum::crc32(payload_ptr, actual_payload_size);
    if (computed_crc != header.checksum) {
        ENGINE_LOG_ERROR("[SAVESYSTEM] Checksum failure! Save file '{}' is corrupt or tampered (Expected: 0x{:08X}, Computed: 0x{:08X})",
                         path, header.checksum, computed_crc);
        return false;
    }

    // 3. Initialize BinaryReader with an owned copy of the validated payload buffer
    std::vector<uint8_t> payload(payload_ptr, payload_ptr + actual_payload_size);
    out_reader = BinaryReader(std::move(payload));
    ENGINE_LOG_INFO("[SAVESYSTEM] Loaded save '{}' (Version: {}, Size: {} bytes, CRC32: 0x{:08X})",
                    path, header.version, header.payload_size, header.checksum);
    return true;
}

SaveHeader SaveSystem::read_slot_header(int slot_id) {
    std::string path = get_slot_path(slot_id);
    if (!FileSystem::exists(path)) {
        return {};
    }

    std::vector<uint8_t> bytes = FileSystem::read_bytes(path);
    bool is_raw_binary = (bytes.size() >= sizeof(SaveHeader) &&
                          bytes[0] == 'E' && bytes[1] == '2' &&
                          bytes[2] == 'D' && bytes[3] == 'S');

    if (!is_raw_binary) {
        std::string text = FileSystem::read_text(path);
        bytes = Base64::decode(text);
    }

    if (bytes.size() >= sizeof(SaveHeader)) {
        SaveHeader h;
        std::memcpy(&h, bytes.data(), sizeof(SaveHeader));
        if (h.magic[0] == 'E' && h.magic[1] == '2' && h.magic[2] == 'D' && h.magic[3] == 'S') {
            return h;
        }
    }
    return {};
}

} // namespace Engine
