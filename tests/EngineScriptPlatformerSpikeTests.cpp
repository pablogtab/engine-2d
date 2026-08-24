#include <cassert>
#include <iostream>
#include <chrono>
#include "enginescript/EngineScript.hpp"

using namespace EngineScript;

// Native implementation representing the generated C++ spike runtime behaviour
class PlatformerPlayerNative : public NativeBehaviour {
public:
    float walkSpeed = 220.0f;
    float jumpSpeed = 480.0f;
    bool isGrounded = true;
    float velocity_x = 0.0f;
    float velocity_y = 0.0f;
    bool anim_played = false;
    bool is_respawning = false;

    void update(float dt) override {
        // Simulates: body.velocity.x = input.axis("Horizontal") * walkSpeed;
        float axis = 1.0f; // Simulated right input
        velocity_x = axis * walkSpeed;

        if (isGrounded) {
            velocity_y = jumpSpeed;
        }
    }

    Task<void> respawn(AsyncScheduler& scheduler) {
        is_respawning = true;
        co_await scheduler.delay(0.5f, task_scope_id);
        anim_played = true;
        is_respawning = false;
    }
};

void* create_platformer_native(void**) {
    return new PlatformerPlayerNative();
}

int main() {
    std::cout << "[TEST] Running EngineScriptPlatformerSpikeTests (Milestone R17)...\n";

    auto start_time = std::chrono::high_resolution_clock::now();

    // Step 1: Workspace setup & Source authoring
    EngineScriptWorkspace workspace("/workspace/SpikeProject");

    std::string spike_source = R"(
        @behaviour
        export class PlatformerPlayer extends Behaviour {
            constructor(
                @inject private input: Input,
                @component private body: RigidBody2D,
                @optional private animator: Animator2D?
            ) {}

            @expose({ min: 0.0, max: 1000.0 })
            walkSpeed: Float = 220.0;

            @expose
            jumpSpeed: Float = 480.0;

            override update(deltaTime: Float): Void {
                let axis: Float = input.axis("Horizontal");
                body.velocity.x = axis * walkSpeed;
                if (input.pressed("Jump") && body.isGrounded) {
                    body.velocity.y = jumpSpeed;
                }
            }

            async respawn(): Task<Void> {
                await delay(500.ms);
                animator?.play("idle");
            }
        }
    )";

    workspace.add_source_file("Source/Behaviours/PlatformerPlayer.e2s", spike_source);

    // Step 2: Compile & Measure Cold Build Time
    auto cold_compile_start = std::chrono::high_resolution_clock::now();
    CompilationResult res = workspace.compile();
    auto cold_compile_end = std::chrono::high_resolution_clock::now();
    double cold_build_ms = std::chrono::duration<double, std::milli>(cold_compile_end - cold_compile_start).count();

    assert(res.success);
    assert(res.error_count == 0);
    assert(!res.metadata_json.empty());
    assert(res.generated_cpp.size() == 1);

    std::cout << "  [METRIC] Cold compile time: " << cold_build_ms << " ms\n";
    std::cout << "  [METRIC] Generated C++ header size: " << res.generated_cpp[0].header_content.size() << " bytes\n";
    std::cout << "  [METRIC] Generated C++ source size: " << res.generated_cpp[0].source_content.size() << " bytes\n";

    // Step 3: DI and ECS Component Requirements Validation
    DIContainer di;
    int fake_input_service = 1;
    di.register_instance("Input", &fake_input_service, DIScopeLevel::Application);

    EntityContext entity;
    entity.entity_id = 1;
    entity.attached_components = {"RigidBody2D", "SpriteRenderer2D"}; // Missing Animator2D is OK (optional)

    auto di_res = di.validate_behaviour_requirements(
        "PlatformerPlayer",
        {{"input", "Input"}},
        {{"body", "RigidBody2D"}},
        {{"animator", "Animator2D"}},
        entity
    );
    assert(di_res.is_valid);

    // Step 4: Runtime Execution & Lifecycle
    auto player = std::make_unique<PlatformerPlayerNative>();
    player->entity_id = 1;
    player->task_scope_id = 1;

    // Tick update
    player->update(0.016f);
    assert(player->velocity_x == 220.0f);
    assert(player->velocity_y == 480.0f);

    // Step 5: Async Task Execution & Cancellation
    AsyncScheduler scheduler;
    auto respawn_task = player->respawn(scheduler);
    assert(player->is_respawning);
    assert(!player->anim_played);
    assert(scheduler.pending_tasks_count() == 1);

    // Step 0.3s (not completed yet)
    scheduler.step(0.3f, 0.3f);
    assert(player->is_respawning);
    assert(!player->anim_played);

    // Step 0.3s more (total 0.6s >= 0.5s -> completed!)
    scheduler.step(0.3f, 0.3f);
    assert(!player->is_respawning);
    assert(player->anim_played);
    assert(scheduler.pending_tasks_count() == 0);

    // Step 6: Hot Reload & Rollback Validation
    HotReloadManager hrm;
    hrm.on_snapshot_properties = [](const NativeBehaviour* b) {
        std::unordered_map<std::string, std::string> props;
        if (auto p = dynamic_cast<const PlatformerPlayerNative*>(b)) {
            props["walkSpeed"] = std::to_string(p->walkSpeed);
            props["jumpSpeed"] = std::to_string(p->jumpSpeed);
        }
        return props;
    };
    hrm.on_restore_properties = [](NativeBehaviour* b, const std::unordered_map<std::string, std::string>& props) {
        if (auto p = dynamic_cast<PlatformerPlayerNative*>(b)) {
            auto it_w = props.find("walkSpeed");
            if (it_w != props.end()) p->walkSpeed = std::stof(it_w->second);
            auto it_j = props.find("jumpSpeed");
            if (it_j != props.end()) p->jumpSpeed = std::stof(it_j->second);
        }
    };

    ModuleDescriptor mod_desc_v1;
    mod_desc_v1.module_name = "PlatformerModuleV1";
    mod_desc_v1.behaviour_types.push_back({"PlatformerPlayer", create_platformer_native});
    auto mod_v1 = std::make_shared<GameModule>(mod_desc_v1);
    assert(hrm.load_initial_module(mod_v1));

    player->walkSpeed = 350.0f; // Authored property edit
    hrm.register_instance(1, "PlatformerPlayer", std::move(player));

    // Hot reload into V2
    ModuleDescriptor mod_desc_v2 = mod_desc_v1;
    mod_desc_v2.module_name = "PlatformerModuleV2";
    auto mod_v2 = std::make_shared<GameModule>(mod_desc_v2);

    auto hr_start = std::chrono::high_resolution_clock::now();
    bool hr_ok = hrm.hot_reload(mod_v2, scheduler, di);
    auto hr_end = std::chrono::high_resolution_clock::now();
    double hr_ms = std::chrono::duration<double, std::milli>(hr_end - hr_start).count();

    assert(hr_ok);
    std::cout << "  [METRIC] Hot reload latency: " << hr_ms << " ms\n";

    auto reloaded_player = dynamic_cast<PlatformerPlayerNative*>(hrm.active_instances()[0].instance.get());
    assert(reloaded_player != nullptr);
    assert(reloaded_player->walkSpeed == 350.0f); // Restored!

    // Step 7: Scene Serialization round-trip
    std::unordered_map<std::string, std::string> authored_props = {
        {"walkSpeed", "350.0"},
        {"jumpSpeed", "480.0"}
    };
    std::string scene_json = workspace.serialize_behaviour_state("PlatformerPlayer", authored_props);
    assert(scene_json.find("\"walkSpeed\": \"350.0\"") != std::string::npos);

    auto restored_scene_props = workspace.deserialize_behaviour_state(scene_json);
    assert(restored_scene_props["walkSpeed"] == "350.0");
    assert(restored_scene_props["jumpSpeed"] == "480.0");

    auto end_time = std::chrono::high_resolution_clock::now();
    double total_spike_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    std::cout << "  [METRIC] Total Platformer Spike Execution: " << total_spike_ms << " ms\n";

    std::cout << "[PASS] EngineScriptPlatformerSpikeTests (Milestone R17) successfully completed!\n";
    return 0;
}
