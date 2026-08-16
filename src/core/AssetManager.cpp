#include "AssetManager.hpp"
#include "Log.hpp"

namespace Engine {

Texture* AssetManager::get_texture(SDL_Renderer* renderer, const std::string& filepath) {
    if (filepath.empty()) {
        return get_white_texture(renderer);
    }

    auto it = m_textures.find(filepath);
    if (it != m_textures.end() && it->second != nullptr) {
        return it->second.get();
    }

    auto tex = load_texture(renderer, filepath);
    if (!tex) {
        return get_white_texture(renderer);
    }

    return tex.get();
}

Texture* AssetManager::get_white_texture(SDL_Renderer* renderer) {
    if (!m_white_texture && renderer) {
        m_white_texture = std::shared_ptr<Texture>(Texture::create_solid(renderer, 4, 4, 255, 255, 255, 255));
    }
    return m_white_texture ? m_white_texture.get() : nullptr;
}

std::shared_ptr<Texture> AssetManager::load_texture(SDL_Renderer* renderer, const std::string& filepath) {
    auto it = m_textures.find(filepath);
    if (it != m_textures.end()) {
        return it->second;
    }

    Texture* tex = Texture::load_from_file(renderer, filepath);
    if (!tex) {
        ENGINE_LOG_WARN("[ASSET] Failed to load image asset: '{}'", filepath);
        return nullptr;
    }

    std::shared_ptr<Texture> shared_tex(tex);
    m_textures[filepath] = shared_tex;
    return shared_tex;
}

void AssetManager::register_texture(const std::string& name, std::unique_ptr<Texture> texture) {
    if (!texture) return;
    m_textures[name] = std::move(texture);
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
    m_white_texture.reset();
}

} // namespace Engine
