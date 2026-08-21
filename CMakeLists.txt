cmake_minimum_required(VERSION 3.20)
project(Engine2D VERSION 0.1.0 LANGUAGES CXX)

enable_testing()

option(ENGINE_ENABLE_HARDWARE_TESTS
    "Register GPU tests that require a native graphics device"
    OFF
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies
find_package(SDL3 3.4 REQUIRED)
find_package(glm REQUIRED)

include(cmake/ShaderCompilation.cmake)

# Source files
set(SOURCES
    src/audio/AudioEngine.cpp
    src/core/Application.cpp
    src/core/AssetDatabase.cpp
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
    src/renderer/material/MaterialAsset.cpp
    src/renderer/material/SDLGPUMaterialBackend.cpp
    src/renderer/postprocess/PostProcessPipeline.cpp
    src/renderer/postprocess/PostProcessProfile.cpp
    src/renderer/Renderer2D.cpp
    src/renderer/Lighting2DSystem.cpp
    src/renderer/RenderEffectSystem.cpp
    src/renderer/RenderLayerEffectProfile.cpp
    src/renderer/RenderTargetPool.cpp
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
    src/scene/SceneValidator.cpp
    src/scene/EntitySubtreeArchive.cpp
    src/scene/PrefabSerializer.cpp
    src/scene/PrefabPropagation.cpp
    src/scene/PrefabOverrides.cpp
    src/ui/CanvasLayout.cpp
    src/ui/CanvasUISystem.cpp
    src/ui/UIInputSystem.cpp
    src/ui/UIComponentRegistrations.cpp
    src/ui/UITextLayout.cpp
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
    src/editor/panels/SceneDiagnosticsPanel.cpp
    src/editor/panels/AssetBrowserPanel.cpp
    src/editor/panels/PostProcessTunerPanel.cpp
    src/editor/panels/ProjectSettingsPanel.cpp
    src/editor/panels/ShortcutsPreferencesPanel.cpp
    src/editor/panels/TilemapPalettePanel.cpp
    src/editor/panels/SpriteAnimationStudioPanel.cpp
    src/editor/gizmos/TransformGizmo2D.cpp
    src/editor/gizmos/UILayoutGizmo.cpp
    src/editor/inspector/InspectorWidgets.cpp
    src/editor/inspector/SpriteInspectorDrawer.cpp
    src/editor/inspector/RenderEffectInspectorDrawer.cpp
    src/editor/inspector/RigidBodyInspectorDrawer.cpp
    src/editor/inspector/ColliderInspectorDrawer.cpp
    src/editor/inspector/LightingInspectorDrawer.cpp
    src/editor/inspector/UIInspectorDrawer.cpp
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
    src/editor/tools/ViewportLightingOverlay.cpp
    src/editor/tools/ViewportSelectionTool.cpp
    src/editor/tools/TilemapPaintingTool.cpp
    src/editor/state/EditorEntityStateStore.cpp
    src/editor/state/AutosaveRecovery.cpp
    src/editor/state/RecentSceneList.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/EditorPropertyTransaction.cpp
    src/editor/commands/EditorEntityStateCommands.cpp
    src/editor/commands/AssetFileCommands.cpp
    src/editor/commands/EntityCommands.cpp
    src/editor/commands/HierarchyCommands.cpp
    src/editor/commands/PrefabCommands.cpp
    src/editor/commands/ProjectSettingsCommands.cpp
    src/editor/commands/PostProcessProfileCommands.cpp
    src/editor/shortcuts/ShortcutManager.cpp
    src/editor/stage/PrefabStageController.cpp
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

set(ENGINE_RUNTIME_SOURCES ${SOURCES})
list(REMOVE_ITEM ENGINE_RUNTIME_SOURCES src/core/Application.cpp src/main.cpp)
list(FILTER ENGINE_RUNTIME_SOURCES EXCLUDE REGEX "^src/editor/")
list(FILTER ENGINE_RUNTIME_SOURCES EXCLUDE REGEX "^src/sandbox/")
list(FILTER ENGINE_RUNTIME_SOURCES EXCLUDE REGEX "^vendor/imgui/")

set(ENGINE_EDITOR_SOURCES ${SOURCES})
list(FILTER ENGINE_EDITOR_SOURCES INCLUDE REGEX "^(src/editor/|src/sandbox/|vendor/imgui/)")

add_library(EngineRuntime STATIC ${ENGINE_RUNTIME_SOURCES})
target_include_directories(EngineRuntime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)
target_link_libraries(EngineRuntime PUBLIC SDL3::SDL3 glm::glm box2d)

add_executable(Engine2D src/main.cpp src/core/Application.cpp ${ENGINE_EDITOR_SOURCES})
target_compile_definitions(Engine2D PRIVATE ENGINE_WITH_EDITOR=1)

add_executable(Engine2DStandalone src/runtime/StandaloneMain.cpp src/core/Application.cpp)
target_compile_definitions(Engine2DStandalone PRIVATE ENGINE_WITH_EDITOR=0)

if(APPLE)
    set(ENGINE_RUNTIME_SHADER_FORMAT "msl")
    set(ENGINE_RUNTIME_SHADER_EXTENSION "msl")
    set(ENGINE_RUNTIME_SHADER_ENTRY_POINT "main0")
elseif(WIN32)
    set(ENGINE_RUNTIME_SHADER_FORMAT "dxil")
    set(ENGINE_RUNTIME_SHADER_EXTENSION "dxil")
    set(ENGINE_RUNTIME_SHADER_ENTRY_POINT "main")
else()
    set(ENGINE_RUNTIME_SHADER_FORMAT "spirv")
    set(ENGINE_RUNTIME_SHADER_EXTENSION "spv")
    set(ENGINE_RUNTIME_SHADER_ENTRY_POINT "main")
endif()

add_custom_target(stage_runtime_assets
    COMMAND "${CMAKE_COMMAND}"
        "-DENGINE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DENGINE_STAGE_DIR=${CMAKE_BINARY_DIR}/runtime"
        "-DENGINE_EXECUTABLE=$<TARGET_FILE:Engine2DStandalone>"
        "-DENGINE_SHADER_VARIANT=${ENGINE_SHADER_OUTPUT_DIR}/night_vision.frag.${ENGINE_RUNTIME_SHADER_EXTENSION}"
        "-DENGINE_SHADER_FORMAT=${ENGINE_RUNTIME_SHADER_FORMAT}"
        "-DENGINE_SHADER_ENTRY_POINT=${ENGINE_RUNTIME_SHADER_ENTRY_POINT}"
        -P "${CMAKE_SOURCE_DIR}/cmake/StageRuntimeAssets.cmake"
    DEPENDS Engine2DStandalone compile_shaders
    COMMENT "Staging runtime assets with the platform shader variant"
    VERBATIM
)

if(APPLE)
    add_custom_target(package_macos_app
        COMMAND "${CMAKE_COMMAND}"
            "-DENGINE_STAGE_DIR=${CMAKE_BINARY_DIR}/runtime"
            "-DENGINE_APP_DIR=${CMAKE_BINARY_DIR}/dist/Engine2D.app"
            "-DENGINE_EXECUTABLE=$<TARGET_FILE:Engine2DStandalone>"
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageMacOSApp.cmake"
        DEPENDS stage_runtime_assets
        COMMENT "Packaging the standalone runtime as a self-contained macOS app"
        VERBATIM
    )

    add_custom_target(verify_macos_app
        COMMAND "${CMAKE_COMMAND}"
            "-DENGINE_APP_DIR=${CMAKE_BINARY_DIR}/dist/Engine2D.app"
            -P "${CMAKE_SOURCE_DIR}/cmake/VerifyMacOSApp.cmake"
        DEPENDS package_macos_app
        COMMENT "Verifying the packaged macOS standalone app"
        VERBATIM
    )
    add_custom_target(smoke_macos_export
        COMMAND "${CMAKE_COMMAND}"
            "-DENGINE_APP_DIR=${CMAKE_BINARY_DIR}/dist/Engine2D.app"
            "-DENGINE_SMOKE_DIR=${CMAKE_BINARY_DIR}/smoke/macos"
            -P "${CMAKE_SOURCE_DIR}/cmake/SmokeTestMacOSExport.cmake"
        DEPENDS verify_macos_app
        COMMENT "Running the isolated macOS export smoke test"
        VERBATIM
    )
    set(ENGINE_MACOS_X86_BUILD_DIR "${CMAKE_BINARY_DIR}/macos-x86_64" CACHE PATH
        "Secondary build directory used for the Intel macOS slice")
    add_custom_target(package_macos_universal
        COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_SOURCE_DIR}" -B "${ENGINE_MACOS_X86_BUILD_DIR}"
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_SOURCE_DIR}/cmake/toolchains/macos-x86_64.cmake"
            "-DENGINE_SHADERCROSS_EXECUTABLE=${engine_shadercross_command}"
            -DCMAKE_BUILD_TYPE=Release
        COMMAND "${CMAKE_COMMAND}" --build "${ENGINE_MACOS_X86_BUILD_DIR}" --target package_macos_app
        COMMAND "${CMAKE_COMMAND}"
            "-DENGINE_ARM_APP_DIR=${CMAKE_BINARY_DIR}/dist/Engine2D.app"
            "-DENGINE_X86_APP_DIR=${ENGINE_MACOS_X86_BUILD_DIR}/dist/Engine2D.app"
            "-DENGINE_UNIVERSAL_APP_DIR=${CMAKE_BINARY_DIR}/dist/Engine2D-universal.app"
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageUniversalMacOSApp.cmake"
        DEPENDS package_macos_app
        COMMENT "Packaging the standalone runtime as a universal macOS app"
        VERBATIM
    )
