#include "AssetManager.hpp"
#include <iostream>

namespace Engine {

std::shared_ptr<Texture> AssetManager::load_texture(SDL_Renderer* renderer, const std::string& filepath) {
    auto it = m_textures.find(filepath);
    if (it != m_textures.end()) {
        // Return cached texture
        return it->second;
    }

    // Load new texture from file
    Texture* tex = Texture::load_from_file(renderer, filepath);
    if (!tex) {
        return nullptr;
    }

    std::shared_ptr<Texture> shared_tex(tex);
    m_textures[filepath] = shared_tex;
    std::cout << "[ASSET MANAGER] Cached texture asset: '" << filepath << "'" << std::endl;
    return shared_tex;
}

void AssetManager::register_texture(const std::string& name, std::unique_ptr<Texture> texture) {
    if (!texture) return;
    std::shared_ptr<Texture> shared_tex = std::move(texture);
    m_textures[name] = shared_tex;
    std::cout << "[ASSET MANAGER] Registered procedural texture: '" << name << "'" << std::endl;
}

std::shared_ptr<Texture> AssetManager::get_texture(const std::string& name) const {
    auto it = m_textures.find(name);
    if (it != m_textures.end()) {
        return it->second;
    }
    return nullptr;
}

bool AssetManager::has_texture(const std::string& name) const {
    return m_textures.find(name) != m_textures.end();
}

void AssetManager::clear() {
    m_textures.clear();
    std::cout << "[ASSET MANAGER] Cleared all cached assets." << std::endl;
}

} // namespace Engine
