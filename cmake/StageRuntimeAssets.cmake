foreach(required ENGINE_SOURCE_DIR ENGINE_PROJECT_DIR ENGINE_STAGE_DIR ENGINE_EXECUTABLE ENGINE_SHADER_VARIANT ENGINE_SHADER_FORMAT ENGINE_SHADER_ENTRY_POINT ENGINE_SHADER_DESCRIPTOR ENGINE_SHADER_VARIANT_BASE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT EXISTS "${ENGINE_EXECUTABLE}")
    message(FATAL_ERROR "Engine executable is missing: ${ENGINE_EXECUTABLE}")
endif()
if(NOT EXISTS "${ENGINE_SHADER_VARIANT}")
    message(FATAL_ERROR "Compiled shader variant is missing: ${ENGINE_SHADER_VARIANT}")
endif()
if(NOT EXISTS "${ENGINE_PROJECT_DIR}/project.engine2d")
    message(FATAL_ERROR "Project descriptor is missing: ${ENGINE_PROJECT_DIR}/project.engine2d")
endif()

file(REMOVE_RECURSE "${ENGINE_STAGE_DIR}")
file(MAKE_DIRECTORY "${ENGINE_STAGE_DIR}")
file(COPY "${ENGINE_PROJECT_DIR}/Assets" DESTINATION "${ENGINE_STAGE_DIR}")
file(COPY "${ENGINE_PROJECT_DIR}/ProjectSettings" DESTINATION "${ENGINE_STAGE_DIR}")
file(COPY_FILE "${ENGINE_PROJECT_DIR}/project.engine2d" "${ENGINE_STAGE_DIR}/project.engine2d" ONLY_IF_DIFFERENT)
get_filename_component(executable_name "${ENGINE_EXECUTABLE}" NAME)
file(COPY_FILE "${ENGINE_EXECUTABLE}" "${ENGINE_STAGE_DIR}/${executable_name}" ONLY_IF_DIFFERENT)

get_filename_component(shader_filename "${ENGINE_SHADER_VARIANT}" NAME)
file(REMOVE
    "${ENGINE_STAGE_DIR}/${ENGINE_SHADER_VARIANT_BASE}.msl"
    "${ENGINE_STAGE_DIR}/${ENGINE_SHADER_VARIANT_BASE}.dxil"
    "${ENGINE_STAGE_DIR}/${ENGINE_SHADER_VARIANT_BASE}.spv"
)
get_filename_component(shader_directory "${ENGINE_SHADER_VARIANT_BASE}" DIRECTORY)
set(staged_shader "${ENGINE_STAGE_DIR}/${shader_directory}/${shader_filename}")
file(COPY_FILE "${ENGINE_SHADER_VARIANT}" "${staged_shader}" ONLY_IF_DIFFERENT)

set(descriptor_path "${ENGINE_STAGE_DIR}/${ENGINE_SHADER_DESCRIPTOR}")
file(READ "${descriptor_path}" descriptor_json)
string(JSON descriptor_json SET "${descriptor_json}" variants 0 format "\"${ENGINE_SHADER_FORMAT}\"")
string(JSON descriptor_json SET "${descriptor_json}" variants 0 entry_point "\"${ENGINE_SHADER_ENTRY_POINT}\"")
file(WRITE "${descriptor_path}" "${descriptor_json}\n")

set(database_path "${ENGINE_STAGE_DIR}/ProjectSettings/AssetDatabase.json")
file(READ "${database_path}" asset_database_json)
string(REPLACE
    "\"path\": \"${ENGINE_SHADER_VARIANT_BASE}.msl\""
    "\"path\": \"${shader_directory}/${shader_filename}\""
    asset_database_json
    "${asset_database_json}"
)
file(WRITE "${database_path}" "${asset_database_json}\n")

message(STATUS "Staged runtime assets for ${ENGINE_SHADER_FORMAT}: ${ENGINE_STAGE_DIR}")
