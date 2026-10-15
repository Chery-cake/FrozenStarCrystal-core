include_guard(GLOBAL)

# ------------------------------------------------------------------------------
# target_create_export(<target> [MODULES <files>...])
#
# Generates a <target>_export.h and wires up the exports definition. Also
# forces the C++ module units to depend on the generated header so they don't
# race ahead during the initial configure.
# ------------------------------------------------------------------------------
function(target_create_export TARGET_NAME)
    cmake_parse_arguments(ARG "" "" "MODULES" ${ARGN})

    string(TOUPPER "${TARGET_NAME}" BASE_NAME_UPPER)
    string(REGEX REPLACE "-" "_" BASE_NAME_UPPER "${BASE_NAME_UPPER}")
    set(EXPORT_MACRO_NAME  "${BASE_NAME_UPPER}_API")
    set(EXPORT_HEADER_NAME "${TARGET_NAME}_export.h")

    include(GenerateExportHeader)
    generate_export_header(${TARGET_NAME}
        BASE_NAME        ${BASE_NAME_UPPER}
        EXPORT_MACRO_NAME ${EXPORT_MACRO_NAME}
        EXPORT_FILE_NAME  ${EXPORT_HEADER_NAME}
    )

    target_compile_definitions(${TARGET_NAME} PRIVATE ${BASE_NAME_UPPER}_EXPORTS)

    # Generated header must be discoverable by consumers at build time.
    target_include_directories(${TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

    # Only export the header when this project is the top-level build.
    if(PROJECT_IS_TOP_LEVEL)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_HEADER_NAME}"
            DESTINATION include/${PROJECT_NAME})
    endif()

    if(ARG_MODULES)
        set_source_files_properties(${ARG_MODULES} PROPERTIES
            OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_HEADER_NAME}")
    endif()
endfunction()

# ------------------------------------------------------------------------------
# add_module([SOURCES ...] [PUBLIC_HEADERS ...] [PRIVATE_HEADERS ...]
#            [MODULES ...] [PUBLIC_DEPS ...] [PRIVATE_DEPS ...])
#
# Creates and fully configures the library target for the current module
# (${PROJECT_NAME}). Applies warnings, build config, sanitizers, platform
# settings, rpath, export header, and install rules in a single canonical
# order so every module in the ecosystem behaves identically.
# ------------------------------------------------------------------------------
function(add_module)
    cmake_parse_arguments(ARG "" ""
        "SOURCES;PUBLIC_HEADERS;PRIVATE_HEADERS;MODULES;PUBLIC_DEPS;PRIVATE_DEPS;EXTRA_SOURCES;EXTRA_PUBLIC_HEADERS;EXTRA_PRIVATE_HEADERS;EXTRA_MODULES"
        ${ARGN})

    # --- Auto-discovery (matches your previous conventions) -----------------
    if(NOT ARG_SOURCES)
        file(GLOB_RECURSE ARG_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        )
    endif()

    if(NOT ARG_PUBLIC_HEADERS)
        file(GLOB_RECURSE ARG_PUBLIC_HEADERS CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        )
    endif()

    if(NOT ARG_PRIVATE_HEADERS)
        file(GLOB_RECURSE ARG_PRIVATE_HEADERS CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        )
    endif()

    if(NOT ARG_MODULES)
        file(GLOB_RECURSE ARG_MODULES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cppm"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/*.cppm"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/*.cc"
        )
    endif()

    # --- Merge extras into the primary lists --------------------------------
    list(APPEND ARG_SOURCES          ${ARG_EXTRA_SOURCES})
    list(APPEND ARG_PUBLIC_HEADERS   ${ARG_EXTRA_PUBLIC_HEADERS})
    list(APPEND ARG_PRIVATE_HEADERS  ${ARG_EXTRA_PRIVATE_HEADERS})
    list(APPEND ARG_MODULES          ${ARG_EXTRA_MODULES})

    # --- Target -------------------------------------------------------------
    add_library(${PROJECT_NAME})

    target_sources(${PROJECT_NAME}
        PRIVATE
            ${ARG_SOURCES}
            ${ARG_PRIVATE_HEADERS}
        PUBLIC
            FILE_SET HEADERS FILES ${ARG_PUBLIC_HEADERS}
    )

    if(ARG_MODULES)
        target_sources(${PROJECT_NAME}
            PUBLIC FILE_SET CXX_MODULES FILES ${ARG_MODULES})
    endif()

    # --- Include dirs (build + install interface) ---------------------------
    target_include_directories(${PROJECT_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    # --- Dependencies -------------------------------------------------------
    if(ARG_PUBLIC_DEPS)
        target_link_libraries(${PROJECT_NAME} PUBLIC ${ARG_PUBLIC_DEPS})
    endif()
    if(ARG_PRIVATE_DEPS)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${ARG_PRIVATE_DEPS})
    endif()

    # --- Defines ------------------------------------------------------------
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:ENGINE_DEBUG>
        $<$<PLATFORM_ID:Darwin>:_LIBCPP_HAS_PARALLEL_ALGORITHMS=1>
    )

    # --- Target properties --------------------------------------------------
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VERSION                    ${PROJECT_VERSION}
        SOVERSION                  ${PROJECT_VERSION_MAJOR}
        EXPORT_NAME                ${PROJECT_NAME}
        POSITION_INDEPENDENT_CODE  ON
        CXX_VISIBILITY_PRESET      hidden
        VISIBILITY_INLINES_HIDDEN  ON
    )

    # --- Export header ------------------------------------------------------
    target_create_export(${PROJECT_NAME} MODULES ${ARG_MODULES})

    # --- Standard quality + build configuration -----------------------------
    # Done in the SAME ORDER for every module so behaviour cannot drift.
    target_configure_build(${PROJECT_NAME})     # warnings + debug/release + LTO + sanitizers
    target_configure_platform(${PROJECT_NAME})  # per-OS defines + libs
    target_configure_rpath(${PROJECT_NAME})     # rpath for install tree

    # --- Install (only when consumed as the top-level project) --------------
    if(PROJECT_IS_TOP_LEVEL)
        install(TARGETS ${PROJECT_NAME}
            EXPORT      ${PROJECT_NAME}Targets
            RUNTIME     DESTINATION bin
            LIBRARY     DESTINATION lib
            ARCHIVE     DESTINATION lib
            FILE_SET HEADERS DESTINATION include
            FILE_SET CXX_MODULES DESTINATION include
        )
    endif()
endfunction()
