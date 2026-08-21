foreach(required ENGINE_APP_DIR ENGINE_SMOKE_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

set(source_executable "${ENGINE_APP_DIR}/Contents/MacOS/Engine2DStandalone")
if(NOT EXISTS "${source_executable}")
    message(FATAL_ERROR "Packaged macOS executable is missing: ${source_executable}")
endif()

set(smoke_app "${ENGINE_SMOKE_DIR}/Engine2D.app")
# ENGINE_SMOKE_DIR is an exact build output owned by this target.
file(REMOVE_RECURSE "${ENGINE_SMOKE_DIR}")
file(MAKE_DIRECTORY "${ENGINE_SMOKE_DIR}")
file(COPY "${ENGINE_APP_DIR}" DESTINATION "${ENGINE_SMOKE_DIR}")

execute_process(
    COMMAND "${smoke_app}/Contents/MacOS/Engine2DStandalone" --smoke-test
    WORKING_DIRECTORY "${ENGINE_SMOKE_DIR}"
    RESULT_VARIABLE smoke_result
    OUTPUT_VARIABLE smoke_output
    ERROR_VARIABLE smoke_error
)
if(NOT smoke_result EQUAL 0)
    message(FATAL_ERROR "macOS export smoke test failed:\n${smoke_output}${smoke_error}")
endif()
if(NOT smoke_output MATCHES "\\[RUNTIME SMOKE\\] Assets=[0-9]+ Scene=PlatformerDemo Shader=NightVision")
    message(FATAL_ERROR "macOS export smoke test did not confirm packaged resources:\n${smoke_output}")
endif()

message(STATUS "Passed isolated macOS export smoke test: ${smoke_app}")
