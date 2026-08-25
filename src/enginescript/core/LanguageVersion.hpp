#pragma once

#include <cstdint>
#include <cstddef>

namespace EngineScript {

inline constexpr std::uint32_t ENGINE_SCRIPT_LANGUAGE_VERSION = 2;
inline constexpr const char* ENGINE_SCRIPT_COMPILER_CACHE_ID = "enginescript_codegen_v2";
inline constexpr std::uint32_t ENGINE_SCRIPT_ABI_VERSION = 25;
inline constexpr const char* ENGINE_SCRIPT_SDK_HASH = "engine2d_abi_v25";
inline constexpr std::size_t ENGINE_SCRIPT_MAX_GENERIC_INSTANTIATIONS = 256;

} // namespace EngineScript
