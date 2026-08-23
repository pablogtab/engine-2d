#include <cassert>
#include <iostream>
#include "enginescript/module/GameModule.hpp"

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

    std::cout << "[PASS] EngineScriptModuleTests passed!\n";
    return 0;
}
