# cmake -DSOURCE_DIR=<repo> -DOUT=<file> -P cmake/web_buildinfo.cmake
# Writes the build.json the page reads (docs/web-engine-interface.md section 1): { "version": "<git describe>", "built": "<ISO time>" }
execute_process(COMMAND git describe --always --dirty --tags
    WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE _version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
if (NOT _rc EQUAL 0 OR _version STREQUAL "")
    set(_version "unknown")
endif()
string(TIMESTAMP _built "%Y-%m-%dT%H:%M:%SZ" UTC)
file(WRITE "${OUT}" "{ \"version\": \"${_version}\", \"built\": \"${_built}\" }\n")
