#pragma once

#include <cstdint>
#include <string>

namespace EngineScript {

class EngineScriptWorkspace;

struct NativeModuleBuildResult {
    bool success{false};
    std::string library_path;
    std::string error;
};

class NativeModuleBuilder {
public:
    [[nodiscard]] static NativeModuleBuildResult build(
        EngineScriptWorkspace& workspace,
        const std::string& project_root,
        uint32_t build_number
    );
};

} // namespace EngineScript
