#pragma once

#include "../renderer/Texture.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace Engine {

class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager() = default;

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Loads a texture or retrieves it from cache if already loaded
    std::shared_ptr<Texture> load_texture(SDL_Renderer* renderer, const std::string& filepath);

    // Manually register a procedurally generated texture into the cache
    void register_texture(const std::string& name, std::unique_ptr<Texture> texture);

    // Retrieve a cached texture by name/filepath
    std::shared_ptr<Texture> get_texture(const std::string& name) const;

    bool has_texture(const std::string& name) const;
    void clear();

    size_t get_loaded_texture_count() const { return m_textures.size(); }

private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
};

} // namespace Engine
