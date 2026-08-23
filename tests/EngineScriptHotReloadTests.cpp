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
void* create_v2(void**) { return new MockPlayerV2(); }

int main() {
    std::cout << "[TEST] Running EngineScriptHotReloadTests...\n";

    AsyncScheduler scheduler;
    DIContainer di;
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
    hrm.register_instance(10, "PlatformerPlayer", std::move(inst1));

    assert(hrm.active_instances_count() == 1);

    // 2. Successful Hot Reload to Module V2
    ModuleDescriptor d2;
    d2.module_name = "GameplayV2";
    d2.behaviour_types.push_back({"PlatformerPlayer", create_v2});
    auto mod_v2 = std::make_shared<GameModule>(d2);

    bool reloaded = hrm.hot_reload(mod_v2, scheduler, di);
    assert(reloaded);
    assert(hrm.active_instances_count() == 1);

    // Verify property was restored
    auto p2 = dynamic_cast<MockPlayerV2*>(hrm.active_instances()[0].instance.get());
    assert(p2 != nullptr);
    assert(p2->speed == 350.0f);
    assert(p2->started);

    // 3. Failed Hot Reload with automatic rollback
    ModuleDescriptor d_broken;
    d_broken.module_name = "GameplayBroken";
    d_broken.behaviour_types.push_back({"PlatformerPlayer", nullptr}); // Broken factory!
    auto mod_broken = std::make_shared<GameModule>(d_broken);

    bool reload_failed = hrm.hot_reload(mod_broken, scheduler, di);
    assert(!reload_failed); // Failed and rolled back!

    // Verify we rolled back to V2 safely
    assert(hrm.active_module() == mod_v2);
    assert(hrm.active_instances_count() == 1);

    std::cout << "[PASS] EngineScriptHotReloadTests passed!\n";
    return 0;
}
