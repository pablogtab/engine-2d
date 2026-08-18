#pragma once

#include "FragmentShader.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace Engine {

class Material {
public:
    static constexpr std::size_t MAX_UNIFORM_SLOTS = 4;

    Material(std::string name, std::shared_ptr<const FragmentShader> shader)
        : m_name(std::move(name)), m_shader(std::move(shader)) {
        if (!m_shader) throw std::invalid_argument("Material requires a fragment shader");
    }

    [[nodiscard]] const std::string& name() const { return m_name; }
    [[nodiscard]] const FragmentShader& shader() const { return *m_shader; }
    [[nodiscard]] const std::shared_ptr<const FragmentShader>& shader_ptr() const { return m_shader; }

    void set_uniform_bytes(std::size_t slot, std::span<const std::byte> bytes) {
        if (slot >= MAX_UNIFORM_SLOTS) throw std::out_of_range("Material uniform slot is out of range");
        m_uniforms[slot].assign(bytes.begin(), bytes.end());
    }

    template<typename T>
    void set_uniform(std::size_t slot, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "Shader uniforms must be trivially copyable");
        set_uniform_bytes(slot, std::as_bytes(std::span<const T>(&value, 1)));
    }

    [[nodiscard]] std::span<const std::byte> uniform_bytes(std::size_t slot) const {
        if (slot >= MAX_UNIFORM_SLOTS) return {};
        return m_uniforms[slot];
    }

private:
    std::string m_name;
    std::shared_ptr<const FragmentShader> m_shader;
    std::array<std::vector<std::byte>, MAX_UNIFORM_SLOTS> m_uniforms;
};

} // namespace Engine
