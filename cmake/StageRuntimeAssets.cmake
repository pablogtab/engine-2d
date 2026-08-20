foreach(required ENGINE_SOURCE_DIR ENGINE_STAGE_DIR ENGINE_EXECUTABLE ENGINE_SHADER_VARIANT ENGINE_SHADER_FORMAT)
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

file(REMOVE_RECURSE "${ENGINE_STAGE_DIR}")
file(MAKE_DIRECTORY "${ENGINE_STAGE_DIR}")
file(COPY "${ENGINE_SOURCE_DIR}/assets" DESTINATION "${ENGINE_STAGE_DIR}")
file(COPY "${ENGINE_SOURCE_DIR}/ProjectSettings" DESTINATION "${ENGINE_STAGE_DIR}")
get_filename_component(executable_name "${ENGINE_EXECUTABLE}" NAME)
file(COPY_FILE "${ENGINE_EXECUTABLE}" "${ENGINE_STAGE_DIR}/${executable_name}" ONLY_IF_DIFFERENT)

set(staged_shader "${ENGINE_STAGE_DIR}/assets/shaders/night_vision.frag.msl")
file(COPY_FILE "${ENGINE_SHADER_VARIANT}" "${staged_shader}" ONLY_IF_DIFFERENT)

set(descriptor_path "${ENGINE_STAGE_DIR}/assets/shaders/NightVision.shader")
file(READ "${descriptor_path}" descriptor_json)
string(JSON descriptor_json SET "${descriptor_json}" variants 0 format "\"${ENGINE_SHADER_FORMAT}\"")
file(WRITE "${descriptor_path}" "${descriptor_json}\n")

message(STATUS "Staged runtime assets for ${ENGINE_SHADER_FORMAT}: ${ENGINE_STAGE_DIR}")
