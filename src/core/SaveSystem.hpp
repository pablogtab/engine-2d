#pragma once

#include "FileSystem.hpp"
#include "BinarySerializer.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace Engine {

// ---------------------------------------------------------------------------
// SaveHeader
// 24-byte binary header placed at the beginning of every .sav file.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct SaveHeader {
    char     magic[4]{'E', '2', 'D', 'S'}; // "E2DS" (Engine 2D Save)
    uint32_t version{1};                   // Data structure version
    uint64_t timestamp{0};                 // Unix timestamp of save
    uint32_t checksum{0};                  // CRC32 of raw payload
    uint32_t payload_size{0};              // Uncompressed byte size
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// SaveSystem
// Unified persistence system providing:
// 1. JSON file reading/writing (for levels, maps, shader presets).
// 2. Binary save game slot management with Magic header and CRC32 verification.
// ---------------------------------------------------------------------------
class SaveSystem {
public:
    static constexpr uint32_t CURRENT_SAVE_VERSION = 1;

    // --- JSON Operations (Levels, Presets, Configs) ---
    static bool write_json(const std::string& path, const nlohmann::json& data, bool pretty = true);
    static bool read_json(const std::string& path, nlohmann::json& out_data);

    // --- Binary Save Game Slots (e.g. "save://slot_1.sav") ---
    // Writes raw payload with a 24-byte SaveHeader + CRC32 checksum.
    // If base64_encode is true, wraps the binary stream in Base64 text.
    static bool save_slot(int slot_id, const BinaryWriter& data, bool base64_encode = false);
    static bool load_slot(int slot_id, BinaryReader& out_reader);

    static bool save_file(const std::string& path, const BinaryWriter& data, bool base64_encode = false);
    static bool load_file(const std::string& path, BinaryReader& out_reader);

    // --- Slot Queries & Management ---
    [[nodiscard]] static bool slot_exists(int slot_id);
    static bool delete_slot(int slot_id);
    [[nodiscard]] static std::string get_slot_path(int slot_id);
    [[nodiscard]] static SaveHeader read_slot_header(int slot_id);
};

} // namespace Engine
