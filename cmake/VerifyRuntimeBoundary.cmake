if(NOT DEFINED ENGINE_EXECUTABLE OR NOT EXISTS "${ENGINE_EXECUTABLE}")
    message(FATAL_ERROR "ENGINE_EXECUTABLE is required")
endif()

find_program(ENGINE_NM_EXECUTABLE nm REQUIRED)
execute_process(
    COMMAND "${ENGINE_NM_EXECUTABLE}" -g "${ENGINE_EXECUTABLE}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE nm_error
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Unable to inspect runtime symbols: ${nm_error}")
endif()

if(symbols MATCHES "EditorLayer|ImGui|imgui")
    message(FATAL_ERROR "Standalone runtime links editor-only symbols")
endif()

message(STATUS "Standalone runtime has no editor symbols")
