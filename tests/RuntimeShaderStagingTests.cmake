foreach(required ENGINE_SOURCE_DIR ENGINE_TEST_BINARY_DIR ENGINE_SCRIPT_COMPILER ENGINE_LIGHTING_BAKE_TOOL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
set(test_root "${ENGINE_TEST_BINARY_DIR}/runtime_shader_staging")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")
set(fixture_project "${test_root}/fixture_project")
file(COPY "${ENGINE_SOURCE_DIR}/tests/fixtures/runtime_project/" DESTINATION "${fixture_project}")
file(MAKE_DIRECTORY "${fixture_project}/Library/LightingBakes")
file(WRITE "${fixture_project}/Library/LightingBakes/staging-marker.txt" "packaged bake")

set(stale_project "${test_root}/stale_project")
file(COPY "${ENGINE_SOURCE_DIR}/tests/fixtures/runtime_project/" DESTINATION "${stale_project}")
file(MAKE_DIRECTORY "${stale_project}/Assets/Scenes")
file(WRITE "${stale_project}/Assets/Scenes/Stale.scene"
    "{\"scene_name\":\"Stale\",\"entities\":[{\"id\":1,\"transform\":{\"position\":[0,0,0]},\"lighting_bake_volume_2d\":{\"size\":[8,8],\"enabled\":true}}]}"
)
execute_process(
    COMMAND "${ENGINE_LIGHTING_BAKE_TOOL}" --validate-project "${stale_project}"
    RESULT_VARIABLE stale_result
)
if(stale_result EQUAL 0)
    message(FATAL_ERROR "Lighting bake validation accepted a stale project")
endif()

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
    set(builtin_shader_variant "${test_root}/lit_sprite.frag.${extension}")
    set(stage_dir "${test_root}/stage-${format}")
    file(WRITE "${shader_variant}" "nonempty ${format} shader")
    file(WRITE "${builtin_shader_variant}" "nonempty built-in ${format} shader")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DENGINE_SOURCE_DIR=${ENGINE_SOURCE_DIR}"
            "-DENGINE_PROJECT_DIR=${fixture_project}"
            "-DENGINE_STAGE_DIR=${stage_dir}"
            "-DENGINE_EXECUTABLE=${dummy_executable}"
            "-DENGINE_SCRIPT_COMPILER=${ENGINE_SCRIPT_COMPILER}"
            "-DENGINE_LIGHTING_BAKE_TOOL=${ENGINE_LIGHTING_BAKE_TOOL}"
            "-DENGINE_SHADER_VARIANT=${shader_variant}"
            "-DENGINE_SHADER_FORMAT=${format}"
            "-DENGINE_SHADER_ENTRY_POINT=${entry_point}"
            "-DENGINE_SHADER_DESCRIPTOR=Assets/Shaders/NightVision.shader"
            "-DENGINE_SHADER_VARIANT_BASE=Assets/Shaders/night_vision.frag"
            "-DENGINE_BUILTIN_LIT_SHADER_VARIANT=${builtin_shader_variant}"
            -P "${ENGINE_SOURCE_DIR}/cmake/StageRuntimeAssets.cmake"
        RESULT_VARIABLE stage_result
        OUTPUT_VARIABLE stage_output
        ERROR_VARIABLE stage_error
    )
    if(NOT stage_result EQUAL 0)
        message(FATAL_ERROR "${format} staging failed: ${stage_output}${stage_error}")
    endif()

    set(descriptor_path "${stage_dir}/Assets/Shaders/NightVision.shader")
    file(READ "${descriptor_path}" descriptor_json)
    string(JSON staged_format GET "${descriptor_json}" variants 0 format)
    string(JSON staged_entry_point GET "${descriptor_json}" variants 0 entry_point)
    if(NOT staged_format STREQUAL format)
        message(FATAL_ERROR "${format} descriptor kept format '${staged_format}'")
    endif()
    if(NOT staged_entry_point STREQUAL entry_point)
        message(FATAL_ERROR "${format} descriptor kept entry point '${staged_entry_point}'")
    endif()
    if(NOT EXISTS "${stage_dir}/Assets/Shaders/night_vision.frag.${extension}")
        message(FATAL_ERROR "${format} shader was not staged")
    endif()
    if(NOT EXISTS "${stage_dir}/Engine/Shaders/lit_sprite.frag.${extension}")
        message(FATAL_ERROR "${format} built-in lit sprite shader was not staged")
    endif()
    if(NOT EXISTS "${stage_dir}/Library/LightingBakes/staging-marker.txt")
        message(FATAL_ERROR "${format} lighting bakes were not staged")
    endif()

    file(READ "${stage_dir}/ProjectSettings/AssetDatabase.json" database_json)
    string(FIND
        "${database_json}"
        "\"path\": \"Assets/Shaders/night_vision.frag.${extension}\""
        staged_path_offset
    )
    if(staged_path_offset EQUAL -1)
        message(FATAL_ERROR "${format} asset database path was not updated")
    endif()
endforeach()

file(REMOVE_RECURSE "${test_root}")
message(STATUS "Runtime shader staging formats and entry points passed")
