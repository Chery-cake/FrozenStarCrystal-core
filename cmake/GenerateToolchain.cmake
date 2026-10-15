# ==============================================================================
# Script-mode entry point for toolchain generation
# ==============================================================================
# Usage:
#   cmake -DTC_PLATFORM=linux-aarch64 \
#         -DTC_OUTPUT=build/toolchains/linux-aarch64.cmake \
#         -P cmake/GenerateToolchain.cmake
# ==============================================================================

cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED TC_PLATFORM OR TC_PLATFORM STREQUAL "")
    message(FATAL_ERROR "TC_PLATFORM not set. Example: -DTC_PLATFORM=linux-aarch64")
endif()
if(NOT DEFINED TC_OUTPUT OR TC_OUTPUT STREQUAL "")
    message(FATAL_ERROR "TC_OUTPUT not set. Example: -DTC_OUTPUT=build/toolchains/linux-aarch64.cmake")
endif()

# CrossCompiling.cmake is safe to include in script mode: after the cleanup in
# step 1, all its top-level statements are just function definitions, so
# nothing that requires an enabled language evaluates here.
include("${CMAKE_CURRENT_LIST_DIR}/CrossCompiling.cmake")

# Make the output path absolute relative to the current working directory.
get_filename_component(TC_OUTPUT "${TC_OUTPUT}" ABSOLUTE)

# Ensure the parent directory exists.
get_filename_component(_out_dir "${TC_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")

generate_toolchain_file("${TC_PLATFORM}" "${TC_OUTPUT}")

if(TC_PLATFORM MATCHES "windows" AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    file(APPEND "${TC_OUTPUT}"
"# Run Windows test binaries via Wine during ctest
set(CMAKE_CROSSCOMPILING_EMULATOR wine64)
")
endif()

message(STATUS "Wrote toolchain: ${TC_OUTPUT}")
