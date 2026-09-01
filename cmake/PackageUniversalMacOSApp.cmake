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

file(GLOB_RECURSE arm_runtime_modules "${ENGINE_ARM_APP_DIR}/Contents/Resources/Library/*.dylib")
foreach(arm_runtime_module IN LISTS arm_runtime_modules)
    file(RELATIVE_PATH runtime_module "${ENGINE_ARM_APP_DIR}/Contents/Resources" "${arm_runtime_module}")
    set(x86_runtime_module "${ENGINE_X86_APP_DIR}/Contents/Resources/${runtime_module}")
    set(universal_runtime_module "${staged_app}/Contents/Resources/${runtime_module}")
    if(NOT EXISTS "${x86_runtime_module}")
        message(FATAL_ERROR "Intel macOS bundle is missing runtime module: ${runtime_module}")
    endif()
    execute_process(
        COMMAND "${LIPO_EXECUTABLE}" -create "${arm_runtime_module}" "${x86_runtime_module}" -output "${universal_runtime_module}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND "${LIPO_EXECUTABLE}" -info "${universal_runtime_module}"
        RESULT_VARIABLE runtime_module_lipo_result
        OUTPUT_VARIABLE runtime_module_lipo_output
        ERROR_VARIABLE runtime_module_lipo_error
    )
    if(NOT runtime_module_lipo_result EQUAL 0 OR NOT runtime_module_lipo_output MATCHES "arm64" OR NOT runtime_module_lipo_output MATCHES "x86_64")
        message(FATAL_ERROR "Universal runtime module is missing an architecture: ${runtime_module_lipo_output}${runtime_module_lipo_error}")
    endif()
endforeach()

file(RENAME "${staged_app}" "${ENGINE_UNIVERSAL_APP_DIR}")
file(REMOVE_RECURSE "${staging_root}")
set(universal_executable "${ENGINE_UNIVERSAL_APP_DIR}/Contents/MacOS/Engine2DStandalone")

file(GLOB bundled_dylibs "${ENGINE_UNIVERSAL_APP_DIR}/Contents/Frameworks/*.dylib")
foreach(bundled_dylib IN LISTS bundled_dylibs)
    file(CHMOD "${bundled_dylib}" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
    )
    execute_process(
        COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${bundled_dylib}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endforeach()
file(GLOB_RECURSE runtime_module_dylibs "${ENGINE_UNIVERSAL_APP_DIR}/Contents/Resources/Library/*.dylib")
foreach(runtime_module_dylib IN LISTS runtime_module_dylibs)
    file(CHMOD "${runtime_module_dylib}" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE
    )
    execute_process(
        COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${runtime_module_dylib}"
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
