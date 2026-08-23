#include "GameModule.hpp"

namespace EngineScript {

GameModule::GameModule(ModuleDescriptor descriptor)
    : m_descriptor(std::move(descriptor)) {
    for (const auto& reg : m_descriptor.behaviour_types) {
        m_types_map[reg.type_name] = reg;
    }
}

GameModule::~GameModule() {
    // Note: dynamic library handle closure can be handled here if loaded dynamically
}

bool GameModule::is_compatible() const {
    return m_descriptor.abi_version == ENGINE_SCRIPT_ABI_VERSION &&
           m_descriptor.sdk_hash == ENGINE_SCRIPT_SDK_HASH;
}

bool GameModule::has_behaviour_type(const std::string& type_name) const {
    return m_types_map.find(type_name) != m_types_map.end();
}

NativeBehaviour* GameModule::create_behaviour(const std::string& type_name, void** args) const {
    auto it = m_types_map.find(type_name);
    if (it != m_types_map.end() && it->second.factory) {
        return static_cast<NativeBehaviour*>(it->second.factory(args));
    }
    return nullptr;
}

} // namespace EngineScript
