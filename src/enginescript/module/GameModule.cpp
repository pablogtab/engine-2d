#include "GameModule.hpp"

#include <unordered_set>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace EngineScript {

GameModule::GameModule(ModuleDescriptor descriptor)
    : m_descriptor(std::move(descriptor)) {
    for (const auto& reg : m_descriptor.behaviour_types) {
        m_types_map[reg.type_name] = reg;
    }
}

GameModule::~GameModule() {
#if defined(_WIN32)
    if (m_lib_handle) FreeLibrary(static_cast<HMODULE>(m_lib_handle));
#else
    if (m_lib_handle) dlclose(m_lib_handle);
#endif
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

std::unique_ptr<GameModule> GameModule::from_native_exports(
    const NativeModuleExports& exports,
    std::string* error
) {
    const auto fail = [error](const std::string& message) -> std::unique_ptr<GameModule> {
        if (error) *error = message;
        return nullptr;
    };
    if (exports.abi_version != ENGINE_SCRIPT_ABI_VERSION) {
        return fail("EngineScript module ABI version is incompatible");
    }
    if (!exports.sdk_hash || std::string_view(exports.sdk_hash) != ENGINE_SCRIPT_SDK_HASH) {
        return fail("EngineScript module SDK hash is incompatible");
    }
    if (!exports.module_name || std::string_view(exports.module_name).empty()) {
        return fail("EngineScript module is missing its name");
    }
    if (exports.behaviour_type_count != 0 && !exports.behaviour_types) {
        return fail("EngineScript module is missing its behaviour registrations");
    }

    ModuleDescriptor descriptor;
    descriptor.abi_version = exports.abi_version;
    descriptor.sdk_hash = exports.sdk_hash;
    descriptor.module_name = exports.module_name;
    descriptor.build_number = exports.build_number;
    std::unordered_set<std::string> registered_names;
    for (std::size_t index = 0; index < exports.behaviour_type_count; ++index) {
        const NativeBehaviourExport& native_registration = exports.behaviour_types[index];
        if (!native_registration.type_name || !native_registration.factory ||
            !registered_names.emplace(native_registration.type_name).second) {
            return fail("EngineScript module has an invalid behaviour registration");
        }
        BehaviourTypeRegistration registration;
        registration.type_name = native_registration.type_name;
        registration.factory = native_registration.factory;
        if (native_registration.dependency_count != 0 && !native_registration.dependencies) {
            return fail("EngineScript module has missing behaviour dependencies");
        }
        for (std::size_t dependency_index = 0;
             dependency_index < native_registration.dependency_count;
             ++dependency_index) {
            const NativeDependencyExport& dependency = native_registration.dependencies[dependency_index];
            if (!dependency.type_name || std::string_view(dependency.type_name).empty()) {
                return fail("EngineScript module has an invalid behaviour dependency");
            }
            registration.constructor_dependencies.emplace_back(dependency.type_name, dependency.kind);
            switch (dependency.kind) {
                case NativeDependencyKind::Service:
                    registration.injected_services.push_back(dependency.type_name);
                    break;
                case NativeDependencyKind::RequiredComponent:
                    registration.required_components.push_back(dependency.type_name);
                    break;
                case NativeDependencyKind::OptionalComponent:
                    registration.optional_components.push_back(dependency.type_name);
                    break;
            }
        }
        descriptor.behaviour_types.push_back(std::move(registration));
    }
    return std::make_unique<GameModule>(std::move(descriptor));
}

std::unique_ptr<GameModule> GameModule::load_dynamic_library(
    const std::string& path,
    std::string* error
) {
    const auto fail = [error](const std::string& message) -> std::unique_ptr<GameModule> {
        if (error) *error = message;
        return nullptr;
    };
#if defined(_WIN32)
    HMODULE library = LoadLibraryA(path.c_str());
    if (!library) return fail("Could not load EngineScript module library");
    const auto exports_function = reinterpret_cast<GetNativeModuleExportsFn>(
        GetProcAddress(library, ENGINE_SCRIPT_MODULE_EXPORT_SYMBOL)
    );
#else
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) return fail("Could not load EngineScript module library: " + std::string(dlerror()));
    const auto exports_function = reinterpret_cast<GetNativeModuleExportsFn>(
        dlsym(library, ENGINE_SCRIPT_MODULE_EXPORT_SYMBOL)
    );
#endif
    if (!exports_function) {
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return fail("EngineScript module does not export engine_script_module_exports");
    }
    const NativeModuleExports* exports = exports_function();
    if (!exports) {
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return fail("EngineScript module returned no exports");
    }
    std::unique_ptr<GameModule> module = from_native_exports(*exports, error);
    if (!module) {
#if defined(_WIN32)
        FreeLibrary(library);
#else
        dlclose(library);
#endif
        return nullptr;
    }
    module->m_lib_handle = library;
    return module;
}

} // namespace EngineScript
