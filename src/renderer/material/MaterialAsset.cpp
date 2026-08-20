#include "MaterialAsset.hpp"
#include "Material.hpp"
#include "ShaderAsset.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>

namespace Engine {
namespace {
void set_error(std::string* error, std::string message) {
    if (error) *error = std::move(message);
}
}

std::optional<MaterialAssetDescription> MaterialAsset::read(
    AssetHandle handle, const AssetDatabase& database, std::string* error
) {
    const AssetRecord* record = database.find(handle);
    if (!record || record->status != AssetStatus::Ready || record->type != AssetType::Material) {
        set_error(error, "Material asset is missing or invalid");
        return {};
    }
    const auto json = nlohmann::json::parse(FileSystem::read_text(database.resolve(handle)), nullptr, false);
    if (json.is_discarded() || !json.contains("name") || !json["name"].is_string() || !json.contains("shader_handle")) {
        set_error(error, "Material JSON is invalid");
        return {};
    }
    const auto shader_handle = AssetHandle::from_string(json.value("shader_handle", std::string{}));
    if (!shader_handle) { set_error(error, "Material shader handle is invalid"); return {}; }
    const auto uniforms = json.value("uniforms", nlohmann::json::array());
    if (!uniforms.is_array() || uniforms.size() > Material::MAX_UNIFORM_SLOTS) {
        set_error(error, "Material uniforms are invalid");
        return {};
    }
    MaterialAssetDescription description{json["name"].get<std::string>(), *shader_handle, {}};
    for (const auto& uniform : uniforms) {
        if (!uniform.is_array()) { set_error(error, "Material uniform must be a byte array"); return {}; }
        std::vector<std::byte> bytes;
        for (const auto& value : uniform) {
            if (!value.is_number_unsigned() || value.get<unsigned>() > 255) {
                set_error(error, "Material uniform contains an invalid byte");
                return {};
            }
            bytes.push_back(static_cast<std::byte>(value.get<unsigned>()));
        }
        description.uniforms.push_back(std::move(bytes));
    }
    return description;
}

bool MaterialAsset::save(
    AssetHandle handle,
    const MaterialAssetDescription& description,
    AssetDatabase& database,
    std::string* error
) {
    const AssetRecord* record = database.find(handle);
    const auto shader = ShaderAsset::read(description.shader_handle, database, error);
    if (!record || record->status != AssetStatus::Ready || record->type != AssetType::Material ||
        description.name.empty() || !shader ||
        description.uniforms.size() > Material::MAX_UNIFORM_SLOTS) {
        set_error(error, "Material asset or shader is invalid");
        return false;
    }
    if (!ShaderAsset::validate_material(*shader, description.uniforms, error)) return false;
    nlohmann::json uniforms = nlohmann::json::array();
    for (const auto& bytes : description.uniforms) {
        nlohmann::json uniform = nlohmann::json::array();
        for (std::byte byte : bytes) uniform.push_back(std::to_integer<unsigned>(byte));
        uniforms.push_back(std::move(uniform));
    }
    const nlohmann::json json{{"name", description.name}, {"shader_handle", description.shader_handle.to_string()}, {"uniforms", uniforms}};
    if (!FileSystem::write_text_atomically(database.resolve(handle), json.dump(2) + "\n")) {
        set_error(error, "Failed writing material asset");
        return false;
    }
    return database.import_asset(database.resolve(handle)).has_value();
}

std::shared_ptr<const Material> MaterialAsset::load(AssetHandle handle, const AssetDatabase& database) {
    const auto description = read(handle, database);
    if (!description) return {};
    std::string error;
    const auto shader_description = ShaderAsset::read(description->shader_handle, database, &error);
    if (!shader_description || !ShaderAsset::validate_material(*shader_description, description->uniforms, &error)) return {};
    const auto shader = ShaderAsset::load(description->shader_handle, database, &error);
    if (!shader) return {};
    auto material = std::make_shared<Material>(description->name, shader);
    for (std::size_t i = 0; i < description->uniforms.size(); ++i) {
        material->set_uniform_bytes(i, description->uniforms[i]);
    }
    return material;
}
} // namespace Engine
