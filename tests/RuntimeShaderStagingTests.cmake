foreach(required ENGINE_SOURCE_DIR ENGINE_TEST_BINARY_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(test_root "${ENGINE_TEST_BINARY_DIR}/runtime_shader_staging")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")
set(dummy_executable "${test_root}/Engine2DStandalone")
file(WRITE "${dummy_executable}" "runtime shader staging test")

set(formats msl dxil spirv)
set(extensions msl dxil spv)
set(entry_points main0 main main)
list(LENGTH formats case_count)
math(EXPR last_case "${case_count} - 1")

foreach(index RANGE ${last_case})
    list(GET formats ${index} format)
    list(GET extensions ${index} extension)
    list(GET entry_points ${index} entry_point)
    set(shader_variant "${test_root}/night_vision.frag.${extension}")
    set(stage_dir "${test_root}/stage-${format}")
    file(WRITE "${shader_variant}" "nonempty ${format} shader")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DENGINE_SOURCE_DIR=${ENGINE_SOURCE_DIR}"
            "-DENGINE_STAGE_DIR=${stage_dir}"
            "-DENGINE_EXECUTABLE=${dummy_executable}"
            "-DENGINE_SHADER_VARIANT=${shader_variant}"
            "-DENGINE_SHADER_FORMAT=${format}"
            "-DENGINE_SHADER_ENTRY_POINT=${entry_point}"
            -P "${ENGINE_SOURCE_DIR}/cmake/StageRuntimeAssets.cmake"
        RESULT_VARIABLE stage_result
        OUTPUT_VARIABLE stage_output
        ERROR_VARIABLE stage_error
    )
    if(NOT stage_result EQUAL 0)
        message(FATAL_ERROR "${format} staging failed: ${stage_output}${stage_error}")
    endif()

    set(descriptor_path "${stage_dir}/assets/shaders/NightVision.shader")
    file(READ "${descriptor_path}" descriptor_json)
    string(JSON staged_format GET "${descriptor_json}" variants 0 format)
    string(JSON staged_entry_point GET "${descriptor_json}" variants 0 entry_point)
    if(NOT staged_format STREQUAL format)
        message(FATAL_ERROR "${format} descriptor kept format '${staged_format}'")
    endif()
    if(NOT staged_entry_point STREQUAL entry_point)
        message(FATAL_ERROR "${format} descriptor kept entry point '${staged_entry_point}'")
    endif()
    if(NOT EXISTS "${stage_dir}/assets/shaders/night_vision.frag.${extension}")
        message(FATAL_ERROR "${format} shader was not staged")
    endif()

    file(READ "${stage_dir}/ProjectSettings/AssetDatabase.json" database_json)
    string(FIND
        "${database_json}"
        "\"path\": \"assets/shaders/night_vision.frag.${extension}\""
        staged_path_offset
    )
    if(staged_path_offset EQUAL -1)
        message(FATAL_ERROR "${format} asset database path was not updated")
    endif()
endforeach()

file(REMOVE_RECURSE "${test_root}")
message(STATUS "Runtime shader staging formats and entry points passed")
