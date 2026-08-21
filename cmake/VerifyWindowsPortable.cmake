foreach(required ENGINE_OUTPUT_DIR ENGINE_OBJDUMP_EXECUTABLE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
if(NOT EXISTS "${ENGINE_OBJDUMP_EXECUTABLE}")
    message(FATAL_ERROR "MinGW objdump is missing: ${ENGINE_OBJDUMP_EXECUTABLE}")
endif()

set(executable "${ENGINE_OUTPUT_DIR}/Engine2DStandalone.exe")
foreach(required_path
    "${executable}"
    "${ENGINE_OUTPUT_DIR}/SDL3.dll"
    "${ENGINE_OUTPUT_DIR}/libgcc_s_seh-1.dll"
    "${ENGINE_OUTPUT_DIR}/libstdc++-6.dll"
    "${ENGINE_OUTPUT_DIR}/libwinpthread-1.dll"
    "${ENGINE_OUTPUT_DIR}/assets/scenes/PlatformerDemo.scene"
    "${ENGINE_OUTPUT_DIR}/assets/shaders/night_vision.frag.dxil"
    "${ENGINE_OUTPUT_DIR}/assets/shaders/NightVision.shader"
    "${ENGINE_OUTPUT_DIR}/ProjectSettings/AssetDatabase.json"
)
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Portable Windows runtime is missing: ${required_path}")
    endif()
endforeach()
if(EXISTS "${ENGINE_OUTPUT_DIR}/editor_assets")
    message(FATAL_ERROR "Portable Windows runtime includes editor-only assets")
endif()

file(READ "${ENGINE_OUTPUT_DIR}/assets/shaders/night_vision.frag.dxil" dxil_header LIMIT 4 HEX)
if(NOT dxil_header STREQUAL "44584243")
    message(FATAL_ERROR "Packaged DXIL shader has an invalid header: ${dxil_header}")
endif()
file(READ "${ENGINE_OUTPUT_DIR}/assets/shaders/NightVision.shader" descriptor_json)
if(NOT descriptor_json MATCHES "\"format\"[ \t]*:[ \t]*\"dxil\"")
    message(FATAL_ERROR "Packaged shader descriptor does not select DXIL")
endif()
file(READ "${ENGINE_OUTPUT_DIR}/ProjectSettings/AssetDatabase.json" asset_database_json)
if(NOT asset_database_json MATCHES "night_vision\\.frag\\.dxil")
    message(FATAL_ERROR "Packaged AssetDatabase does not resolve the DXIL shader")
endif()

set(system_dlls
    kernel32.dll user32.dll gdi32.dll advapi32.dll shell32.dll ole32.dll oleaut32.dll
    imm32.dll winmm.dll version.dll setupapi.dll ws2_32.dll dinput8.dll dxguid.dll
    d3d12.dll d3d11.dll dxgi.dll avrt.dll bcrypt.dll crypt32.dll ntdll.dll ucrtbase.dll
)
set(scan_queue "${executable}")
set(scanned_files)
while(scan_queue)
    list(POP_FRONT scan_queue current_file)
    list(FIND scanned_files "${current_file}" scanned_index)
    if(NOT scanned_index EQUAL -1)
        continue()
    endif()
    list(APPEND scanned_files "${current_file}")

    execute_process(
        COMMAND "${ENGINE_OBJDUMP_EXECUTABLE}" -p "${current_file}"
        RESULT_VARIABLE objdump_result
        OUTPUT_VARIABLE objdump_output
        ERROR_VARIABLE objdump_error
    )
    if(NOT objdump_result EQUAL 0)
        message(FATAL_ERROR "Could not inspect PE dependencies for ${current_file}: ${objdump_error}")
    endif()
    string(REGEX MATCHALL "DLL Name: [A-Za-z0-9_+.-]+" dependency_matches "${objdump_output}")
    foreach(dependency_match IN LISTS dependency_matches)
        string(REGEX REPLACE "^DLL Name: " "" dependency_name "${dependency_match}")
        string(TOLOWER "${dependency_name}" dependency_lower)
        if(dependency_lower MATCHES "^(api-ms-win-|ext-ms-win-)" OR dependency_lower IN_LIST system_dlls)
            continue()
        endif()
        set(packaged_dependency "${ENGINE_OUTPUT_DIR}/${dependency_name}")
        if(NOT EXISTS "${packaged_dependency}")
            message(FATAL_ERROR "Portable Windows runtime is missing imported DLL: ${dependency_name}")
        endif()
        list(APPEND scan_queue "${packaged_dependency}")
    endforeach()
endwhile()

message(STATUS "Verified portable Windows runtime: ${ENGINE_OUTPUT_DIR}")
