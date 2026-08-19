cmake_minimum_required(VERSION 3.20)
project(Engine2D VERSION 0.1.0 LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies
find_package(SDL3 3.4 REQUIRED)
find_package(glm REQUIRED)

# Source files
set(SOURCES
    src/audio/AudioEngine.cpp
    src/core/Application.cpp
    src/core/AssetManager.cpp
    src/core/BinarySerializer.cpp
    src/core/DisplayManager.cpp
    src/core/FileSystem.cpp
    src/core/Input.cpp
    src/core/Log.cpp
    src/core/PlayerPrefs.cpp
    src/core/ProjectSettings.cpp
    src/core/SDLDisplayBackend.cpp
    src/core/SaveSystem.cpp
    src/core/ThreadPool.cpp
    src/core/Time.cpp
    src/core/Window.cpp
    src/ecs/CameraSystem.cpp
    src/ecs/EntityManager.cpp
    src/particles/ParticleSystem2D.cpp
    src/renderer/Animation2D.cpp
    src/renderer/Font.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/material/SDLGPUMaterialBackend.cpp
    src/renderer/postprocess/PostProcessPipeline.cpp
    src/renderer/postprocess/PostProcessProfile.cpp
    src/renderer/Renderer2D.cpp
    src/renderer/SpriteAnimationSystem.cpp
    src/renderer/SpriteSheet.cpp
    src/renderer/Texture.cpp
    src/scene/Camera2D.cpp
    src/scene/CameraController2D.cpp
    src/scene/BuiltinComponentRegistrations.cpp
    src/scene/ComponentRegistry.cpp
    src/scene/SceneManager.cpp
    src/scene/GenericScene.cpp
    src/scene/SceneSchema.cpp
    src/scene/SceneSerializer.cpp
    src/sandbox/AnimatedSpriteDemoScene.cpp
    src/sandbox/DepthDemoScene.cpp
    src/sandbox/PlatformerDemoScene.cpp
    src/sandbox/PostProcessValidationScene.cpp
    src/sandbox/StressTestScene.cpp
    src/editor/EditorTheme.cpp
    src/editor/EditorLayer.cpp
    src/editor/panels/ViewportPanel.cpp
    src/editor/panels/SceneHierarchyPanel.cpp
    src/editor/panels/InspectorPanel.cpp
    src/editor/panels/ConsolePanel.cpp
    src/editor/panels/StatsPanel.cpp
    src/editor/panels/AssetBrowserPanel.cpp
    src/editor/panels/PostProcessTunerPanel.cpp
    src/editor/panels/ProjectSettingsPanel.cpp
    src/editor/panels/ShortcutsPreferencesPanel.cpp
    src/editor/panels/TilemapPalettePanel.cpp
    src/editor/panels/SpriteAnimationStudioPanel.cpp
    src/editor/gizmos/TransformGizmo2D.cpp
    src/editor/inspector/InspectorWidgets.cpp
    src/editor/inspector/SpriteInspectorDrawer.cpp
    src/editor/inspector/RigidBodyInspectorDrawer.cpp
    src/editor/inspector/ColliderInspectorDrawer.cpp
    src/editor/inspector/TilemapInspectorDrawer.cpp
    src/editor/inspector/AnimationInspectorDrawer.cpp
    src/editor/inspector/EditorComponentRegistry.cpp
    src/editor/inspector/BuiltinEditorComponentRegistrations.cpp
    src/editor/inspector/TransformInspectorDrawer.cpp
    src/editor/inspector/CameraInspectorDrawer.cpp
    src/editor/tools/ViewportCameraController.cpp
    src/editor/tools/ViewportArrangementTool.cpp
    src/editor/tools/EditorIcons.cpp
    src/editor/tools/EditorToolbox.cpp
    src/editor/tools/ViewportReferenceOverlay.cpp
    src/editor/tools/ViewportColliderOverlay.cpp
    src/editor/tools/ViewportSelectionTool.cpp
    src/editor/tools/TilemapPaintingTool.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/EditorPropertyTransaction.cpp
    src/editor/commands/EntityCommands.cpp
    src/editor/commands/ProjectSettingsCommands.cpp
    src/editor/commands/PostProcessProfileCommands.cpp
    src/editor/shortcuts/ShortcutManager.cpp
    src/tilemap/Tilemap.cpp
    src/tilemap/TilemapColliderSystem.cpp
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_sdl3.cpp
    vendor/imgui/backends/imgui_impl_sdlrenderer3.cpp
    vendor/stb/stb_impl.cpp
    vendor/miniaudio/miniaudio_impl.cpp
    src/main.cpp
)

# Build vendored Box2D library
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/box2d)

add_executable(Engine2D ${SOURCES})

