include_guard(GLOBAL)

# Capture our cmake/ directory at definition time so macros can find
# sibling files regardless of where they are called from.
if(NOT DEFINED FSC_CMAKE_DIR)
    set(FSC_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()

# ==============================================================================
# PRE-PROJECT SETUP — runs at include() time, before the caller's project()
# ==============================================================================
# These MUST be set before CXX is enabled, otherwise `import std` / C++23
# modules fail toolchain detection (CXX_MODULE_STD requires this UUID).

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "451f2fe2-a8a2-47c3-bc32-94786d8fc91b"
    CACHE STRING "Enable import std experimental feature" FORCE
)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
endif()

if(DEFINED ENV{CXX_MODULES_JSON})
    set(CMAKE_CXX_STDLIB_MODULES_JSON "$ENV{CXX_MODULES_JSON}" CACHE FILEPATH
        "Path to libc++.modules.json or libstdc++.modules.json")
    message(STATUS "Stdlib modules json: $ENV{CXX_MODULES_JSON}")
endif()

# --- Include building blocks ---------------------------------------------------
include(${FSC_CMAKE_DIR}/CompilerWarnings.cmake)
include(${FSC_CMAKE_DIR}/BuildConfiguration.cmake)
include(${FSC_CMAKE_DIR}/CrossCompiling.cmake)
include(${FSC_CMAKE_DIR}/sanitizers.cmake)
include(${FSC_CMAKE_DIR}/GenerateClangd.cmake)
include(${FSC_CMAKE_DIR}/AddEngineModule.cmake)

# --- CPM.cmake bootstrap -------------------------------------------------------
# Pin the version so bumps are explicit. Bumping the string triggers a
# re-download on the next configure; nothing to delete by hand.
set(FSC_CPM_VERSION "v0.43.2" CACHE STRING "CPM.cmake version to fetch")
set(FETCHCONTENT_QUIET FALSE CACHE BOOL
    "Show FetchContent/CPM download progress" FORCE)

macro(fsc_ensure_cpm)
    # Already loaded in this project tree (a parent already ran this macro).
    # CPM.cmake sets CPM_VERSION as an internal cache var when included.
    if(DEFINED CPM_VERSION)
        return()
    endif()

    # Always live in the TOP-LEVEL build dir, so every subproject shares one copy.
    # Wiping build/ re-downloads; no other cleanup required.
    set(_cpm_dir  "${CMAKE_BINARY_DIR}")
    set(_cpm_file "${_cpm_dir}/CPM.cmake")
    set(_cpm_ver  "${_cpm_dir}/.CPM.version")

    file(MAKE_DIRECTORY "${_cpm_dir}")

    set(_have_version "")
    if(EXISTS "${_cpm_ver}")
        file(READ "${_cpm_ver}" _have_version)
        string(STRIP "${_have_version}" _have_version)
    endif()

    if(NOT EXISTS "${_cpm_file}" OR NOT _have_version STREQUAL FSC_CPM_VERSION)
        set(_cpm_url
            "https://github.com/cpm-cmake/CPM.cmake/releases/download/${FSC_CPM_VERSION}/CPM.cmake")
        message(STATUS "Fetching CPM.cmake ${FSC_CPM_VERSION} -> ${_cpm_file}")
        file(DOWNLOAD "${_cpm_url}" "${_cpm_file}"
            STATUS _cpm_status TLS_VERIFY ON)
        list(GET _cpm_status 0 _cpm_code)
        if(NOT _cpm_code EQUAL 0)
            message(FATAL_ERROR "Failed to download CPM.cmake: ${_cpm_status}")
        endif()
        file(WRITE "${_cpm_ver}" "${FSC_CPM_VERSION}")
    endif()

    include("${_cpm_file}")
endmacro()

# ==============================================================================
# POST-PROJECT SETUP — call AFTER the literal project() in top-level file
# ==============================================================================
macro(fsc_initialize_project)
    # Out-of-source build enforcement
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
        message(FATAL_ERROR
            "In-source builds are not allowed.\n  cmake -B build -S .")
    endif()

    enable_testing()

    # C++23 / C23, modules on (import std is by design)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
    set(CMAKE_CXX_MODULE_STD ON)

    if(MSVC)
        set(CMAKE_C_STANDARD 17)
    else()
        set(CMAKE_C_STANDARD 23)
    endif()
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    if(APPLE)
        enable_language(OBJC)
        enable_language(OBJCXX)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_compile_definitions(_GNU_SOURCE)
    endif()

    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    endif()
    option(BUILD_SHARED_LIBS "Build shared libraries" ON)

    # Output layout
    set(TARGET_OS  "${CMAKE_SYSTEM_NAME}")
    set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}/${TARGET_OS}")

    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}/lib")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

    # Diagnostics colour / MSVC extras
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fdiagnostics-color=always)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        add_compile_options(/diagnostics:color /FS)
        set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb")
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
        set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
    endif()

    fsc_ensure_cpm()

    message(STATUS "FSC module initialized: ${PROJECT_NAME} v${PROJECT_VERSION}")
endmacro()

# ==============================================================================
# fsc_finalize_top_level()
# ==============================================================================
function(fsc_finalize_top_level)
    if(NOT PROJECT_IS_TOP_LEVEL)
        return()
    endif()

    generateClangd()

    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " ${PROJECT_NAME} v${PROJECT_VERSION}")
    message(STATUS "========================================")
    message(STATUS " Build Type:         ${CMAKE_BUILD_TYPE}")
    message(STATUS " Shared Libraries:   ${BUILD_SHARED_LIBS}")
    message(STATUS " C++ Standard:       ${CMAKE_CXX_STANDARD}")
    message(STATUS " C++ Compiler:       ${CMAKE_CXX_COMPILER_ID}")
    message(STATUS " C Standard:         ${CMAKE_C_STANDARD}")
    message(STATUS " C Compiler:         ${CMAKE_C_COMPILER_ID}")
    message(STATUS " Output:             ${OUTPUT_DIR}")
    message(STATUS "========================================")

    print_build_configuration()
    print_cross_compilation_info()
endfunction()
