#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "enginescript/module/GameModule.hpp"
#include "enginescript/module/NativeModuleBuilder.hpp"
#include "enginescript/editor/EngineScriptWorkspace.hpp"

using namespace EngineScript;

class TestPlayerBehaviour : public NativeBehaviour {
public:
    float speed = 100.0f;
    bool updated = false;

    void update(float dt) override {
        updated = true;
    }
};

void* create_player(void**) {
    return new TestPlayerBehaviour();
}

namespace {

std::string quote_argument(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

void test_dynamic_module_loading() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_native_module_tests";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    const fs::path source = root / "GameplayModule.cpp";
#if defined(_WIN32)
    const fs::path library = root / "GameplayModule.dll";
    constexpr const char* library_flags = " /LD ";
#elif defined(__APPLE__)
    const fs::path library = root / "libGameplayModule.dylib";
    constexpr const char* library_flags = " -dynamiclib ";
#else
    const fs::path library = root / "libGameplayModule.so";
    constexpr const char* library_flags = " -shared -fPIC ";
#endif
    std::ofstream(source) << R"(
#include "enginescript/module/GameModule.hpp"

class LoadedBehaviour final : public EngineScript::NativeBehaviour {
public:
    void update(float) override {}
};

extern "C" EngineScript::NativeBehaviour* create_loaded(void**) {
    return new LoadedBehaviour();
}

extern "C" const EngineScript::NativeModuleExports* engine_script_module_exports() {
    static const EngineScript::NativeBehaviourExport behaviours[] = {
        {"Loaded", create_loaded, nullptr, 0}
    };
    static const EngineScript::NativeModuleExports exports{
        EngineScript::ENGINE_SCRIPT_ABI_VERSION,
        EngineScript::ENGINE_SCRIPT_SDK_HASH,
        "Gameplay",
        7,
        behaviours,
        1
    };
    return &exports;
}
)";

#if defined(_MSC_VER)
    const std::string command = quote_argument(ENGINE_TEST_CXX_COMPILER) +
        " /nologo /std:c++20" + library_flags + " /I" +
        quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "src") + " /I" +
        quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "vendor") + " /Fe" +
        quote_argument(library) + " " + quote_argument(source);
#else
    const std::string command = quote_argument(ENGINE_TEST_CXX_COMPILER) +
        " -std=c++20" + library_flags + " -I" +
        quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "src") + " -I" +
        quote_argument(fs::path(ENGINE_TEST_SOURCE_DIR) / "vendor") + " -I" +
        quote_argument(ENGINE_TEST_GLM_INCLUDE_DIR) + " -o " +
        quote_argument(library) + " " + quote_argument(source);
#endif
    assert(std::system(command.c_str()) == 0);

    std::string load_error;
    std::unique_ptr<GameModule> module = GameModule::load_dynamic_library(library.string(), &load_error);
    assert(module != nullptr);
    assert(load_error.empty());
    assert(module->is_compatible());
    assert(module->descriptor().module_name == "Gameplay");
    assert(module->descriptor().build_number == 7);
    assert(module->has_behaviour_type("Loaded"));

    NativeBehaviour* behaviour = module->create_behaviour("Loaded");
    assert(behaviour != nullptr);
    behaviour->update(0.016f);
    delete behaviour;
    module.reset();
    fs::remove_all(root, error);
}

void test_generated_module_build_and_load() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "engine2d_generated_native_module_tests";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);
    EngineScriptWorkspace workspace(root.string());
    workspace.add_source_file("Assets/Scripts/Utils.e2s", R"(
export function identity<T extends Send>(value: T): T { return value; }
)");
    workspace.add_source_file("Assets/Scripts/Probe.e2s", R"(
import { identity } from "./Utils";
@behaviour
export class Probe extends Behaviour {
    @expose speed: Float = identity<Float>(42.0);
    override update(deltaTime: Float): Void { speed = speed + deltaTime; }
}
)");
    assert(workspace.compile().success);
    const NativeModuleBuildResult build = NativeModuleBuilder::build(workspace, root.string(), 11);
    assert(build.success);
    std::string load_error;
    std::unique_ptr<GameModule> module = GameModule::load_dynamic_library(build.library_path, &load_error);
    assert(module && module->has_behaviour_type("Probe"));
    std::unique_ptr<NativeBehaviour> behaviour(module->create_behaviour("Probe"));
    NativeBehaviour::ExposedPropertyValue speed;
    assert(behaviour && behaviour->get_exposed_property("speed", speed));
    assert(std::get<double>(speed) == 42.0);
    behaviour->update(0.5f);
    assert(behaviour->get_exposed_property("speed", speed));
    assert(std::get<double>(speed) == 42.5);
    behaviour.reset();
    module.reset();
    const NativeModuleBuildResult static_sources = NativeModuleBuilder::build(
        workspace, root.string(), 12, NativeModuleBuildMode::StaticSources
    );
    assert(static_sources.success);
    assert(fs::exists(fs::path(static_sources.output_directory) / "GameModule.generated.cpp"));
    assert(static_sources.library_path.empty());
    fs::remove_all(root, error);
}

} // namespace

int main() {
    std::cout << "[TEST] Running EngineScriptModuleTests...\n";

    ModuleDescriptor desc;
    desc.module_name = "Gameplay";
    desc.abi_version = ENGINE_SCRIPT_ABI_VERSION;
    desc.sdk_hash = ENGINE_SCRIPT_SDK_HASH;

    BehaviourTypeRegistration reg;
    reg.type_name = "Player";
    reg.factory = create_player;
    desc.behaviour_types.push_back(reg);

    GameModule module(desc);

    // Test 1: Module compatibility check
    assert(module.is_compatible());

    // Test 2: Factory creation
    assert(module.has_behaviour_type("Player"));
    assert(!module.has_behaviour_type("Enemy"));

    NativeBehaviour* b = module.create_behaviour("Player");
    assert(b != nullptr);

    b->update(0.016f);
    auto p = dynamic_cast<TestPlayerBehaviour*>(b);
    assert(p != nullptr);
    assert(p->updated);

    delete b;

    // Test 3: Incompatible ABI module rejection
    ModuleDescriptor bad_desc = desc;
    bad_desc.abi_version = 999;
    GameModule bad_module(bad_desc);
    assert(!bad_module.is_compatible());

    // Test 4: Load a behaviour factory through the versioned C ABI boundary.
    test_dynamic_module_loading();
    test_generated_module_build_and_load();

    std::cout << "[PASS] EngineScriptModuleTests passed!\n";
    return 0;
}
