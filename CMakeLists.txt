cmake_minimum_required(VERSION 3.20)
project(Engine2D VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies
find_package(SDL3 REQUIRED)
find_package(glm REQUIRED)

# Source files
file(GLOB_RECURSE SOURCES
    src/core/*.cpp
    src/renderer/*.cpp
    src/scene/*.cpp
    src/sandbox/*.cpp
    vendor/*.cpp
    src/main.cpp
)

add_executable(Engine2D ${SOURCES})

target_include_directories(Engine2D PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

target_link_libraries(Engine2D PRIVATE 
    SDL3::SDL3
    glm::glm
)

# Automatically copy compile_commands.json to workspace root for IDE / IntelliSense
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_BINARY_DIR}/compile_commands.json
    ${CMAKE_SOURCE_DIR}/compile_commands.json
)
