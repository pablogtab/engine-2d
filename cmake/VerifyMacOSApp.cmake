if(NOT DEFINED ENGINE_APP_DIR)
    message(FATAL_ERROR "ENGINE_APP_DIR is required")
endif()

find_program(OTOOL_EXECUTABLE otool REQUIRED)
find_program(CODESIGN_EXECUTABLE codesign REQUIRED)

set(contents_dir "${ENGINE_APP_DIR}/Contents")
set(executable "${contents_dir}/MacOS/Engine2DStandalone")
set(resources_dir "${contents_dir}/Resources")
set(frameworks_dir "${contents_dir}/Frameworks")
file(READ "${resources_dir}/project.engine2d" project_json)
string(JSON startup_scene GET "${project_json}" startup_scene)
set(optional_shader_paths)
string(JSON runtime_shader_type ERROR_VARIABLE runtime_shader_error TYPE "${project_json}" runtime_shader)
if(NOT runtime_shader_error)
    string(JSON shader_descriptor GET "${project_json}" runtime_shader descriptor)
    string(JSON shader_variant_base GET "${project_json}" runtime_shader variant_base)
    list(APPEND optional_shader_paths "${resources_dir}/${shader_descriptor}" "${resources_dir}/${shader_variant_base}.msl")
endif()

foreach(required_path
    "${contents_dir}/Info.plist"
    "${executable}"
    "${resources_dir}/project.engine2d"
    "${resources_dir}/${startup_scene}"
    ${optional_shader_paths}
    "${resources_dir}/Engine/Shaders/lit_sprite.frag.msl"
    "${resources_dir}/ProjectSettings/AssetDatabase.json"
    "${frameworks_dir}/libSDL3.0.dylib"
    "${frameworks_dir}/libglm.dylib"
)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "macOS bundle is missing: ${required_path}")
    endif()
endforeach()

if(EXISTS "${resources_dir}/editor_assets")
    message(FATAL_ERROR "macOS bundle includes editor-only assets")
endif()

file(GLOB_RECURSE runtime_module_dylibs "${resources_dir}/Library/*.dylib")
if(NOT runtime_module_dylibs)
    message(FATAL_ERROR "macOS bundle is missing the generated EngineScript runtime module")
endif()

execute_process(
    COMMAND "${OTOOL_EXECUTABLE}" -L "${executable}"
    RESULT_VARIABLE executable_otool_result
    OUTPUT_VARIABLE executable_otool_output
    ERROR_VARIABLE executable_otool_error
)
if(NOT executable_otool_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect macOS executable: ${executable_otool_error}")
endif()
if(executable_otool_output MATCHES "/opt/homebrew/" OR executable_otool_output MATCHES "/usr/local/")
    message(FATAL_ERROR "macOS executable contains a package-manager dependency:\n${executable_otool_output}")
endif()
foreach(required_dependency "@rpath/libSDL3.0.dylib" "@rpath/libglm.dylib")
    string(FIND "${executable_otool_output}" "${required_dependency}" dependency_index)
    if(dependency_index EQUAL -1)
        message(FATAL_ERROR "macOS executable is missing expected bundled dependency: ${required_dependency}")
    endif()
endforeach()

execute_process(
    COMMAND "${OTOOL_EXECUTABLE}" -l "${executable}"
    RESULT_VARIABLE rpath_otool_result
    OUTPUT_VARIABLE rpath_otool_output
    ERROR_VARIABLE rpath_otool_error
)
if(NOT rpath_otool_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect macOS executable rpaths: ${rpath_otool_error}")
endif()
string(FIND "${rpath_otool_output}" "@executable_path/../Frameworks" rpath_index)
if(rpath_index EQUAL -1)
    message(FATAL_ERROR "macOS executable is missing its Frameworks rpath")
endif()
if(rpath_otool_output MATCHES "/opt/homebrew/" OR rpath_otool_output MATCHES "/usr/local/")
    message(FATAL_ERROR "macOS executable retains a package-manager rpath:\n${rpath_otool_output}")
endif()

foreach(framework "libSDL3.0.dylib" "libglm.dylib")
    execute_process(
        COMMAND "${OTOOL_EXECUTABLE}" -D "${frameworks_dir}/${framework}"
        RESULT_VARIABLE framework_otool_result
        OUTPUT_VARIABLE framework_otool_output
        ERROR_VARIABLE framework_otool_error
    )
    if(NOT framework_otool_result EQUAL 0)
        message(FATAL_ERROR "Could not inspect bundled framework ${framework}: ${framework_otool_error}")
    endif()
    string(FIND "${framework_otool_output}" "@rpath/${framework}" framework_id_index)
    if(framework_id_index EQUAL -1)
        message(FATAL_ERROR "Bundled framework has an invalid install name: ${framework}")
    endif()
endforeach()

execute_process(
    COMMAND "${CODESIGN_EXECUTABLE}" --verify --deep --strict "${ENGINE_APP_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Verified self-contained macOS app: ${ENGINE_APP_DIR}")
