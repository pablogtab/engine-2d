#pragma once

#include "FragmentShader.hpp"
#include "EngineShaderGlobals.hpp"
#include "../../core/AssetDatabase.hpp"
#include "../../core/FileSystem.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <cstring>
#include <vector>

namespace Engine {

struct ShaderAssetVariant {
    ShaderBinaryFormat format{ShaderBinaryFormat::SpirV};
    AssetHandle source_handle{};
    std::string entry_point;
};

enum class ShaderUniformParameterType {
    Float,
    Vec2,
    Vec4,
    Color
};

enum class ShaderUniformBufferSource {
    Material,
    Engine
};

struct ShaderUniformParameter {
    std::string name;
    ShaderUniformParameterType type{ShaderUniformParameterType::Float};
    std::size_t offset{0};
    std::vector<float> default_value;
    std::optional<float> minimum;
    std::optional<float> maximum;
};

struct ShaderUniformBufferLayout {
    std::string name;
    std::size_t size{0};
    ShaderUniformBufferSource source{ShaderUniformBufferSource::Material};
    std::vector<ShaderUniformParameter> parameters;
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
        if (description.name.empty() || description.sampler_count > MaterialSamplerLimit || !buffers.is_array() ||
            buffers.size() > MaterialUniformSlotLimit) {
            set_error(error, "Shader layout is unsupported");
            return {};
        }
        bool has_engine_buffer = false;
        for (const auto& buffer : buffers) {
            if (!buffer.is_object() || !buffer.contains("name") || !buffer["name"].is_string() ||
                !buffer.contains("size") || !buffer["size"].is_number_unsigned()) {
                set_error(error, "Shader uniform buffer layout is invalid");
                return {};
            }
            const std::size_t size = buffer["size"].get<std::size_t>();
            if (size == 0) { set_error(error, "Shader uniform buffer size must be positive"); return {}; }
            ShaderUniformBufferSource source = ShaderUniformBufferSource::Material;
            if (buffer.contains("source")) {
                if (!buffer["source"].is_string()) {
                    set_error(error, "Shader uniform buffer source must be a string");
                    return {};
                }
                const std::string source_name = buffer["source"].get<std::string>();
                if (source_name == "engine") source = ShaderUniformBufferSource::Engine;
                else if (source_name != "material") {
                    set_error(error, "Shader uniform buffer source is unsupported");
                    return {};
                }
            }
            if (source == ShaderUniformBufferSource::Engine) {
                if (has_engine_buffer || size != sizeof(EngineShaderGlobals)) {
                    set_error(error, "Engine uniform buffer must be unique and exactly 32 bytes");
                    return {};
                }
                has_engine_buffer = true;
            }
            ShaderUniformBufferLayout layout{
                buffer["name"].get<std::string>(), size, source, {}
            };
            const auto parameters = buffer.value("parameters", nlohmann::json::array());
            if (!parameters.is_array()) {
                set_error(error, "Shader uniform parameters must be an array");
                return {};
            }
            if (source == ShaderUniformBufferSource::Engine && !parameters.empty()) {
                set_error(error, "Engine uniform buffer cannot declare material parameters");
                return {};
            }
            for (const auto& parameter : parameters) {
                if (!parameter.is_object() || !parameter.contains("name") || !parameter["name"].is_string() ||
                    !parameter.contains("type") || !parameter["type"].is_string() ||
                    !parameter.contains("offset") || !parameter["offset"].is_number_unsigned()) {
                    set_error(error, "Shader uniform parameter is invalid");
                    return {};
                }
                const auto type = parameter_type_from_name(parameter["type"].get<std::string>());
                if (!type) {
                    set_error(error, "Shader uniform parameter type is unsupported");
                    return {};
                }
                ShaderUniformParameter property;
                property.name = parameter["name"].get<std::string>();
                property.type = *type;
                property.offset = parameter["offset"].get<std::size_t>();
                const std::size_t component_count = parameter_component_count(*type);
                const auto default_value = parameter.value("default", nlohmann::json::array());
                if (property.name.empty() || property.offset + component_count * sizeof(float) > size ||
                    !default_value.is_array() || default_value.size() != component_count) {
                    set_error(error, "Shader uniform parameter layout is invalid");
                    return {};
                }
                for (const auto& component : default_value) {
                    if (!component.is_number()) {
                        set_error(error, "Shader uniform parameter default is invalid");
                        return {};
                    }
                    property.default_value.push_back(component.get<float>());
                }
                if (parameter.contains("min")) {
                    if (!parameter["min"].is_number()) { set_error(error, "Shader parameter minimum is invalid"); return {}; }
                    property.minimum = parameter["min"].get<float>();
                }
                if (parameter.contains("max")) {
                    if (!parameter["max"].is_number()) { set_error(error, "Shader parameter maximum is invalid"); return {}; }
                    property.maximum = parameter["max"].get<float>();
                }
                if (property.minimum.has_value() != property.maximum.has_value()) {
                    set_error(error, "Shader parameter range requires both min and max");
                    return {};
                }
                if (property.minimum && property.maximum && *property.minimum > *property.maximum) {
                    set_error(error, "Shader parameter range is invalid");
                    return {};
                }
                const std::size_t property_end = property.offset + component_count * sizeof(float);
                for (const auto& existing : layout.parameters) {
                    const std::size_t existing_end = existing.offset +
                        parameter_component_count(existing.type) * sizeof(float);
                    if (property.name == existing.name ||
                        (property.offset < existing_end && existing.offset < property_end)) {
                        set_error(error, "Shader uniform parameters overlap or have duplicate names");
                        return {};
                    }
                }
                layout.parameters.push_back(std::move(property));
            }
            description.uniform_buffers.push_back(std::move(layout));
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
        for (std::size_t slot = 0; slot < description->uniform_buffers.size(); ++slot) {
            if (description->uniform_buffers[slot].source == ShaderUniformBufferSource::Engine) {
                shader->set_engine_uniform_slot(static_cast<std::uint32_t>(slot));
                break;
            }
        }
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
        const std::size_t material_buffer_count = static_cast<std::size_t>(std::count_if(
            shader.uniform_buffers.begin(), shader.uniform_buffers.end(),
            [](const ShaderUniformBufferLayout& layout) {
                return layout.source == ShaderUniformBufferSource::Material;
            }
        ));
        if (uniforms.size() != material_buffer_count) {
            set_error(error, "Material uniform buffer count does not match its shader");
            return false;
        }
        std::size_t material_slot = 0;
        for (const auto& layout : shader.uniform_buffers) {
            if (layout.source == ShaderUniformBufferSource::Engine) continue;
            if (uniforms[material_slot].size() != layout.size) {
                set_error(error, "Material uniform slot " + std::to_string(material_slot) + " must contain " +
                    std::to_string(layout.size) + " bytes");
                return false;
            }
            ++material_slot;
        }
        return true;
    }

