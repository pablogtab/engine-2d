#pragma once

#include "../renderer/Texture.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace Engine {

class AssetManager {
public:
    static AssetManager& get() {
        static AssetManager s_instance;
        return s_instance;
    }

    AssetManager() = default;
    ~AssetManager() = default;

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Loads a raw texture pointer or retrieves it from cache if already loaded
    Texture* get_texture(SDL_Renderer* renderer, const std::string& filepath);

    // Returns a fallback 4x4 pure white texture (cached)
    Texture* get_white_texture(SDL_Renderer* renderer);

    // Loads a texture or retrieves it from cache if already loaded (shared_ptr)
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
    std::shared_ptr<Texture> m_white_texture{nullptr};
};

} // namespace Engine
