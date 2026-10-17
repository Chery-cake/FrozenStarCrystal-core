# ==============================================================================
# FSCDeps.cmake - Generic third-party dependency helpers
# ==============================================================================
include_guard(GLOBAL)


# ------------------------------------------------------------------------------
# _fsc_source_pkg_name(<repo> <fallback> <out_var>)   [internal]
#
# Derives a stable source-package name from the repository URL
# (shader-slang/slang -> slang_source). Two fsc_* calls targeting the same
# repo therefore share one CPM source download.
# ------------------------------------------------------------------------------
function(_fsc_source_pkg_name REPO FALLBACK OUT_VAR)
    if(REPO)
        string(REGEX REPLACE "^.*/" "" _base "${REPO}")
        set(${OUT_VAR} "${_base}_source" PARENT_SCOPE)
    else()
        set(${OUT_VAR} "${FALLBACK}_source" PARENT_SCOPE)
    endif()
endfunction()


# ------------------------------------------------------------------------------
# _fsc_resolve_source(<src_pkg> <repo> <tag> <out_var> [CPM_ARGS <args>...])
#
# Returns the on-disk source directory for <src_pkg>. Honors a sibling's
# FETCHCONTENT_SOURCE_DIR_<NAME>; otherwise fetches via CPM (DOWNLOAD_ONLY).
# ------------------------------------------------------------------------------
function(_fsc_resolve_source SRC_PKG REPO TAG OUT_VAR)
    cmake_parse_arguments(ARG "" "" "CPM_ARGS" ${ARGN})

    string(TOUPPER "${SRC_PKG}" _upper)
    if(DEFINED FETCHCONTENT_SOURCE_DIR_${_upper}
       AND EXISTS "${FETCHCONTENT_SOURCE_DIR_${_upper}}/CMakeLists.txt")
        message(STATUS "${SRC_PKG}: using cached source at ${FETCHCONTENT_SOURCE_DIR_${_upper}}")
        set(${OUT_VAR} "${FETCHCONTENT_SOURCE_DIR_${_upper}}" PARENT_SCOPE)
        return()
    endif()

    CPMAddPackage(
        NAME ${SRC_PKG}
        GITHUB_REPOSITORY ${REPO}
        GIT_TAG ${TAG}
        DOWNLOAD_ONLY YES
        ${ARG_CPM_ARGS})
    set(${OUT_VAR} "${${SRC_PKG}_SOURCE_DIR}" PARENT_SCOPE)
endfunction()


# ------------------------------------------------------------------------------
# fsc_find_shared_install(<name> SUBDIR <dir> HEADER <relpath>)
#
# Scans every registered FSC checkout for <checkout>/build/_deps/<SUBDIR>
# containing <HEADER>. Sets in PARENT_SCOPE:
#   FSC_<name>_INSTALL_DIR  — chosen install prefix
#   FSC_<name>_NEED_BUILD   — TRUE if the prefix is empty
#   FSC_<name>_SHARED_FROM  — checkout that provided it ("" for local)
# ------------------------------------------------------------------------------
function(fsc_find_shared_install NAME)
    cmake_parse_arguments(ARG "" "SUBDIR;HEADER" "" ${ARGN})
    if(NOT ARG_SUBDIR)
        set(ARG_SUBDIR "${NAME}")
    endif()
    if(NOT ARG_HEADER)
        message(FATAL_ERROR "fsc_find_shared_install(${NAME}): HEADER required")
    endif()

    get_property(_checkouts GLOBAL PROPERTY FSC_CHECKOUTS)

    set(_install "")
    set(_owner   "")
    foreach(_co IN LISTS _checkouts)
        set(_c "${_co}/build/_deps/${ARG_SUBDIR}")
        if(EXISTS "${_c}/${ARG_HEADER}")
            set(_install "${_c}")
            set(_owner   "${_co}")
            break()
        endif()
    endforeach()

    set(_need TRUE)
    if(_install STREQUAL "")
        set(_install "${CMAKE_BINARY_DIR}/_deps/${ARG_SUBDIR}")
        if(EXISTS "${_install}/${ARG_HEADER}")
            set(_need FALSE)
            set(_owner "(local)")
        endif()
    else()
        set(_need FALSE)
    endif()

    set(FSC_${NAME}_INSTALL_DIR "${_install}" PARENT_SCOPE)
    set(FSC_${NAME}_NEED_BUILD  "${_need}"    PARENT_SCOPE)
    set(FSC_${NAME}_SHARED_FROM "${_owner}"   PARENT_SCOPE)

    if(_need)
        message(STATUS "${NAME}: build required → ${_install}")
    else()
        message(STATUS "${NAME}: reusing install at ${_install}")
    endif()
