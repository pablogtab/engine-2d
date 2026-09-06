#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Engine {

struct LevelComposerLineRequest {
    glm::vec2 start{0.0f, 0.0f};
    glm::vec2 end{0.0f, 0.0f};
    glm::vec2 snap{0.0f, 0.0f};
    glm::vec2 origin{0.0f, 0.0f};
    float spacing{64.0f};
    bool include_end{true};
};

class LevelComposerService {
public:
    [[nodiscard]] static glm::vec2 snap_position(
        glm::vec2 position,
        glm::vec2 step,
        glm::vec2 origin = {0.0f, 0.0f}
    );

    [[nodiscard]] static std::vector<glm::vec2> line_placements(
        const LevelComposerLineRequest& request,
        std::string* error = nullptr
    );
};

} // namespace Engine
