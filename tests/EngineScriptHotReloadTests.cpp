#include <cassert>
#include <iostream>
#include "enginescript/module/HotReloadManager.hpp"
#include "enginescript/async/AsyncScheduler.hpp"
#include "enginescript/di/DependencyInjection.hpp"

using namespace EngineScript;

class MockPlayerV1 : public NativeBehaviour {
public:
    float speed = 220.0f;
    bool started = false;

    void on_start() override { started = true; }
};

class MockPlayerV2 : public NativeBehaviour {
public:
    float speed = 0.0f; // Restored from snapshot
    bool started = false;

    void on_start() override { started = true; }
};

void* create_v1(void**) { return new MockPlayerV1(); }
void* expected_input_service = nullptr;
void* create_v2(void** args) {
    if (!args || args[0] != expected_input_service) return nullptr;
    return new MockPlayerV2();
}

int main() {
    std::cout << "[TEST] Running EngineScriptHotReloadTests...\n";

    AsyncScheduler scheduler;
    DIContainer di;
    int input_service = 42;
    expected_input_service = &input_service;
    di.register_instance("Input", expected_input_service);
    HotReloadManager hrm;

    // Snapshot hooks
    hrm.on_snapshot_properties = [](const NativeBehaviour* b) {
        std::unordered_map<std::string, std::string> props;
        if (auto p1 = dynamic_cast<const MockPlayerV1*>(b)) {
            props["speed"] = std::to_string(p1->speed);
        } else if (auto p2 = dynamic_cast<const MockPlayerV2*>(b)) {
            props["speed"] = std::to_string(p2->speed);
        }
        return props;
    };

    hrm.on_restore_properties = [](NativeBehaviour* b, const std::unordered_map<std::string, std::string>& props) {
        auto it = props.find("speed");
        if (it != props.end()) {
            if (auto p1 = dynamic_cast<MockPlayerV1*>(b)) {
                p1->speed = std::stof(it->second);
            } else if (auto p2 = dynamic_cast<MockPlayerV2*>(b)) {
                p2->speed = std::stof(it->second);
            }
        }
    };

    // 1. Initial Module V1
    ModuleDescriptor d1;
    d1.module_name = "GameplayV1";
    d1.behaviour_types.push_back({"PlatformerPlayer", create_v1});
    auto mod_v1 = std::make_shared<GameModule>(d1);
    assert(hrm.load_initial_module(mod_v1));

    // Register active entity instance
    auto inst1 = std::make_unique<MockPlayerV1>();
    inst1->speed = 350.0f; // modified authored property
    int owner_context = 7;
    inst1->owner_entity = Entity{.id = 10};
    inst1->owner_entity.runtime_context = &owner_context;
    inst1->owner_entity.destroy_callback = [](void*, uint32_t) {};
    hrm.register_instance(10, "PlatformerPlayer", std::move(inst1));

    assert(hrm.active_instances_count() == 1);

    // 2. Successful Hot Reload to Module V2
    ModuleDescriptor d2;
    d2.module_name = "GameplayV2";
    BehaviourTypeRegistration v2_registration;
    v2_registration.type_name = "PlatformerPlayer";
    v2_registration.factory = create_v2;
    v2_registration.injected_services = {"Input"};
    d2.behaviour_types.push_back(std::move(v2_registration));
    auto mod_v2 = std::make_shared<GameModule>(d2);

    bool reloaded = hrm.hot_reload(mod_v2, scheduler, di);
    assert(reloaded);
    assert(hrm.active_instances_count() == 1);

    // Verify property was restored
    auto p2 = dynamic_cast<MockPlayerV2*>(hrm.active_instances()[0].instance.get());
    assert(p2 != nullptr);
    assert(p2->speed == 350.0f);
    assert(p2->started);
    assert(p2->self().id == 10);
    assert(p2->self().runtime_context == &owner_context);
    assert(p2->self().destroy_callback != nullptr);

    // 3. Failed Hot Reload stops execution instead of hiding the failure by rollback.
    ModuleDescriptor d_broken;
    d_broken.module_name = "GameplayBroken";
    d_broken.behaviour_types.push_back({"PlatformerPlayer", nullptr}); // Broken factory!
    auto mod_broken = std::make_shared<GameModule>(d_broken);

    bool reload_failed = hrm.hot_reload(mod_broken, scheduler, di);
    assert(!reload_failed);

    assert(hrm.active_module() == nullptr);
    assert(hrm.active_instances_count() == 0);

    std::cout << "[PASS] EngineScriptHotReloadTests passed!\n";
    return 0;
}
