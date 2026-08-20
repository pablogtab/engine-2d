include(ExternalProject)

set(ENGINE_SHADERCROSS_EXECUTABLE "" CACHE FILEPATH
    "Path to a shadercross executable; leave empty to build the pinned toolchain")

set(ENGINE_SHADERCROSS_REVISION "e55cf5e31ced6f3d1be5cc6d0c50e99384f9f4ba")
set(ENGINE_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shader_assets")
set(ENGINE_SHADER_SOURCE "${CMAKE_SOURCE_DIR}/assets/shaders/night_vision.frag.hlsl")

if(ENGINE_SHADERCROSS_EXECUTABLE)
    set(engine_shadercross_command "${ENGINE_SHADERCROSS_EXECUTABLE}")
else()
    ExternalProject_Add(engine_shadercross_toolchain
        EXCLUDE_FROM_ALL TRUE
        PREFIX "${CMAKE_BINARY_DIR}/toolchains/shadercross"
        GIT_REPOSITORY "https://github.com/libsdl-org/SDL_shadercross.git"
        GIT_TAG "${ENGINE_SHADERCROSS_REVISION}"
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE TRUE
        UPDATE_DISCONNECTED TRUE
        CMAKE_ARGS
            "-DSDLSHADERCROSS_VENDORED=ON"
            "-DSDLSHADERCROSS_CLI=ON"
            "-DSDLSHADERCROSS_SHARED=OFF"
            "-DSDLSHADERCROSS_STATIC=ON"
            "-DSDLSHADERCROSS_INSTALL=OFF"
            "-DSDLSHADERCROSS_TESTS=OFF"
            "-DSDLSHADERCROSS_WERROR=OFF"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel 4
        BUILD_BYPRODUCTS "<BINARY_DIR>/shadercross"
        INSTALL_COMMAND ""
    )
    ExternalProject_Get_Property(engine_shadercross_toolchain BINARY_DIR)
    set(engine_shadercross_command "${BINARY_DIR}/shadercross")
endif()

function(engine_add_shader_variant format extension)
    set(output "${ENGINE_SHADER_OUTPUT_DIR}/night_vision.frag.${extension}")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${ENGINE_SHADER_OUTPUT_DIR}"
        COMMAND "${engine_shadercross_command}" "${ENGINE_SHADER_SOURCE}"
            --source HLSL --dest "${format}" --stage fragment --entrypoint main --output "${output}"
        DEPENDS "${ENGINE_SHADER_SOURCE}"
        COMMENT "Compiling Night Vision ${format} shader variant"
        VERBATIM
    )
    set(ENGINE_SHADER_VARIANTS "${ENGINE_SHADER_VARIANTS};${output}" PARENT_SCOPE)
endfunction()

engine_add_shader_variant(SPIRV spv)
engine_add_shader_variant(DXIL dxil)
engine_add_shader_variant(MSL msl)

add_custom_target(compile_shaders DEPENDS ${ENGINE_SHADER_VARIANTS})
if(TARGET engine_shadercross_toolchain)
    add_dependencies(compile_shaders engine_shadercross_toolchain)
endif()

add_custom_target(validate_compiled_shaders
    COMMAND "${CMAKE_COMMAND}"
        "-DENGINE_SHADER_OUTPUT_DIR=${ENGINE_SHADER_OUTPUT_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/ValidateCompiledShaders.cmake"
    DEPENDS compile_shaders
    COMMENT "Validating compiled Night Vision shader variants"
    VERBATIM
)
