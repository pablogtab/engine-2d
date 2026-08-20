#pragma once

#include "FragmentShader.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Engine {

struct ShaderAssetVariant {
    ShaderBinaryFormat format{ShaderBinaryFormat::SpirV};
    AssetHandle source_handle{};
    std::string entry_point;
};

struct ShaderUniformBufferLayout {
    std::string name;
    std::size_t size{0};
};

struct ShaderAssetDescription {
    std::string name;
    std::uint32_t sampler_count{1};
    std::vector<ShaderUniformBufferLayout> uniform_buffers;
    std::vector<ShaderAssetVariant> variants;
};

class ShaderAsset {
public:
    [[nodiscard]] static std::optional<ShaderAssetDescription> read(
        AssetHandle handle, const AssetDatabase& database, std::string* error = nullptr
    ) {
        const AssetRecord* record = database.find(handle);
        if (!record || record->status != AssetStatus::Ready || record->type != AssetType::Shader) {
            set_error(error, "Shader asset is missing or invalid");
            return {};
        }
        const auto json = nlohmann::json::parse(FileSystem::read_text(database.resolve(handle)), nullptr, false);
        if (json.is_discarded() || !json.contains("name") || !json["name"].is_string() ||
            !json.contains("variants") || !json["variants"].is_array()) {
            set_error(error, "Shader JSON is invalid");
            return {};
        }
        ShaderAssetDescription description;
        description.name = json["name"].get<std::string>();
        description.sampler_count = json.value("sampler_count", 1u);
        const auto buffers = json.value("uniform_buffers", nlohmann::json::array());
        if (description.name.empty() || description.sampler_count != 1 || !buffers.is_array() ||
            buffers.size() > MaterialUniformSlotLimit) {
            set_error(error, "Shader layout is unsupported");
            return {};
        }
        for (const auto& buffer : buffers) {
            if (!buffer.is_object() || !buffer.contains("name") || !buffer["name"].is_string() ||
                !buffer.contains("size") || !buffer["size"].is_number_unsigned()) {
                set_error(error, "Shader uniform buffer layout is invalid");
                return {};
            }
            const std::size_t size = buffer["size"].get<std::size_t>();
            if (size == 0) { set_error(error, "Shader uniform buffer size must be positive"); return {}; }
            description.uniform_buffers.push_back({buffer["name"].get<std::string>(), size});
        }
        for (const auto& variant : json["variants"]) {
            if (!variant.is_object() || !variant.contains("format") || !variant["format"].is_string() ||
                !variant.contains("source_handle") || !variant["source_handle"].is_string() ||
                !variant.contains("entry_point") || !variant["entry_point"].is_string()) {
                set_error(error, "Shader variant is invalid");
                return {};
            }
            const auto format = format_from_name(variant["format"].get<std::string>());
            const auto source = AssetHandle::from_string(variant["source_handle"].get<std::string>());
            const std::string entry_point = variant["entry_point"].get<std::string>();
            const AssetRecord* source_record = source ? database.find(*source) : nullptr;
            if (!format || !source_record || source_record->status != AssetStatus::Ready || entry_point.empty()) {
                set_error(error, "Shader variant source or entry point is invalid");
                return {};
            }
            description.variants.push_back({*format, *source, entry_point});
        }
        if (description.variants.empty()) { set_error(error, "Shader has no variants"); return {}; }
        return description;
    }

    [[nodiscard]] static std::shared_ptr<const FragmentShader> load(
        AssetHandle handle, const AssetDatabase& database, std::string* error = nullptr
    ) {
        const auto description = read(handle, database, error);
        if (!description) return {};
        auto shader = std::make_shared<FragmentShader>(description->name);
        shader->set_sampler_count(description->sampler_count);
        shader->set_uniform_buffer_count(static_cast<std::uint32_t>(description->uniform_buffers.size()));
        for (const auto& variant : description->variants) {
            if (!shader->add_variant_from_file(variant.format, database.resolve(variant.source_handle), variant.entry_point)) {
                set_error(error, "Failed reading shader variant source");
                return {};
            }
        }
        return shader;
    }

    [[nodiscard]] static bool validate_material(
        const ShaderAssetDescription& shader, const std::vector<std::vector<std::byte>>& uniforms, std::string* error = nullptr
    ) {
        if (uniforms.size() != shader.uniform_buffers.size()) {
            set_error(error, "Material uniform buffer count does not match its shader");
            return false;
        }
        for (std::size_t slot = 0; slot < uniforms.size(); ++slot) {
            if (uniforms[slot].size() != shader.uniform_buffers[slot].size) {
                set_error(error, "Material uniform slot " + std::to_string(slot) + " must contain " +
                    std::to_string(shader.uniform_buffers[slot].size) + " bytes");
                return false;
            }
        }
        return true;
    }

private:
    static constexpr std::size_t MaterialUniformSlotLimit = 4;

    static void set_error(std::string* error, std::string message) { if (error) *error = std::move(message); }

    [[nodiscard]] static std::optional<ShaderBinaryFormat> format_from_name(const std::string& name) {
        if (name == "msl") return ShaderBinaryFormat::Msl;
        if (name == "spirv") return ShaderBinaryFormat::SpirV;
        if (name == "dxil") return ShaderBinaryFormat::Dxil;
        if (name == "metallib") return ShaderBinaryFormat::MetalLibrary;
        return {};
    }
};

} // namespace Engine
