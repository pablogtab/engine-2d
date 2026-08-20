#include "MaterialAsset.hpp"
#include "Material.hpp"
#include "FragmentShader.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>

namespace Engine {
namespace {
ShaderBinaryFormat format_for(const std::string& path) {
    return std::filesystem::path(path).extension() == ".msl" ? ShaderBinaryFormat::Msl : ShaderBinaryFormat::SpirV;
}
}

std::shared_ptr<const Material> MaterialAsset::load(AssetHandle handle, const AssetDatabase& database) {
    const AssetRecord* record = database.find(handle);
    if (!record || record->status != AssetStatus::Ready || record->type != AssetType::Material) return {};
    const auto json = nlohmann::json::parse(FileSystem::read_text(database.resolve(handle)), nullptr, false);
    if (json.is_discarded() || !json.contains("name") || !json["name"].is_string() || !json.contains("shader_handle")) return {};
    const auto shader_handle = AssetHandle::from_string(json.value("shader_handle", std::string{}));
    const AssetRecord* shader_record = shader_handle ? database.find(*shader_handle) : nullptr;
    if (!shader_record || shader_record->status != AssetStatus::Ready || shader_record->type != AssetType::Shader) return {};
    auto shader = std::make_shared<FragmentShader>(json["name"].get<std::string>() + "Shader");
    if (!shader->add_variant_from_file(format_for(shader_record->source_path), database.resolve(*shader_handle))) return {};
    const auto uniforms = json.value("uniforms", nlohmann::json::array());
    if (!uniforms.is_array() || uniforms.size() > Material::MAX_UNIFORM_SLOTS) return {};
    shader->set_uniform_buffer_count(static_cast<std::uint32_t>(uniforms.size()));
    auto material = std::make_shared<Material>(json["name"].get<std::string>(), shader);
    for (std::size_t i = 0; i < uniforms.size(); ++i) {
        if (!uniforms[i].is_array()) return {};
        std::vector<std::byte> bytes;
        for (const auto& value : uniforms[i]) { if (!value.is_number_unsigned() || value.get<unsigned>() > 255) return {}; bytes.push_back(static_cast<std::byte>(value.get<unsigned>())); }
        material->set_uniform_bytes(i, bytes);
    }
    return material;
}
} // namespace Engine
