#pragma once

#include "../ir/TypedIR.hpp"
#include "../module/GameModule.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace EngineScript {

struct ExposedPropertyDesc {
    std::string name;
    std::string type;
    std::string default_value_str;
    std::optional<double> min_value;
    std::optional<double> max_value;
    bool is_exposed = false;
    bool is_serializable = true;
    bool is_behaviour_reference = false;
};

struct InjectedDependencyDesc {
    std::string name;
    std::string type;
    bool is_service = false;
    bool is_component = false;
    bool is_optional = false;
};

struct BehaviourManifest {
    std::string name;
    std::string module_path;
    std::vector<InjectedDependencyDesc> dependencies;
    std::vector<ExposedPropertyDesc> properties;
    std::vector<std::string> methods;
};

struct ProjectMetadataManifest {
    std::uint32_t language_version = ENGINE_SCRIPT_LANGUAGE_VERSION;
    std::string compiler_cache_id = ENGINE_SCRIPT_COMPILER_CACHE_ID;
    std::string sdk_hash = ENGINE_SCRIPT_SDK_HASH;
    std::vector<BehaviourManifest> behaviours;
    std::vector<std::string> structs;
    std::vector<std::string> enums;
};

class MetadataEmitter {
public:
    static std::string emit_json(const std::vector<IRModule>& modules);
    static ProjectMetadataManifest parse_json(const std::string& json_str);
};

} // namespace EngineScript
