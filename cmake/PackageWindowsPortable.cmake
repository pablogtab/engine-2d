foreach(required ENGINE_STAGE_DIR ENGINE_OUTPUT_DIR ENGINE_EXECUTABLE_NAME ENGINE_OBJDUMP_EXECUTABLE ENGINE_DLL_SEARCH_PATHS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT EXISTS "${ENGINE_OBJDUMP_EXECUTABLE}")
    message(FATAL_ERROR "MinGW objdump is missing: ${ENGINE_OBJDUMP_EXECUTABLE}")
endif()
string(REPLACE "|" ";" dll_search_paths "${ENGINE_DLL_SEARCH_PATHS}")

set(staged_executable "${ENGINE_STAGE_DIR}/${ENGINE_EXECUTABLE_NAME}")
if(NOT EXISTS "${staged_executable}")
    message(FATAL_ERROR "Staged standalone executable is missing: ${staged_executable}")
endif()
foreach(required_directory assets ProjectSettings)
    if(NOT EXISTS "${ENGINE_STAGE_DIR}/${required_directory}")
        message(FATAL_ERROR "Staged runtime directory is missing: ${ENGINE_STAGE_DIR}/${required_directory}")
    endif()
endforeach()

# ENGINE_OUTPUT_DIR is an exact build output owned by this target.
file(REMOVE_RECURSE "${ENGINE_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${ENGINE_OUTPUT_DIR}")
file(COPY "${ENGINE_STAGE_DIR}/assets" DESTINATION "${ENGINE_OUTPUT_DIR}")
file(COPY "${ENGINE_STAGE_DIR}/ProjectSettings" DESTINATION "${ENGINE_OUTPUT_DIR}")
file(COPY_FILE "${staged_executable}" "${ENGINE_OUTPUT_DIR}/${ENGINE_EXECUTABLE_NAME}")

set(system_dlls
    kernel32.dll user32.dll gdi32.dll advapi32.dll shell32.dll ole32.dll oleaut32.dll
    imm32.dll winmm.dll version.dll setupapi.dll ws2_32.dll dinput8.dll dxguid.dll
    d3d12.dll d3d11.dll dxgi.dll avrt.dll bcrypt.dll crypt32.dll ntdll.dll ucrtbase.dll
)
set(scan_queue "${ENGINE_OUTPUT_DIR}/${ENGINE_EXECUTABLE_NAME}")
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

        set(dependency_path "")
        foreach(search_path IN LISTS dll_search_paths)
            if(EXISTS "${search_path}/${dependency_name}")
                set(dependency_path "${search_path}/${dependency_name}")
                break()
            endif()
        endforeach()
        if(dependency_path STREQUAL "")
            message(FATAL_ERROR "Could not resolve runtime DLL dependency: ${dependency_name}")
        endif()

        set(packaged_dependency "${ENGINE_OUTPUT_DIR}/${dependency_name}")
        file(COPY_FILE "${dependency_path}" "${packaged_dependency}" ONLY_IF_DIFFERENT)
        list(APPEND scan_queue "${packaged_dependency}")
    endforeach()
endwhile()

message(STATUS "Created portable Windows runtime folder: ${ENGINE_OUTPUT_DIR}")
