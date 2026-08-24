#pragma once

#include "AssetDatabase.hpp"
#include "AssetManager.hpp"
#include "FileSystem.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

class RuntimeAssetCache {
public:
    [[nodiscard]] bool preload(const AssetDatabase& database, AssetHandle handle, SDL_Renderer* renderer, std::string& error) {
        const AssetRecord* record = database.find(handle);
        if (!record || record->status != AssetStatus::Ready) {
            error = "Asset is not ready";
            return false;
        }
        const std::string path = database.resolve(handle);
        if (path.empty()) {
            error = "Asset source is unavailable";
            return false;
        }
        if (record->type == AssetType::Texture) {
            if (!renderer || !AssetManager::get().load_texture(renderer, path)) {
                error = "Texture could not be decoded";
                return false;
            }
            m_preloaded.insert(handle.value);
            return true;
        }
        std::vector<uint8_t> bytes = FileSystem::read_bytes(path);
        if (bytes.empty()) {
            error = "Asset bytes could not be loaded";
            return false;
        }
        m_binary_assets[handle.value] = std::move(bytes);
        m_preloaded.insert(handle.value);
        return true;
    }

    [[nodiscard]] bool is_preloaded(AssetHandle handle) const {
        return m_preloaded.contains(handle.value);
    }

private:
    std::unordered_map<uint64_t, std::vector<uint8_t>> m_binary_assets;
    std::unordered_set<uint64_t> m_preloaded;
};

} // namespace Engine