elseif(WIN32)
    get_filename_component(ENGINE_WINDOWS_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libgcc_s_seh-1.dll
        OUTPUT_VARIABLE ENGINE_WINDOWS_LIBGCC_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    get_filename_component(ENGINE_WINDOWS_RUNTIME_LIB_DIR "${ENGINE_WINDOWS_LIBGCC_PATH}" DIRECTORY)
    get_filename_component(ENGINE_WINDOWS_RUNTIME_ROOT "${ENGINE_WINDOWS_RUNTIME_LIB_DIR}/.." ABSOLUTE)
    string(JOIN "|" ENGINE_WINDOWS_DLL_SEARCH_PATHS
        "${ENGINE_WINDOWS_VCPKG_INSTALLED_DIR}/bin"
        "${ENGINE_WINDOWS_COMPILER_BIN_DIR}"
        "${ENGINE_WINDOWS_RUNTIME_LIB_DIR}"
        "${ENGINE_WINDOWS_RUNTIME_ROOT}/bin"
    )
    add_custom_target(package_windows_portable
        COMMAND "${CMAKE_COMMAND}"
            "-DENGINE_STAGE_DIR=${CMAKE_BINARY_DIR}/runtime"
            "-DENGINE_OUTPUT_DIR=${CMAKE_BINARY_DIR}/dist/Engine2D"
            "-DENGINE_EXECUTABLE_NAME=$<TARGET_FILE_NAME:Engine2DStandalone>"
            "-DENGINE_OBJDUMP_EXECUTABLE=${CMAKE_OBJDUMP}"
            "-DENGINE_DLL_SEARCH_PATHS=${ENGINE_WINDOWS_DLL_SEARCH_PATHS}"
            -P "${CMAKE_SOURCE_DIR}/cmake/PackageWindowsPortable.cmake"
        DEPENDS stage_runtime_assets
        COMMENT "Packaging the standalone runtime as a portable Windows folder"
        VERBATIM
    )
    add_custom_target(verify_windows_portable
        COMMAND "${CMAKE_COMMAND}"
            "-DENGINE_OUTPUT_DIR=${CMAKE_BINARY_DIR}/dist/Engine2D"
            "-DENGINE_OBJDUMP_EXECUTABLE=${CMAKE_OBJDUMP}"
            -P "${CMAKE_SOURCE_DIR}/cmake/VerifyWindowsPortable.cmake"
        DEPENDS package_windows_portable
        COMMENT "Verifying the portable Windows runtime folder"
        VERBATIM
    )
    add_custom_target(package_windows_zip
        COMMAND "${CMAKE_COMMAND}" -E tar cfv
            "${CMAKE_BINARY_DIR}/dist/Engine2D-windows-x64.zip"
            --format=zip Engine2D
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/dist"
        DEPENDS verify_windows_portable
        COMMENT "Creating the distributable Windows x64 ZIP"
        VERBATIM
    )
endif()

add_test(NAME RuntimeBoundaryTests
    COMMAND "${CMAKE_COMMAND}"
        "-DENGINE_EXECUTABLE=$<TARGET_FILE:Engine2DStandalone>"
        -P "${CMAKE_SOURCE_DIR}/cmake/VerifyRuntimeBoundary.cmake"
)

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

add_executable(AssetDatabaseTests
    tests/AssetDatabaseTests.cpp
    src/core/AssetDatabase.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
)
target_include_directories(AssetDatabaseTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(AssetDatabaseTests PRIVATE SDL3::SDL3)
add_test(NAME AssetDatabaseTests COMMAND AssetDatabaseTests)

add_executable(AutosaveRecoveryTests
    tests/AutosaveRecoveryTests.cpp
    src/editor/state/AutosaveRecovery.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
)
target_include_directories(AutosaveRecoveryTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(AutosaveRecoveryTests PRIVATE SDL3::SDL3)
add_test(NAME AutosaveRecoveryTests COMMAND AutosaveRecoveryTests)

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
    src/core/AssetDatabase.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/core/ThreadPool.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/PostProcessProfileCommands.cpp
    src/ecs/EntityManager.cpp
    src/renderer/Font.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/material/MaterialAsset.cpp
    src/renderer/Renderer2D.cpp
    src/renderer/RenderLayerEffectProfile.cpp
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

add_executable(RenderPipelineTests tests/RenderPipelineTests.cpp)
target_include_directories(RenderPipelineTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(RenderPipelineTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME RenderPipelineTests COMMAND RenderPipelineTests)

add_executable(LocalEffectsTests tests/LocalEffectsTests.cpp)
target_include_directories(LocalEffectsTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(LocalEffectsTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME LocalEffectsTests COMMAND LocalEffectsTests)

add_executable(RenderMetricsTests tests/RenderMetricsTests.cpp)
target_include_directories(RenderMetricsTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(RenderMetricsTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME RenderMetricsTests COMMAND RenderMetricsTests)

add_executable(Lighting2DTests
    tests/Lighting2DTests.cpp
    src/editor/commands/EditorCommandHistory.cpp
)
target_include_directories(Lighting2DTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(Lighting2DTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME Lighting2DTests COMMAND Lighting2DTests)

add_executable(CanvasLayoutTests tests/CanvasLayoutTests.cpp)
target_include_directories(CanvasLayoutTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(CanvasLayoutTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME CanvasLayoutTests COMMAND CanvasLayoutTests)

add_executable(UIInputTests tests/UIInputTests.cpp)
target_include_directories(UIInputTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(UIInputTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME UIInputTests COMMAND UIInputTests)

add_executable(CanvasUIRenderTests tests/CanvasUIRenderTests.cpp)
target_include_directories(CanvasUIRenderTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(CanvasUIRenderTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME CanvasUIRenderTests COMMAND CanvasUIRenderTests)

add_executable(GPURenderSmokeTests tests/GPURenderSmokeTests.cpp)
target_include_directories(GPURenderSmokeTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_compile_definitions(GPURenderSmokeTests PRIVATE
    ENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    ENGINE_SHADER_OUTPUT_DIR="${ENGINE_SHADER_OUTPUT_DIR}"
)
target_link_libraries(GPURenderSmokeTests PRIVATE SDL3::SDL3 glm::glm)
if(ENGINE_ENABLE_HARDWARE_TESTS)
    if(NOT APPLE)
        add_dependencies(GPURenderSmokeTests compile_shaders)
    endif()
    add_test(NAME GPURenderSmokeTests COMMAND GPURenderSmokeTests)
    set_tests_properties(GPURenderSmokeTests PROPERTIES
        LABELS "gpu;hardware"
        SKIP_RETURN_CODE 77
        TIMEOUT 30
    )
endif()

add_test(
    NAME RuntimeShaderStagingTests
    COMMAND ${CMAKE_COMMAND}
        "-DENGINE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DENGINE_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-output"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RuntimeShaderStagingTests.cmake"
)

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

add_executable(AssetFileCommandTests
    tests/AssetFileCommandTests.cpp
    src/core/AssetDatabase.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/editor/commands/AssetFileCommands.cpp
    src/editor/commands/EditorCommandHistory.cpp
)
target_include_directories(AssetFileCommandTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(AssetFileCommandTests PRIVATE SDL3::SDL3)
add_test(NAME AssetFileCommandTests COMMAND AssetFileCommandTests)

add_executable(InspectorCommandTests
    tests/InspectorCommandTests.cpp
    src/core/AssetDatabase.cpp
    src/core/AssetManager.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/editor/state/EditorEntityStateStore.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/EditorPropertyTransaction.cpp
    src/editor/commands/EntityCommands.cpp
    src/renderer/Texture.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/material/MaterialAsset.cpp
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

add_executable(SceneValidatorTests
    tests/SceneValidatorTests.cpp
    src/ecs/EntityManager.cpp
    src/scene/SceneValidator.cpp
)
target_include_directories(SceneValidatorTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(SceneValidatorTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME SceneValidatorTests COMMAND SceneValidatorTests)

add_executable(EditorEntityStateStoreTests
    tests/EditorEntityStateStoreTests.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/editor/state/EditorEntityStateStore.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/EditorEntityStateCommands.cpp
)
target_include_directories(EditorEntityStateStoreTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(EditorEntityStateStoreTests PRIVATE glm::glm)
add_test(NAME EditorEntityStateStoreTests COMMAND EditorEntityStateStoreTests)

add_executable(RecentSceneListTests
    tests/RecentSceneListTests.cpp
    src/core/Log.cpp
    src/editor/state/RecentSceneList.cpp
)
target_include_directories(RecentSceneListTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
target_link_libraries(RecentSceneListTests PRIVATE SDL3::SDL3)
add_test(NAME RecentSceneListTests COMMAND RecentSceneListTests)

add_executable(HierarchyCommandTests
    tests/HierarchyCommandTests.cpp
    src/core/AssetDatabase.cpp
    src/core/AssetManager.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/EntityCommands.cpp
    src/editor/commands/HierarchyCommands.cpp
    src/editor/commands/EditorEntityStateCommands.cpp
    src/editor/state/EditorEntityStateStore.cpp
    src/renderer/Texture.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/material/MaterialAsset.cpp
    src/renderer/SpriteAnimationSystem.cpp
    src/scene/BuiltinComponentRegistrations.cpp
    src/scene/ComponentRegistry.cpp
    src/scene/SceneSchema.cpp
    src/tilemap/Tilemap.cpp
    src/tilemap/TilemapColliderSystem.cpp
    vendor/stb/stb_impl.cpp
)
target_include_directories(HierarchyCommandTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)
target_link_libraries(HierarchyCommandTests PRIVATE SDL3::SDL3 glm::glm box2d)
add_test(NAME HierarchyCommandTests COMMAND HierarchyCommandTests)

add_executable(ViewportVisibilityTests
    tests/ViewportVisibilityTests.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/editor/state/EditorEntityStateStore.cpp
    src/editor/tools/ViewportSelectionTool.cpp
    src/scene/Camera2D.cpp
    src/ui/CanvasLayout.cpp
    src/renderer/Texture.cpp
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/stb/stb_impl.cpp
)
target_include_directories(ViewportVisibilityTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
)
target_link_libraries(ViewportVisibilityTests PRIVATE SDL3::SDL3 glm::glm)
add_test(NAME ViewportVisibilityTests COMMAND ViewportVisibilityTests)

add_executable(SceneSerializerTests
    tests/SceneSerializerTests.cpp
    src/core/AssetDatabase.cpp
    src/core/AssetManager.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/renderer/Texture.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/material/MaterialAsset.cpp
    src/renderer/SpriteAnimationSystem.cpp
    src/scene/BuiltinComponentRegistrations.cpp
    src/scene/ComponentRegistry.cpp
    src/scene/SceneSchema.cpp
    src/scene/SceneSerializer.cpp
    src/editor/commands/EditorCommandHistory.cpp
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
    src/core/AssetDatabase.cpp
    src/core/AssetManager.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/renderer/Texture.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/material/MaterialAsset.cpp
    src/renderer/material/SDLGPUMaterialBackend.cpp
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
    src/core/AssetDatabase.cpp
    src/core/AssetManager.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/renderer/Texture.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/material/MaterialAsset.cpp
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

add_executable(PrefabTests
    tests/PrefabTests.cpp
    src/core/AssetDatabase.cpp
    src/core/AssetManager.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/ecs/EntityManager.cpp
    src/ecs/CameraSystem.cpp
    src/renderer/Texture.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/material/MaterialAsset.cpp
    src/renderer/SpriteAnimationSystem.cpp
    src/scene/BuiltinComponentRegistrations.cpp
    src/scene/ComponentRegistry.cpp
    src/scene/SceneSchema.cpp
    src/scene/SceneSerializer.cpp
    src/scene/EntitySubtreeArchive.cpp
    src/scene/PrefabSerializer.cpp
    src/scene/PrefabPropagation.cpp
    src/scene/PrefabOverrides.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/EntityCommands.cpp
    src/editor/commands/HierarchyCommands.cpp
    src/editor/commands/PrefabCommands.cpp
    src/editor/commands/EditorEntityStateCommands.cpp
    src/editor/state/EditorEntityStateStore.cpp
    src/tilemap/Tilemap.cpp
    src/tilemap/TilemapColliderSystem.cpp
    vendor/stb/stb_impl.cpp
)
target_include_directories(PrefabTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)
target_link_libraries(PrefabTests PRIVATE SDL3::SDL3 glm::glm box2d)
add_test(NAME PrefabTests COMMAND PrefabTests)

add_executable(PrefabStageControllerTests
    tests/PrefabStageControllerTests.cpp
    src/core/AssetDatabase.cpp
    src/core/AssetManager.cpp
    src/core/FileSystem.cpp
    src/core/Log.cpp
    src/core/Input.cpp
    src/core/Time.cpp
    src/core/ThreadPool.cpp
    src/ecs/EntityManager.cpp
    src/ecs/CameraSystem.cpp
    src/renderer/Texture.cpp
    src/renderer/material/FragmentShader.cpp
    src/renderer/material/MaterialAsset.cpp
    src/renderer/Renderer2D.cpp
    src/renderer/SpriteAnimationSystem.cpp
    src/renderer/Animation2D.cpp
    src/renderer/Font.cpp
    src/renderer/SpriteSheet.cpp
    src/renderer/postprocess/PostProcessPipeline.cpp
    src/renderer/postprocess/PostProcessProfile.cpp
    src/audio/AudioEngine.cpp
    src/scene/Camera2D.cpp
    src/scene/CameraController2D.cpp
    src/scene/BuiltinComponentRegistrations.cpp
    src/scene/ComponentRegistry.cpp
    src/scene/SceneSchema.cpp
    src/scene/SceneSerializer.cpp
    src/scene/EntitySubtreeArchive.cpp
    src/scene/PrefabSerializer.cpp
    src/scene/PrefabPropagation.cpp
    src/scene/PrefabOverrides.cpp
    src/scene/SceneManager.cpp


    src/scene/GenericScene.cpp
    src/editor/commands/EditorCommandHistory.cpp
    src/editor/commands/EntityCommands.cpp
    src/editor/commands/HierarchyCommands.cpp
    src/editor/commands/PrefabCommands.cpp
    src/editor/commands/EditorEntityStateCommands.cpp
    src/editor/state/EditorEntityStateStore.cpp
    src/editor/stage/PrefabStageController.cpp
    src/tilemap/Tilemap.cpp
    src/tilemap/TilemapColliderSystem.cpp
    vendor/stb/stb_impl.cpp
    vendor/miniaudio/miniaudio_impl.cpp
)
target_include_directories(PrefabStageControllerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)
target_link_libraries(PrefabStageControllerTests PRIVATE SDL3::SDL3 glm::glm box2d)
add_test(NAME PrefabStageControllerTests COMMAND PrefabStageControllerTests)

function(engine_use_runtime_for_test target)
    get_target_property(test_sources ${target} SOURCES)
    foreach(runtime_source IN LISTS ENGINE_RUNTIME_SOURCES)
        list(REMOVE_ITEM test_sources "${runtime_source}")
    endforeach()
    set_property(TARGET ${target} PROPERTY SOURCES "${test_sources}")

    get_target_property(test_libraries ${target} LINK_LIBRARIES)
    if(test_libraries STREQUAL "test_libraries-NOTFOUND")
        set(test_libraries)
    endif()
    list(REMOVE_ITEM test_libraries SDL3::SDL3 glm::glm box2d)
    set_property(TARGET ${target} PROPERTY LINK_LIBRARIES "${test_libraries}")
    target_link_libraries(${target} PRIVATE EngineRuntime)
endfunction()

foreach(runtime_test
    Camera2DTests
    DisplayManagerTests
    AssetDatabaseTests
    DisplayPersistenceTests
    PostProcessTests
    RenderPipelineTests
    LocalEffectsTests
    RenderMetricsTests
    Lighting2DTests
    CanvasLayoutTests
    UIInputTests
    CanvasUIRenderTests
    GPURenderSmokeTests
    EditorPropertyTransactionTests
    EditorCommandHistoryTests
    AssetFileCommandTests
    InspectorCommandTests
    AnimationSystemTests
    EntityManagerTests
    SceneValidatorTests
    EditorEntityStateStoreTests
    RecentSceneListTests
    HierarchyCommandTests
    ViewportVisibilityTests
    SceneSerializerTests
    ComponentRegistryTests
    SceneMigrationTests
    PrefabTests
    PrefabStageControllerTests
)
    engine_use_runtime_for_test(${runtime_test})
endforeach()


target_include_directories(Engine2D PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)

target_link_libraries(Engine2D PRIVATE
    EngineRuntime
)

target_include_directories(Engine2DStandalone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)
target_link_libraries(Engine2DStandalone PRIVATE EngineRuntime)

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