add_executable(Camera2DTests
    tests/Camera2DTests.cpp
    src/scene/Camera2D.cpp
)
target_include_directories(Camera2DTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(Camera2DTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME Camera2DTests COMMAND Camera2DTests)

add_executable(DisplayManagerTests
    tests/DisplayManagerTests.cpp
    src/core/DisplayManager.cpp
)
target_include_directories(DisplayManagerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME DisplayManagerTests COMMAND DisplayManagerTests)

add_executable(DisplayPersistenceTests
    tests/DisplayPersistenceTests.cpp
    src/core/BinarySerializer.cpp
    src/core/DisplayManager.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/core/PlayerPrefs.cpp
    src/core/ProjectSettings.cpp
    src/core/SaveSystem.cpp
)
target_include_directories(DisplayPersistenceTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(DisplayPersistenceTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME DisplayPersistenceTests COMMAND DisplayPersistenceTests)

add_executable(PostProcessTests
    tests/PostProcessTests.cpp
    src/core/Log.cpp
    src/core/ThreadPool.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/PostProcessProfileCommands.cpp
    src/renderer/Font.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/Renderer2D.cpp
    src/renderer/Texture.cpp
    src/renderer/postprocess/PostProcessPipeline.cpp
    src/renderer/postprocess/PostProcessProfile.cpp
    src/scene/Camera2D.cpp
    vendor/stb/stb_impl.cpp
)
target_include_directories(PostProcessTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_compile_definitions(PostProcessTests PRIVATE
    ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(PostProcessTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME PostProcessTests COMMAND PostProcessTests)

add_executable(EditorPropertyTransactionTests
    tests/EditorPropertyTransactionTests.cpp
    src/core/Log.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/EditorPropertyTransaction.cpp
)
target_include_directories(EditorPropertyTransactionTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME EditorPropertyTransactionTests COMMAND EditorPropertyTransactionTests)

add_executable(EditorCommandHistoryTests
    tests/EditorCommandHistoryTests.cpp
    src/core/Log.cpp
    src/editor/commands/EditorCommandHistory.cpp
)
target_include_directories(EditorCommandHistoryTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
add_test(NAME EditorCommandHistoryTests COMMAND EditorCommandHistoryTests)

add_executable(InspectorCommandTests
    tests/InspectorCommandTests.cpp
    src/core/AssetManager.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/EditorPropertyTransaction.cpp
    src/editor/commands/EntityCommands.cpp
    src/renderer/Texture.cpp
    src/renderer/SpriteAnimationSystem.cpp
    src/scene/BuiltinComponentRegistrations.cpp
    src/scene/ComponentRegistry.cpp
    src/tilemap/TilemapColliderSystem.cpp
    vendor/stb/stb_impl.cpp
)
target_include_directories(InspectorCommandTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)
target_link_libraries(InspectorCommandTests PRIVATE SDL3::SDL3 glm::glm box2d)
add_test(NAME InspectorCommandTests COMMAND InspectorCommandTests)

add_executable(AnimationSystemTests
    tests/AnimationSystemTests.cpp
    src/ecs/EntityManager.cpp
    src/renderer/SpriteAnimationSystem.cpp
)
target_include_directories(AnimationSystemTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(AnimationSystemTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME AnimationSystemTests COMMAND AnimationSystemTests)

add_executable(EntityManagerTests
    tests/EntityManagerTests.cpp
    src/ecs/EntityManager.cpp
)
target_include_directories(EntityManagerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(EntityManagerTests PRIVATE glm::glm)
add_test(NAME EntityManagerTests COMMAND EntityManagerTests)

add_executable(SceneSerializerTests
    tests/SceneSerializerTests.cpp
    src/core/AssetManager.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/renderer/Texture.cpp
    src/renderer/SpriteAnimationSystem.cpp
    src/scene/BuiltinComponentRegistrations.cpp
    src/scene/ComponentRegistry.cpp
    src/scene/SceneSchema.cpp
    src/scene/SceneSerializer.cpp
    src/tilemap/Tilemap.cpp
    src/tilemap/TilemapColliderSystem.cpp
    vendor/stb/stb_impl.cpp
)
target_include_directories(SceneSerializerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)
target_compile_definitions(SceneSerializerTests PRIVATE
    ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(SceneSerializerTests PRIVATE SDL3::SDL3 glm::glm box2d)
add_test(NAME SceneSerializerTests COMMAND SceneSerializerTests)

add_executable(ComponentRegistryTests
    tests/ComponentRegistryTests.cpp
    src/core/AssetManager.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/renderer/Texture.cpp
    src/renderer/SpriteAnimationSystem.cpp
    src/scene/BuiltinComponentRegistrations.cpp
    src/scene/ComponentRegistry.cpp
    src/editor/inspector/EditorComponentRegistry.cpp
    src/tilemap/TilemapColliderSystem.cpp
    vendor/stb/stb_impl.cpp
)
target_include_directories(ComponentRegistryTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
)
target_link_libraries(ComponentRegistryTests PRIVATE SDL3::SDL3 glm::glm box2d)
add_test(NAME ComponentRegistryTests COMMAND ComponentRegistryTests)

add_executable(SceneMigrationTests
    tests/SceneMigrationTests.cpp
    src/core/AssetManager.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/renderer/Texture.cpp
    src/renderer/SpriteAnimationSystem.cpp
    src/scene/BuiltinComponentRegistrations.cpp
    src/scene/ComponentRegistry.cpp
    src/scene/SceneSchema.cpp
    src/tilemap/TilemapColliderSystem.cpp
    vendor/stb/stb_impl.cpp
)
target_include_directories(SceneMigrationTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(SceneMigrationTests PRIVATE SDL3::SDL3 glm::glm box2d)
add_test(NAME SceneMigrationTests COMMAND SceneMigrationTests)

target_include_directories(Engine2D PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)

target_link_libraries(Engine2D PRIVATE
    SDL3::SDL3
    glm::glm
    box2d
)

# Lucide is an editor-only visual dependency. Keep it next to the editor
# executable instead of copying it into game assets or standalone packages.
add_custom_command(TARGET Engine2D POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:Engine2D>/editor_assets/lucide"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lucide/lucide.ttf"
        "$<TARGET_FILE_DIR:Engine2D>/editor_assets/lucide/lucide.ttf"
    VERBATIM
)

if (WIN32 AND MINGW)
    target_link_options(Engine2D PRIVATE -static-libgcc -static-libstdc++)
endif()

# Automatically copy compile_commands.json to workspace root for IDE / IntelliSense
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
)
