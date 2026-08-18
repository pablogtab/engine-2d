#include "FragmentShader.hpp"
#include <fstream>
#include <stdexcept>

namespace Engine {

bool FragmentShader::add_variant_from_file(
    ShaderBinaryFormat format,
    const std::string& path,
    std::string entry_point
) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;

    stream.seekg(0, std::ios::end);
    const std::streamsize size = stream.tellg();
    if (size <= 0) return false;
    stream.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size)) return false;

    // Text shader formats are consumed as C strings by some native drivers.
    if (format == ShaderBinaryFormat::Msl && bytes.back() != 0) bytes.push_back(0);
    add_variant({format, std::move(bytes), std::move(entry_point), path});
    return true;
}

void FragmentShader::set_uniform_buffer_count(std::uint32_t count) {
    if (count > 4) throw std::out_of_range("SDL GPU supports four fragment uniform slots");
    m_uniform_buffer_count = count;
}

} // namespace Engine
