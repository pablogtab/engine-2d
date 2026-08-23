#include <cassert>
#include <iostream>
#include "enginescript/editor/EngineScriptWorkspace.hpp"

using namespace EngineScript;

int main() {
    std::cout << "[TEST] Running EngineScriptEditorIntegrationTests...\n";

    EngineScriptWorkspace workspace("/workspace/MyGame");

    std::string player_source = R"(
        @behaviour
        export class PlatformerPlayer extends Behaviour {
            constructor(
                @inject private input: Input,
                @component private body: RigidBody2D
            ) {}

            @expose({ min: 0.0, max: 1000.0 })
            walkSpeed: Float = 250.0;
        }
    )";

    workspace.add_source_file("Source/Behaviours/PlatformerPlayer.e2s", player_source);

    CompilationResult res = workspace.compile();
    assert(res.success);
    assert(res.error_count == 0);
    assert(!res.metadata_json.empty());
    assert(res.generated_cpp.size() == 1);

    // Test scene serialization
    std::unordered_map<std::string, std::string> props = {{"walkSpeed", "320.0"}};
    std::string serialized = workspace.serialize_behaviour_state("PlatformerPlayer", props);

    assert(serialized.find("\"type\": \"PlatformerPlayer\"") != std::string::npos);
    assert(serialized.find("\"walkSpeed\": \"320.0\"") != std::string::npos);

    auto restored = workspace.deserialize_behaviour_state(serialized);
    assert(restored["walkSpeed"] == "320.0");

    std::cout << "[PASS] EngineScriptEditorIntegrationTests passed!\n";
    return 0;
}
