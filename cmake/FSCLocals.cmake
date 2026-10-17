# ==============================================================================
# FSCLocals.cmake - Local package registration and package resolution
# ==============================================================================
# Everything in this file runs AFTER fsc_initialize_project() — it depends on
# CPMAddPackage being available. Safe to include from FSCProject.cmake.
# ==============================================================================
include_guard(GLOBAL)


# ------------------------------------------------------------------------------
# Drop stale FETCHCONTENT_SOURCE_DIR_* entries pointing at deleted sibling caches.
# Moved here from FSCBootstrap: this only matters once some sibling's _deps/ was
# clean'd between configures, and it runs well before the first fsc_cpm_or_local
# call, so this placement is equivalent and keeps bootstrap focused on tooling
# discovery.
# ------------------------------------------------------------------------------
get_cmake_property(_fsc_cache_vars CACHE_VARIABLES)
foreach(_fsc_var IN LISTS _fsc_cache_vars)
    if(_fsc_var MATCHES "^FETCHCONTENT_SOURCE_DIR_")
        set(_fsc_val "${${_fsc_var}}")
        if(_fsc_val MATCHES "/build/_deps/" AND NOT EXISTS "${_fsc_val}/CMakeLists.txt")
            message(STATUS "FSC dep cache: clearing stale ${_fsc_var} -> ${_fsc_val}")
            unset(${_fsc_var} CACHE)
        endif()
    endif()
endforeach()


# ------------------------------------------------------------------------------
# fsc_register_local(<name> <path>)
#
# Registers an explicit path for a local checkout of an FSC package. Use
# this only when the checkout lives somewhere other than the default sibling
# location (${FSC_PROJECTS_ROOT}/<name>) — for example a worktree under /opt,
# a teammate's branch, or a path selected by an external tool.
#
# Paths may be relative; they are resolved against CMAKE_CURRENT_LIST_DIR
# (the directory of the file that calls this, typically local-paths.cmake).
# ------------------------------------------------------------------------------
function(fsc_register_local NAME PATH)
    if(NOT IS_ABSOLUTE "${PATH}")
        get_filename_component(PATH "${CMAKE_CURRENT_LIST_DIR}/${PATH}" ABSOLUTE)
    endif()

    if(NOT EXISTS "${PATH}/CMakeLists.txt")
        message(WARNING
            "fsc_register_local(${NAME}): ${PATH} does not contain "
            "CMakeLists.txt — ignoring.")
        return()
    endif()

    set(FSC_LOCAL_${NAME} "${PATH}" CACHE INTERNAL
        "Local checkout of ${NAME}" FORCE)
    message(STATUS "FSC local path: ${NAME} -> ${PATH}")
endfunction()


# ------------------------------------------------------------------------------
# fsc_load_local_paths([<file>])
#
# Loads an optional file that registers non-standard local checkout paths.
# The file is expected to call fsc_register_local(<name> <path>) for packages
# that live outside ${FSC_PROJECTS_ROOT}.
#
# Default file resolution order (first hit wins):
#   1. Explicit argument (if provided and exists)
#   2. $ENV{FSC_LOCAL_PATHS}
#   3. ${CMAKE_CURRENT_SOURCE_DIR}/cmake/local-paths.cmake
#
# Silently no-ops if no file is found — the local-paths file is opt-in.
# ------------------------------------------------------------------------------
function(fsc_load_local_paths)
    set(_file "")

    if(ARGC GREATER 0 AND EXISTS "${ARGV0}")
        set(_file "${ARGV0}")
    elseif(DEFINED ENV{FSC_LOCAL_PATHS} AND EXISTS "$ENV{FSC_LOCAL_PATHS}")
        set(_file "$ENV{FSC_LOCAL_PATHS}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/local-paths.cmake")
        set(_file "${CMAKE_CURRENT_SOURCE_DIR}/cmake/local-paths.cmake")
    endif()

    if(_file)
        message(STATUS "FSC local paths: loading ${_file}")
        include("${_file}")
    endif()
endfunction()


# ------------------------------------------------------------------------------
# fsc_cpm_or_local(<name>
#                  GITHUB_REPOSITORY <repo>
#                  GIT_TAG <tag>
#                  [OPTIONS ...]
#                  [DOWNLOAD_AND_ADD_SUBDIRECTORY]
#                  [TEST_TARGET <target>])
#
# Adds an FSC package, preferring a local checkout over a fresh CPM download.
# Local-checkout resolution is delegated to fsc_find_local(), which already
# covers explicit registration, cache/env overrides, and sibling discovery.
#
# With DOWNLOAD_AND_ADD_SUBDIRECTORY, the fetched source is not configured by
# CPM itself but by an explicit add_subdirectory() call — required for
# packages whose CMakeLists invokes project()-scoped macros that must run as
# a subdirectory (e.g. FrozenStarCrystal-core).
#
# TEST_TARGET overrides the target name used to detect "already present".
# Defaults to <name>.
# ------------------------------------------------------------------------------
function(fsc_cpm_or_local NAME)
    cmake_parse_arguments(ARG
        "DOWNLOAD_AND_ADD_SUBDIRECTORY"
        "GITHUB_REPOSITORY;GIT_TAG;TEST_TARGET"
        "OPTIONS"
        ${ARGN})

    if(NOT ARG_TEST_TARGET)
        set(ARG_TEST_TARGET "${NAME}")
    endif()

    if(TARGET ${ARG_TEST_TARGET})
        message(STATUS "${NAME}: already provided")
        return()
    endif()

    fsc_find_local("${NAME}" _local)
    if(_local)
        message(STATUS "${NAME}: using local checkout at ${_local}")
        add_subdirectory("${_local}"
            "${CMAKE_BINARY_DIR}/_deps/${NAME}-build")
        return()
    endif()

    if(ARG_DOWNLOAD_AND_ADD_SUBDIRECTORY)
        CPMAddPackage(
            NAME ${NAME}
            GITHUB_REPOSITORY ${ARG_GITHUB_REPOSITORY}
            GIT_TAG ${ARG_GIT_TAG}
            DOWNLOAD_ONLY YES
            OPTIONS ${ARG_OPTIONS})

        set(_src_var "${NAME}_SOURCE_DIR")
        if(EXISTS "${${_src_var}}/CMakeLists.txt")
            add_subdirectory("${${_src_var}}"
                "${CMAKE_BINARY_DIR}/_deps/${NAME}-build")
        endif()
    else()
        CPMAddPackage(
            NAME ${NAME}
            GITHUB_REPOSITORY ${ARG_GITHUB_REPOSITORY}
            GIT_TAG ${ARG_GIT_TAG}
            OPTIONS ${ARG_OPTIONS})
    endif()
endfunction()
