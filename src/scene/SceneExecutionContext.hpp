#pragma once

#include "EditorSceneView.hpp"
#include "SceneRenderer.hpp"
#include "SceneRuntime.hpp"

namespace Engine {

struct SceneExecutionContext {
    SceneRuntime runtime;
    SceneRenderer renderer;
    EditorSceneView editor_view;
    bool attached{false};
};

} // namespace Engine
