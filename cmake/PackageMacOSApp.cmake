foreach(required ENGINE_STAGE_DIR ENGINE_APP_DIR ENGINE_EXECUTABLE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT EXISTS "${ENGINE_STAGE_DIR}/Assets")
    message(FATAL_ERROR "Runtime assets are missing: ${ENGINE_STAGE_DIR}/Assets")
endif()
if(NOT EXISTS "${ENGINE_STAGE_DIR}/ProjectSettings")
    message(FATAL_ERROR "Runtime project settings are missing: ${ENGINE_STAGE_DIR}/ProjectSettings")
endif()
if(NOT EXISTS "${ENGINE_EXECUTABLE}")
    message(FATAL_ERROR "Standalone executable is missing: ${ENGINE_EXECUTABLE}")
endif()

find_program(OTOOL_EXECUTABLE otool REQUIRED)
find_program(INSTALL_NAME_TOOL_EXECUTABLE install_name_tool REQUIRED)
find_program(CODESIGN_EXECUTABLE codesign REQUIRED)

set(contents_dir "${ENGINE_APP_DIR}/Contents")
set(macos_dir "${contents_dir}/MacOS")
set(resources_dir "${contents_dir}/Resources")
set(frameworks_dir "${contents_dir}/Frameworks")
get_filename_component(executable_name "${ENGINE_EXECUTABLE}" NAME)
set(bundle_executable "${macos_dir}/${executable_name}")

# ENGINE_APP_DIR is an exact build output owned by this target.
file(REMOVE_RECURSE "${ENGINE_APP_DIR}")
file(MAKE_DIRECTORY "${macos_dir}" "${resources_dir}" "${frameworks_dir}")
file(COPY "${ENGINE_STAGE_DIR}/Assets" DESTINATION "${resources_dir}")
file(COPY "${ENGINE_STAGE_DIR}/ProjectSettings" DESTINATION "${resources_dir}")
file(COPY_FILE "${ENGINE_STAGE_DIR}/project.engine2d" "${resources_dir}/project.engine2d")
file(COPY_FILE "${ENGINE_EXECUTABLE}" "${bundle_executable}")

file(WRITE "${contents_dir}/Info.plist" [=[<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>Engine2DStandalone</string>
    <key>CFBundleIdentifier</key><string>com.engine2d.standalone</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>Engine2D</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
</dict>
</plist>
]=])

execute_process(
    COMMAND "${OTOOL_EXECUTABLE}" -L "${ENGINE_EXECUTABLE}"
    RESULT_VARIABLE otool_result
    OUTPUT_VARIABLE otool_output
    ERROR_VARIABLE otool_error
)
if(NOT otool_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect standalone dependencies: ${otool_error}")
endif()

string(REGEX MATCHALL "/[^ \t]+\\.dylib" dynamic_dependencies "${otool_output}")
list(REMOVE_DUPLICATES dynamic_dependencies)
foreach(dependency IN LISTS dynamic_dependencies)
    if(dependency MATCHES "^/usr/lib/" OR dependency MATCHES "^/System/")
        continue()
    endif()
    if(NOT EXISTS "${dependency}")
        message(FATAL_ERROR "Non-system dependency is not available for bundling: ${dependency}")
    endif()

    get_filename_component(dependency_name "${dependency}" NAME)
    set(bundled_dependency "${frameworks_dir}/${dependency_name}")
    file(COPY_FILE "${dependency}" "${bundled_dependency}")
    execute_process(
        COMMAND "${INSTALL_NAME_TOOL_EXECUTABLE}" -id "@rpath/${dependency_name}" "${bundled_dependency}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND "${INSTALL_NAME_TOOL_EXECUTABLE}" -change "${dependency}" "@rpath/${dependency_name}" "${bundle_executable}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endforeach()

execute_process(
    COMMAND "${INSTALL_NAME_TOOL_EXECUTABLE}" -add_rpath "@executable_path/../Frameworks" "${bundle_executable}"
    COMMAND_ERROR_IS_FATAL ANY
)

# CMake may have added a package-manager rpath while linking the development
# executable. It must not survive in the distributable bundle.
execute_process(
    COMMAND "${OTOOL_EXECUTABLE}" -l "${bundle_executable}"
    RESULT_VARIABLE rpath_otool_result
    OUTPUT_VARIABLE rpath_otool_output
    ERROR_VARIABLE rpath_otool_error
)
if(NOT rpath_otool_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect bundled rpaths: ${rpath_otool_error}")
endif()
string(REGEX MATCHALL "path[ \t]+/(opt/homebrew|usr/local)/[^ \t]+" package_manager_rpath_matches "${rpath_otool_output}")
foreach(rpath_match IN LISTS package_manager_rpath_matches)
    string(REGEX REPLACE "^path[ \t]+" "" package_manager_rpath "${rpath_match}")
    execute_process(
        COMMAND "${INSTALL_NAME_TOOL_EXECUTABLE}" -delete_rpath "${package_manager_rpath}" "${bundle_executable}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endforeach()

execute_process(
    COMMAND "${OTOOL_EXECUTABLE}" -L "${bundle_executable}"
    RESULT_VARIABLE bundled_otool_result
    OUTPUT_VARIABLE bundled_otool_output
    ERROR_VARIABLE bundled_otool_error
)
if(NOT bundled_otool_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect bundled executable: ${bundled_otool_error}")
endif()
if(bundled_otool_output MATCHES "/opt/homebrew/" OR bundled_otool_output MATCHES "/usr/local/")
    message(FATAL_ERROR "Bundled executable still contains a package-manager dependency:\n${bundled_otool_output}")
endif()

file(GLOB bundled_dylibs "${frameworks_dir}/*.dylib")
foreach(bundled_dylib IN LISTS bundled_dylibs)
    execute_process(
        COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${bundled_dylib}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endforeach()
execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${bundle_executable}"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - --timestamp=none "${ENGINE_APP_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --verify --deep --strict "${ENGINE_APP_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Created ad-hoc signed macOS bundle: ${ENGINE_APP_DIR}")
