foreach(required ENGINE_ARM_APP_DIR ENGINE_X86_APP_DIR ENGINE_UNIVERSAL_APP_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

find_program(LIPO_EXECUTABLE lipo REQUIRED)
find_program(CODESIGN_EXECUTABLE codesign REQUIRED)

set(arm_executable "${ENGINE_ARM_APP_DIR}/Contents/MacOS/Engine2DStandalone")
set(x86_executable "${ENGINE_X86_APP_DIR}/Contents/MacOS/Engine2DStandalone")
if(NOT EXISTS "${arm_executable}" OR NOT EXISTS "${x86_executable}")
    message(FATAL_ERROR "Both arm64 and x86_64 macOS bundles must exist before merging")
endif()

get_filename_component(universal_parent "${ENGINE_UNIVERSAL_APP_DIR}" DIRECTORY)
set(staging_root "${universal_parent}/macos-universal-staging")
# ENGINE_UNIVERSAL_APP_DIR and staging_root are exact build outputs owned by this target.
file(REMOVE_RECURSE "${ENGINE_UNIVERSAL_APP_DIR}" "${staging_root}")
file(MAKE_DIRECTORY "${staging_root}")
file(COPY "${ENGINE_ARM_APP_DIR}" DESTINATION "${staging_root}")
get_filename_component(arm_app_name "${ENGINE_ARM_APP_DIR}" NAME)
set(staged_app "${staging_root}/${arm_app_name}")

set(universal_executable "${staged_app}/Contents/MacOS/Engine2DStandalone")
execute_process(
    COMMAND "${LIPO_EXECUTABLE}" -create "${arm_executable}" "${x86_executable}" -output "${universal_executable}"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${LIPO_EXECUTABLE}" -info "${universal_executable}"
    RESULT_VARIABLE lipo_result
    OUTPUT_VARIABLE lipo_output
    ERROR_VARIABLE lipo_error
)
if(NOT lipo_result EQUAL 0 OR NOT lipo_output MATCHES "arm64" OR NOT lipo_output MATCHES "x86_64")
    message(FATAL_ERROR "Universal executable is missing an architecture: ${lipo_output}${lipo_error}")
endif()

file(RENAME "${staged_app}" "${ENGINE_UNIVERSAL_APP_DIR}")
file(REMOVE_RECURSE "${staging_root}")
set(universal_executable "${ENGINE_UNIVERSAL_APP_DIR}/Contents/MacOS/Engine2DStandalone")

file(GLOB bundled_dylibs "${ENGINE_UNIVERSAL_APP_DIR}/Contents/Frameworks/*.dylib")
foreach(bundled_dylib IN LISTS bundled_dylibs)
    execute_process(
        COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${bundled_dylib}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endforeach()
execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${universal_executable}"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${ENGINE_UNIVERSAL_APP_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --verify --deep --strict "${ENGINE_UNIVERSAL_APP_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Created ad-hoc signed universal macOS bundle: ${ENGINE_UNIVERSAL_APP_DIR}")