    [[nodiscard]] static std::vector<std::vector<std::byte>> make_default_uniforms(
        const ShaderAssetDescription& shader
    ) {
        std::vector<std::vector<std::byte>> uniforms;
        uniforms.reserve(shader.uniform_buffers.size());
        for (const auto& layout : shader.uniform_buffers) {
            if (layout.source == ShaderUniformBufferSource::Engine) continue;
            auto& bytes = uniforms.emplace_back(layout.size, std::byte{0});
            for (const auto& parameter : layout.parameters) {
                std::memcpy(
                    bytes.data() + parameter.offset,
                    parameter.default_value.data(),
                    parameter.default_value.size() * sizeof(float)
                );
            }
        }
        return uniforms;
    }

    [[nodiscard]] static std::size_t parameter_component_count(ShaderUniformParameterType type) {
        switch (type) {
            case ShaderUniformParameterType::Float: return 1;
            case ShaderUniformParameterType::Vec2: return 2;
            case ShaderUniformParameterType::Vec4:
            case ShaderUniformParameterType::Color: return 4;
        }
        return 0;
    }

private:
    static constexpr std::size_t MaterialUniformSlotLimit = 4;
    static constexpr std::uint32_t MaterialSamplerLimit = 8;

    static void set_error(std::string* error, std::string message) { if (error) *error = std::move(message); }

    [[nodiscard]] static std::optional<ShaderBinaryFormat> format_from_name(const std::string& name) {
        if (name == "msl") return ShaderBinaryFormat::Msl;
        if (name == "spirv") return ShaderBinaryFormat::SpirV;
        if (name == "dxil") return ShaderBinaryFormat::Dxil;
        if (name == "metallib") return ShaderBinaryFormat::MetalLibrary;
        return {};
    }

    [[nodiscard]] static std::optional<ShaderUniformParameterType> parameter_type_from_name(
        const std::string& name
    ) {
        if (name == "float") return ShaderUniformParameterType::Float;
        if (name == "vec2") return ShaderUniformParameterType::Vec2;
        if (name == "vec4") return ShaderUniformParameterType::Vec4;
        if (name == "color") return ShaderUniformParameterType::Color;
        return {};
    }
};

} // namespace Engine
