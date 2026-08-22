include(ExternalProject)

set(ENGINE_SHADERCROSS_EXECUTABLE "" CACHE FILEPATH
    "Path to a shadercross executable; leave empty to build the pinned toolchain")

set(ENGINE_SHADERCROSS_REVISION "e55cf5e31ced6f3d1be5cc6d0c50e99384f9f4ba")
set(ENGINE_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shader_assets")
set(ENGINE_RUNTIME_SHADER_SOURCE "")
set(ENGINE_RUNTIME_SHADER_DESCRIPTOR "")
set(ENGINE_RUNTIME_SHADER_VARIANT_BASE "")
set(ENGINE_HAS_RUNTIME_SHADER OFF)
if(ENGINE_PROJECT_DIR AND EXISTS "${ENGINE_PROJECT_DIR}/project.engine2d")
    file(READ "${ENGINE_PROJECT_DIR}/project.engine2d" engine_project_json)
    string(JSON runtime_shader_type ERROR_VARIABLE runtime_shader_error
        TYPE "${engine_project_json}" runtime_shader)
    if(NOT runtime_shader_error)
        string(JSON ENGINE_RUNTIME_SHADER_SOURCE ERROR_VARIABLE shader_source_error
            GET "${engine_project_json}" runtime_shader source)
        string(JSON ENGINE_RUNTIME_SHADER_DESCRIPTOR ERROR_VARIABLE shader_descriptor_error
            GET "${engine_project_json}" runtime_shader descriptor)
        string(JSON ENGINE_RUNTIME_SHADER_VARIANT_BASE ERROR_VARIABLE shader_variant_error
            GET "${engine_project_json}" runtime_shader variant_base)
        if(shader_source_error OR shader_descriptor_error OR shader_variant_error)
            message(FATAL_ERROR "runtime_shader requires source, descriptor, and variant_base")
        endif()
        set(ENGINE_HAS_RUNTIME_SHADER ON)
    else()
        message(STATUS "Project has no runtime_shader; shader compilation will be skipped")
    endif()
endif()
set(ENGINE_SHADER_SOURCE "${ENGINE_PROJECT_DIR}/${ENGINE_RUNTIME_SHADER_SOURCE}")
get_filename_component(ENGINE_SHADER_OUTPUT_BASENAME "${ENGINE_RUNTIME_SHADER_VARIANT_BASE}" NAME)

if(NOT ENGINE_PROJECT_DIR OR NOT EXISTS "${ENGINE_PROJECT_DIR}/project.engine2d")
    message(STATUS "No Engine2D project selected; runtime shader/export targets require -DENGINE_PROJECT_DIR=<path>")
endif()

if(ENGINE_HAS_RUNTIME_SHADER)
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
        set(output "${ENGINE_SHADER_OUTPUT_DIR}/${ENGINE_SHADER_OUTPUT_BASENAME}.${extension}")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${ENGINE_SHADER_OUTPUT_DIR}"
            COMMAND "${engine_shadercross_command}" "${ENGINE_SHADER_SOURCE}"
                --source HLSL --dest "${format}" --stage fragment --entrypoint main --output "${output}"
            DEPENDS "${ENGINE_SHADER_SOURCE}"
            COMMENT "Compiling ${ENGINE_SHADER_OUTPUT_BASENAME} ${format} shader variant"
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
        COMMENT "Validating compiled shader variants"
        VERBATIM
    )
else()
    add_custom_target(compile_shaders
        COMMAND "${CMAKE_COMMAND}" -E echo "No runtime shader declared; nothing to compile"
        VERBATIM
    )
    add_custom_target(validate_compiled_shaders DEPENDS compile_shaders)
endif()
