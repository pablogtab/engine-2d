#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace EngineScript {

class EngineScriptWorkspace;

enum class NativeModuleBuildMode {
    DynamicLibrary,
    StaticSources,
    PrepareDynamicLibrary
};

enum class NativeModuleBuildPhase {
    CheckingCache,
    GeneratingSources,
    CompilingNative
};

using NativeModuleBuildProgress = std::function<void(NativeModuleBuildPhase)>;

struct NativeModuleBuildResult {
    bool success{false};
    bool cache_hit{false};
    std::uint32_t compiled_objects{0};
    std::uint32_t reused_objects{0};
    std::string output_directory;
    std::string library_path;
    std::string source_hash;
    std::string error;
};

class NativeModuleBuilder {
public:
    [[nodiscard]] static NativeModuleBuildResult build(
        EngineScriptWorkspace& workspace,
        const std::string& project_root,
        uint32_t build_number,
        NativeModuleBuildMode mode = NativeModuleBuildMode::DynamicLibrary,
        NativeModuleBuildProgress progress = {}
    );
    [[nodiscard]] static NativeModuleBuildResult compile_prepared(
        const std::string& project_root,
        uint32_t build_number,
        const std::string& source_hash,
        NativeModuleBuildProgress progress = {}
    );
};

} // namespace EngineScript
