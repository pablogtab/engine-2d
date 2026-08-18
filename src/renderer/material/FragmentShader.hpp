#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

enum class ShaderBinaryFormat {
    SpirV,
    Dxil,
    Msl,
    MetalLibrary
};

struct FragmentShaderVariant {
    ShaderBinaryFormat format{ShaderBinaryFormat::SpirV};
    std::vector<std::uint8_t> code;
    std::string entry_point{"main"};
    std::string source_path;
};

// Backend-neutral fragment shader asset. A shader can contain one compiled
// variant per graphics API; the active render backend selects the best match.
class FragmentShader {
public:
    explicit FragmentShader(std::string name) : m_name(std::move(name)) {}

    [[nodiscard]] const std::string& name() const { return m_name; }
    [[nodiscard]] const std::vector<FragmentShaderVariant>& variants() const { return m_variants; }
    [[nodiscard]] std::uint32_t sampler_count() const { return m_sampler_count; }
    [[nodiscard]] std::uint32_t uniform_buffer_count() const { return m_uniform_buffer_count; }

    void add_variant(FragmentShaderVariant variant) { m_variants.push_back(std::move(variant)); }
    bool add_variant_from_file(
        ShaderBinaryFormat format,
        const std::string& path,
        std::string entry_point = "main"
    );
    void set_sampler_count(std::uint32_t count) { m_sampler_count = count; }
    void set_uniform_buffer_count(std::uint32_t count);

private:
    std::string m_name;
    std::vector<FragmentShaderVariant> m_variants;
    std::uint32_t m_sampler_count{1};
    std::uint32_t m_uniform_buffer_count{0};
};

} // namespace Engine
