find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${PROJECT_ROOT}"
        RESULT_VARIABLE result OUTPUT_VARIABLE version
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT GIT_FOUND OR NOT result EQUAL 0 OR version STREQUAL "")
    set(version "unknown")
endif()
string(REPLACE "\\" "\\\\" version "${version}")
string(REPLACE "\"" "\\\"" version "${version}")
set(content "#define PICOLOGGER_VERSION \"${version}\"\n")
if(EXISTS "${OUTPUT_FILE}")
    file(READ "${OUTPUT_FILE}" old_content)
endif()
if(NOT "${content}" STREQUAL "${old_content}")
    file(WRITE "${OUTPUT_FILE}" "${content}")
endif()