endfunction()


# ------------------------------------------------------------------------------
# fsc_add_library(NAME <name>
#     GITHUB_REPOSITORY <repo> GIT_TAG <tag>
#     [CPM_ARGS <args>...]                          # passed to CPMAddPackage
#     [TEST_TARGET <target>]                        # default: <NAME>
#     [POST_ADD_COMMANDS <cmake code>...]           # run after target exists
#
#   -- External-project mode (shares install trees across FSC checkouts) --
#     [INSTALL_SUBDIR <dir>]                        # presence selects this mode
#     [INSTALL_HEADER <relpath>]                    # sentinel file
#     [INTERFACE_LIB_NAME <filename>]               # override auto-derived lib
#     [INTERFACE_INCLUDE_DIRS <dirs>...]            # extra includes
#     [INTERFACE_COMPILE_DEFINITIONS <defs>...]
#     [INTERFACE_COMPILE_OPTIONS <opts>...]
#     [INTERFACE_LINK_LIBS <libs>...]               # extra links
#     [EXTERNAL_PROJECT_DEPENDS <targets>...]
#     [EXTERNAL_PROJECT_ARGS <args>...]             # CMAKE_ARGS/BUILD_COMMAND/
#                                                   # INSTALL_COMMAND/BYPRODUCTS
# )
# ------------------------------------------------------------------------------
function(fsc_add_library)
    cmake_parse_arguments(ARG
        ""
        "NAME;GITHUB_REPOSITORY;GIT_TAG;TEST_TARGET;INSTALL_SUBDIR;INSTALL_HEADER;INTERFACE_LIB_NAME"
        "CPM_ARGS;POST_ADD_COMMANDS;INTERFACE_INCLUDE_DIRS;INTERFACE_COMPILE_DEFINITIONS;INTERFACE_COMPILE_OPTIONS;INTERFACE_LINK_LIBS;EXTERNAL_PROJECT_DEPENDS;EXTERNAL_PROJECT_ARGS"
        ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "fsc_add_library: NAME is required")
    endif()
    set(NAME "${ARG_NAME}")

    if(NOT ARG_TEST_TARGET)
        set(ARG_TEST_TARGET "${NAME}")
    endif()
    if(TARGET ${ARG_TEST_TARGET})
        message(STATUS "${NAME}: already provided")
        return()
    endif()

    # --------------------------------------------------------------------------
    # External-project mode (install-tree sharing)
    # --------------------------------------------------------------------------
    if(ARG_INSTALL_SUBDIR)
        if(NOT ARG_INSTALL_HEADER)
            message(FATAL_ERROR
                "fsc_add_library(${NAME}): INSTALL_HEADER required when "
                "INSTALL_SUBDIR is set")
        endif()

        fsc_find_shared_install(${NAME}
            SUBDIR ${ARG_INSTALL_SUBDIR} HEADER ${ARG_INSTALL_HEADER})
        set(_install "${FSC_${NAME}_INSTALL_DIR}")

        # Auto-derive the primary library file, with per-platform names
        if(ARG_INTERFACE_LIB_NAME)
            set(_lib "${_install}/lib/${ARG_INTERFACE_LIB_NAME}")
        elseif(WIN32 AND MSVC)
            set(_lib "${_install}/lib/${NAME}.lib")
        elseif(WIN32)
            set(_lib "${_install}/lib/lib${NAME}.dll.a")
        else()
            set(_lib "${_install}/lib/lib${NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
        endif()

        if(FSC_${NAME}_NEED_BUILD)
            # Fetch source, honoring FETCHCONTENT_SOURCE_DIR_* from sibling cache
            _fsc_source_pkg_name("${ARG_GITHUB_REPOSITORY}" "${NAME}" _src_pkg)
            _fsc_resolve_source("${_src_pkg}"
                "${ARG_GITHUB_REPOSITORY}" "${ARG_GIT_TAG}" _source
                CPM_ARGS ${ARG_CPM_ARGS})

            # Default INSTALL_COMMAND that forces --prefix <INSTALL_DIR>. Some
            # projects (e.g. slang) reset CMAKE_INSTALL_PREFIX during configure,
            # so relying on ExternalProject's INSTALL_DIR alone is not enough.
            # --prefix at install time overrides whatever the cache contains.
            # Skipped if the caller supplied their own INSTALL_COMMAND.
            list(FIND ARG_EXTERNAL_PROJECT_ARGS INSTALL_COMMAND _has_install_cmd)
            if(_has_install_cmd EQUAL -1)
                set(_install_cmd_override
                    INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
                        --prefix <INSTALL_DIR> --config $<CONFIG>)
            else()
                set(_install_cmd_override "")
            endif()

            include(ExternalProject)
            ExternalProject_Add(
                _${NAME}
                SOURCE_DIR   "${_source}"
                BINARY_DIR   "${CMAKE_BINARY_DIR}/_deps/${NAME}-build"
                INSTALL_DIR  "${_install}"
                DEPENDS      ${ARG_EXTERNAL_PROJECT_DEPENDS}
                BUILD_BYPRODUCTS "${_lib}"
                ${_install_cmd_override}
                ${ARG_EXTERNAL_PROJECT_ARGS})
        endif()

        # Interface target
        add_library(${NAME} INTERFACE)
        target_include_directories(${NAME} INTERFACE
            "${_install}/include"
            ${ARG_INTERFACE_INCLUDE_DIRS})
        if(ARG_INTERFACE_COMPILE_DEFINITIONS)
            target_compile_definitions(${NAME} INTERFACE ${ARG_INTERFACE_COMPILE_DEFINITIONS})
        endif()
        if(ARG_INTERFACE_COMPILE_OPTIONS)
            target_compile_options(${NAME} INTERFACE ${ARG_INTERFACE_COMPILE_OPTIONS})
        endif()
        target_link_libraries(${NAME} INTERFACE "${_lib}" ${ARG_INTERFACE_LINK_LIBS})

        if(FSC_${NAME}_NEED_BUILD)
            add_dependencies(${NAME} _${NAME})
        endif()

        set(FSC_${NAME}_INSTALL_DIR "${_install}" PARENT_SCOPE)

    # --------------------------------------------------------------------------
    # CPM mode (source sharing only — library builds in-tree)
    # --------------------------------------------------------------------------
    else()
        CPMAddPackage(
            NAME ${NAME}
            GITHUB_REPOSITORY ${ARG_GITHUB_REPOSITORY}
            GIT_TAG ${ARG_GIT_TAG}
            ${ARG_CPM_ARGS})
    endif()

    foreach(_cmd IN LISTS ARG_POST_ADD_COMMANDS)
        cmake_language(EVAL CODE "${_cmd}")
    endforeach()
endfunction()


# ------------------------------------------------------------------------------
# fsc_external_install(<name>
#     GITHUB_REPOSITORY <repo> GIT_TAG <tag>
#     INSTALL_SUBDIR <dir> INSTALL_HEADER <relpath>
#     [CPM_ARGS <args>...]
#     [EXTERNAL_PROJECT_DEPENDS <targets>...]
#     [EXTERNAL_PROJECT_ARGS <args>...]
# )
#
# Builds an ExternalProject into a shareable install prefix
# (build/_deps/<INSTALL_SUBDIR>) and reuses it from any sibling FSC checkout
# that already has it. INSTALL_HEADER is a sentinel file relative to the
# install prefix (e.g. "bin/slang-embed").
#
# Intended for build-time prerequisites that produce tools/headers but no
# linkable target. For libraries that become INTERFACE targets, use
# fsc_add_library() instead.
#
# Exports in PARENT_SCOPE:
#   FSC_<name>_INSTALL_DIR  — chosen install prefix
#   FSC_<name>_NEED_BUILD   — TRUE if this project must build it
#   FSC_<name>_TARGET       — ExternalProject target name ("_<name>") or ""
#                             (empty when reused from a sibling — safe to pass
#                             straight into EXTERNAL_PROJECT_DEPENDS, since
#                             cmake_parse_arguments accepts zero values)
# ------------------------------------------------------------------------------
function(fsc_external_install)
    cmake_parse_arguments(ARG
        ""
        "NAME;GITHUB_REPOSITORY;GIT_TAG;INSTALL_SUBDIR;INSTALL_HEADER"
        "CPM_ARGS;EXTERNAL_PROJECT_DEPENDS;EXTERNAL_PROJECT_ARGS"
        ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "fsc_external_install: NAME is required")
    endif()
    if(NOT ARG_INSTALL_SUBDIR OR NOT ARG_INSTALL_HEADER)
        message(FATAL_ERROR "fsc_external_install(${ARG_NAME}): INSTALL_SUBDIR and INSTALL_HEADER required")
    endif()
    set(NAME "${ARG_NAME}")

    fsc_find_shared_install(${NAME}
        SUBDIR ${ARG_INSTALL_SUBDIR} HEADER ${ARG_INSTALL_HEADER})
    set(_install "${FSC_${NAME}_INSTALL_DIR}")
    set(_target  "")

    if(FSC_${NAME}_NEED_BUILD)
        set(_target "_${NAME}")

        _fsc_source_pkg_name("${ARG_GITHUB_REPOSITORY}" "${NAME}" _src_pkg)
        _fsc_resolve_source("${_src_pkg}"
            "${ARG_GITHUB_REPOSITORY}" "${ARG_GIT_TAG}" _source
            CPM_ARGS ${ARG_CPM_ARGS})

        list(FIND ARG_EXTERNAL_PROJECT_ARGS INSTALL_COMMAND _has_install_cmd)
        if(_has_install_cmd EQUAL -1)
            set(_install_cmd_override
                INSTALL_COMMAND ${CMAKE_COMMAND} --install <BINARY_DIR>
                    --prefix <INSTALL_DIR> --config $<CONFIG>)
        else()
            set(_install_cmd_override "")
        endif()

        include(ExternalProject)
        ExternalProject_Add(
            ${_target}
            SOURCE_DIR   "${_source}"
            BINARY_DIR   "${CMAKE_BINARY_DIR}/_deps/${NAME}-build"
            INSTALL_DIR  "${_install}"
            DEPENDS      ${ARG_EXTERNAL_PROJECT_DEPENDS}
            ${_install_cmd_override}
            ${ARG_EXTERNAL_PROJECT_ARGS})
    endif()

    set(FSC_${NAME}_INSTALL_DIR "${_install}"          PARENT_SCOPE)
    set(FSC_${NAME}_NEED_BUILD  "${FSC_${NAME}_NEED_BUILD}" PARENT_SCOPE)
    set(FSC_${NAME}_TARGET      "${_target}"           PARENT_SCOPE)
endfunction()
