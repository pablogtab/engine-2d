cmake_minimum_required(VERSION 3.20)
project(Engine2D VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies
find_package(SDL3 REQUIRED)
find_package(glm REQUIRED)

# Source files
set(SOURCES
    src/core/Application.cpp
    src/core/AssetManager.cpp
    src/core/Input.cpp
    src/core/Log.cpp
    src/core/ThreadPool.cpp
    src/core/Window.cpp
    src/ecs/EntityManager.cpp
    src/renderer/Animation2D.cpp
    src/renderer/ParticleSystem2D.cpp
    src/renderer/Renderer2D.cpp
    src/renderer/SpriteSheet.cpp
    src/renderer/Texture.cpp
    src/scene/Camera2D.cpp
    src/scene/SceneManager.cpp
    src/sandbox/AnimatedSpriteDemoScene.cpp
    src/sandbox/DepthDemoScene.cpp
    src/sandbox/PlatformerDemoScene.cpp
    src/sandbox/StressTestScene.cpp
    vendor/stb/stb_impl.cpp
    src/main.cpp
)

# Build vendored Box2D library
set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/box2d)

add_executable(Engine2D ${SOURCES})

target_include_directories(Engine2D PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/box2d/include
)

target_link_libraries(Engine2D PRIVATE 
    SDL3::SDL3
    glm::glm
    box2d
)

# Automatically copy compile_commands.json to workspace root for IDE / IntelliSense
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
)
