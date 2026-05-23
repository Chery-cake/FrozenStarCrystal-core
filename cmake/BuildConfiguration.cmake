# ==============================================================================
# BuildConfiguration.cmake - Debug and Release Build Configuration
# ==============================================================================
# Provides comprehensive build settings for Debug and Release configurations.
# - Debug: Debug symbols, sanitizers, unobfuscated code
# - Release: Full optimizations, LTO, stripped symbols
# ==============================================================================

# ==============================================================================
# Sanitizer Suppression File Path
# ==============================================================================
set(SANITIZER_SUPPRESSION_DIR "${CMAKE_SOURCE_DIR}/cmake/sanitizers")
set(LSAN_SUPPRESSION_FILE "${SANITIZER_SUPPRESSION_DIR}/lsan.supp")
set(ASAN_SUPPRESSION_FILE "${SANITIZER_SUPPRESSION_DIR}/asan.supp")

# ==============================================================================
# Function to configure Debug build settings for a target
# ==============================================================================
function(target_configure_debug TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_debug: Target '${TARGET_NAME}' does not exist")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Debug-specific compile options
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                -g3                     # Maximum debug information
                -O0                     # No optimization
                -fno-omit-frame-pointer # Keep frame pointers for better stack traces
                -fno-optimize-sibling-calls # Better stack traces
                -fstack-protector-strong # Stack overflow protection
            >
        )

        # Debug definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                DEBUG
                _DEBUG
                ENGINE_DEBUG
            >
        )
    elseif(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                /Zi         # Debug information
                /Od         # No optimization
                /RTC1       # Runtime checks
                /GS         # Buffer security check
                /sdl        # Additional security checks
            >
        )

        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:
                DEBUG
                _DEBUG
                ENGINE_DEBUG
            >
        )
    endif()
endfunction()

# ==============================================================================
# Function to configure Release build settings for a target
# ==============================================================================
function(target_configure_release TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_release: Target '${TARGET_NAME}' does not exist")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Release-specific compile options - maximum optimization
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                -O3                     # Maximum optimization
                -funroll-loops          # Unroll loops
                -ffunction-sections     # Place each function in its own section
                -fdata-sections         # Place each data item in its own section
                -fvisibility=hidden     # Hide symbols by default (obfuscation)
                -fvisibility-inlines-hidden # Hide inline function symbols
            >
        )

        # Optional: Optimize for current CPU (not suitable for distribution)
        if(ENGINE_OPTIMIZE_NATIVE)
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Release>:
                    -march=native       # Optimize for current CPU
                    -mtune=native       # Tune for current CPU
                >
            )
        endif()

        # Optional: Enable fast-math (breaks IEEE 754 compliance)
        # WARNING: This may cause issues with physics simulations, financial
        # calculations, or any code requiring strict floating-point behavior.
        if(ENGINE_FAST_MATH)
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Release>:-ffast-math>
            )
        endif()

        # Release definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        # Linker options for Release
        if(APPLE)
            target_link_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Release>:
                    -Wl,-dead_strip         # Remove unused code (Apple ld)
                >
            )
        else()
            target_link_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Release>:
                    -Wl,--gc-sections       # Remove unused sections
                    -Wl,--strip-all         # Strip all symbols
                    -Wl,-s                  # Strip symbol table
                >
            )
        endif()
    elseif(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                /O2         # Maximum optimization
                /Ob2        # Inline expansion
                /Oi         # Intrinsic functions
                /Ot         # Favor fast code
                /GL         # Whole program optimization
                /Gy         # Function-level linking
            >
        )

        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                NDEBUG
                ENGINE_RELEASE
            >
        )

        target_link_options(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Release>:
                /LTCG       # Link-time code generation
                /OPT:REF    # Remove unreferenced code
                /OPT:ICF    # COMDAT folding
            >
        )
    endif()
endfunction()

# ==============================================================================
# Function to enable Link-Time Optimization (LTO) for Release builds
# ==============================================================================
function(target_enable_lto TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_enable_lto: Target '${TARGET_NAME}' does not exist")
    endif()

    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

    if(lto_supported)
        set_target_properties(${TARGET_NAME} PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
        )
        message(STATUS "LTO enabled for ${TARGET_NAME} (Release builds)")
    else()
        message(WARNING "LTO not supported for ${TARGET_NAME}: ${lto_error}")
    endif()
endfunction()

# ==============================================================================
# Function to configure sanitizer suppressions for a target
# ==============================================================================
# This function:
# 1. Configures sanitizer options with suppression file paths
# 2. Generates launcher scripts (separate scripts for incompatible groups):
#    - run_<target>.sh/.bat:       ASan + LSan + UBSan via LD_PRELOAD
#    - run_<target>_tsan.sh:       TSan (+ UBSan) via LD_PRELOAD
#    - run_<target>_msan.sh:       MSan (+ UBSan) via LD_PRELOAD (Clang only)
# 3. Configures ctest to use the same suppression files
#
# Sanitizer compatibility:
#   ASan + LSan + UBSan → compatible, share Script 1
#   TSan + UBSan        → compatible, share Script 2 (TSan conflicts with ASan/LSan/MSan)
#   MSan + UBSan        → compatible, share Script 3 (MSan conflicts with ASan/TSan, Clang only)
# ==============================================================================
function(target_enable_sanitizers TARGET_NAME)
    set(options ADDRESS LEAK UNDEFINED THREAD MEMORY)
    set(oneValueArgs "")
    set(multiValueArgs "")
    cmake_parse_arguments(SANITIZER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_enable_sanitizers: Target '${TARGET_NAME}' does not exist")
    endif()

    # Get the output directory for the target
    get_target_property(TARGET_RUNTIME_DIR ${TARGET_NAME} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT TARGET_RUNTIME_DIR)
        set(TARGET_RUNTIME_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()

    # ==========================================================================
    # MSVC sanitizer support
    # ==========================================================================
    # MSVC only supports AddressSanitizer (/fsanitize=address).
    # Unlike GCC/Clang, MSVC has no LD_PRELOAD equivalent, so the compile flag
    # is required for ASan functionality. A .bat launcher script is generated
    # to configure sanitizer behaviour via environment variables, matching the
    # script-based approach used on GCC/Clang.
    # ==========================================================================
    if(MSVC)
        if(SANITIZER_ADDRESS)
            target_compile_options(${TARGET_NAME} PRIVATE
                $<$<CONFIG:Debug>:/fsanitize=address>
            )

            # Build ASAN_OPTIONS
            set(ASAN_OPTIONS "detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1")
            if(EXISTS "${ASAN_SUPPRESSION_FILE}")
                set(ASAN_OPTIONS "${ASAN_OPTIONS}:suppressions=${ASAN_SUPPRESSION_FILE}")
                message(STATUS "Using ASan suppression file: ${ASAN_SUPPRESSION_FILE}")
            endif()

            # Generate .bat launcher script
            set(LAUNCHER_SCRIPT "${TARGET_RUNTIME_DIR}/run_${TARGET_NAME}.bat")
            file(WRITE "${LAUNCHER_SCRIPT}"
"@echo off
REM ==============================================================================
REM Launcher script for ${TARGET_NAME} with AddressSanitizer
REM Generated by CMake - DO NOT EDIT
REM ==============================================================================

set ASAN_OPTIONS=${ASAN_OPTIONS}

\"%~dp0${TARGET_NAME}.exe\" %*
")
            message(STATUS "AddressSanitizer enabled for ${TARGET_NAME} (Debug builds, launcher: run_${TARGET_NAME}.bat)")
        endif()
        if(SANITIZER_LEAK)
            message(WARNING "LeakSanitizer is not supported by MSVC - skipping")
        endif()
        if(SANITIZER_UNDEFINED)
            message(WARNING "UndefinedBehaviorSanitizer is not supported by MSVC - skipping")
        endif()
        if(SANITIZER_THREAD)
            message(WARNING "ThreadSanitizer is not supported by MSVC - skipping")
        endif()
        if(SANITIZER_MEMORY)
            message(WARNING "MemorySanitizer is not supported by MSVC - skipping")
        endif()
        message(STATUS "Sanitizer suppressions configured for ${TARGET_NAME}")
        return()
    endif()

    # ==========================================================================
    # GCC/Clang sanitizer support (via launcher scripts with LD_PRELOAD)
    # ==========================================================================
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # Determine preload environment variable and library extension (platform-specific)
        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(PRELOAD_VAR "DYLD_INSERT_LIBRARIES")
            set(_SAN_LIB_EXT "dylib")
        else()
            set(PRELOAD_VAR "LD_PRELOAD")
            set(_SAN_LIB_EXT "so")
        endif()

        # ==================================================================
        # Find sanitizer shared library paths
        # ==================================================================
        set(_ASAN_LIB "")
        set(_UBSAN_LIB "")
        set(_TSAN_LIB "")
        set(_MSAN_LIB "")

        if(SANITIZER_ADDRESS)
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libasan.${_SAN_LIB_EXT}
                OUTPUT_VARIABLE _ASAN_LIB OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
            )
            if(NOT IS_ABSOLUTE "${_ASAN_LIB}")
                set(_ASAN_LIB "")
            endif()
        endif()

        if(SANITIZER_UNDEFINED)
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libubsan.${_SAN_LIB_EXT}
                OUTPUT_VARIABLE _UBSAN_LIB OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
            )
            if(NOT IS_ABSOLUTE "${_UBSAN_LIB}")
                set(_UBSAN_LIB "")
            endif()
        endif()

        if(SANITIZER_THREAD)
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libtsan.${_SAN_LIB_EXT}
                OUTPUT_VARIABLE _TSAN_LIB OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
            )
            if(NOT IS_ABSOLUTE "${_TSAN_LIB}")
                set(_TSAN_LIB "")
            endif()
        endif()

        if(SANITIZER_MEMORY AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # MSan is Clang-only; its runtime uses the libclang_rt naming convention
            # (e.g. libclang_rt.msan-x86_64.so) unlike ASan/TSan/UBSan which have
            # GCC-compatible aliases (libasan.so, libtsan.so, libubsan.so).
            execute_process(
                COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libclang_rt.msan-${CMAKE_SYSTEM_PROCESSOR}.${_SAN_LIB_EXT}
                OUTPUT_VARIABLE _MSAN_LIB OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
            )
            if(NOT IS_ABSOLUTE "${_MSAN_LIB}")
                set(_MSAN_LIB "")
            endif()
        endif()

        # ==================================================================
        # Script 1: ASan + LSan + UBSan launcher
        # ==================================================================
        if(SANITIZER_ADDRESS OR SANITIZER_LEAK OR SANITIZER_UNDEFINED)
            # Build environment variable exports
            set(SANITIZER_EXPORT "")

            if(SANITIZER_ADDRESS)
                set(ASAN_OPTIONS "detect_leaks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1")
                if(EXISTS "${ASAN_SUPPRESSION_FILE}")
                    set(ASAN_OPTIONS "${ASAN_OPTIONS}:suppressions=${ASAN_SUPPRESSION_FILE}")
                    message(STATUS "Using ASan suppression file: ${ASAN_SUPPRESSION_FILE}")
                endif()
                set(SANITIZER_EXPORT "export ASAN_OPTIONS=\"${ASAN_OPTIONS}\"")
                message(STATUS "AddressSanitizer enabled for ${TARGET_NAME} (via launcher script)")
            endif()

            if(SANITIZER_LEAK)
                if(EXISTS "${LSAN_SUPPRESSION_FILE}")
                    set(LSAN_OPTIONS "suppressions=${LSAN_SUPPRESSION_FILE}:print_suppressions=0")
                    message(STATUS "Using LSan suppression file: ${LSAN_SUPPRESSION_FILE}")
                else()
                    set(LSAN_OPTIONS "print_suppressions=0")
                    message(STATUS "LSan suppression file not found, using defaults")
                endif()
                set(SANITIZER_EXPORT "${SANITIZER_EXPORT}\nexport LSAN_OPTIONS=\"${LSAN_OPTIONS}\"")
                message(STATUS "LeakSanitizer enabled for ${TARGET_NAME} (via launcher script)")
            endif()

            if(SANITIZER_UNDEFINED)
                set(SANITIZER_EXPORT "${SANITIZER_EXPORT}\nexport UBSAN_OPTIONS=\"print_stacktrace=1\"")
                message(STATUS "UndefinedBehaviorSanitizer enabled for ${TARGET_NAME} (via launcher script)")
            endif()

            # Build LD_PRELOAD string for ASan + UBSan
            set(SANITIZER_PRELOAD "")
            if(_ASAN_LIB)
                set(SANITIZER_PRELOAD "${_ASAN_LIB}")
            endif()
            if(_UBSAN_LIB)
                if(SANITIZER_PRELOAD)
                    set(SANITIZER_PRELOAD "${SANITIZER_PRELOAD}:${_UBSAN_LIB}")
                else()
                    set(SANITIZER_PRELOAD "${_UBSAN_LIB}")
                endif()
            endif()

            set(LAUNCHER_SCRIPT "${TARGET_RUNTIME_DIR}/run_${TARGET_NAME}.sh")
            file(WRITE "${LAUNCHER_SCRIPT}"
"#!/bin/bash
# ==============================================================================
# Launcher script for ${TARGET_NAME} with ASan + LSan + UBSan
# Generated by CMake - DO NOT EDIT
# ==============================================================================

SCRIPT_DIR=\"$(cd \"$(dirname \"\${BASH_SOURCE[0]}\")\" && pwd)\"

# Set sanitizer options (configured by CMake)
${SANITIZER_EXPORT}

# Preload sanitizer runtimes to avoid library ordering issues
if [ -n \"${SANITIZER_PRELOAD}\" ]; then
    export ${PRELOAD_VAR}=\"${SANITIZER_PRELOAD}\"
fi

exec \"\${SCRIPT_DIR}/${TARGET_NAME}\" \"$@\"
")
            file(CHMOD "${LAUNCHER_SCRIPT}"
                FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                                 GROUP_READ GROUP_EXECUTE
                                 WORLD_READ WORLD_EXECUTE
            )
            message(STATUS "Generated ASan/LSan/UBSan launcher script: ${LAUNCHER_SCRIPT}")
        endif()

        # ==================================================================
        # Script 2: TSan (ThreadSanitizer) launcher
        # TSan conflicts with ASan, LSan, and MSan but is compatible with UBSan
        # ==================================================================
        if(SANITIZER_THREAD)
            set(TSAN_EXPORT "export TSAN_OPTIONS=\"history_size=7:second_deadlock_stack=1\"")

            # Include UBSan options in TSan script if also enabled
            if(SANITIZER_UNDEFINED)
                set(TSAN_EXPORT "${TSAN_EXPORT}\nexport UBSAN_OPTIONS=\"print_stacktrace=1\"")
            endif()

            # Build LD_PRELOAD string for TSan (+ UBSan if enabled)
            set(TSAN_PRELOAD "")
            if(_TSAN_LIB)
                set(TSAN_PRELOAD "${_TSAN_LIB}")
            endif()
            if(_UBSAN_LIB)
                if(TSAN_PRELOAD)
                    set(TSAN_PRELOAD "${TSAN_PRELOAD}:${_UBSAN_LIB}")
                else()
                    set(TSAN_PRELOAD "${_UBSAN_LIB}")
                endif()
            endif()

            set(TSAN_SCRIPT "${TARGET_RUNTIME_DIR}/run_${TARGET_NAME}_tsan.sh")
            file(WRITE "${TSAN_SCRIPT}"
"#!/bin/bash
# ==============================================================================
# Launcher script for ${TARGET_NAME} with ThreadSanitizer
# Generated by CMake - DO NOT EDIT
# ==============================================================================

SCRIPT_DIR=\"$(cd \"$(dirname \"\${BASH_SOURCE[0]}\")\" && pwd)\"

# Set sanitizer options
${TSAN_EXPORT}

# Preload sanitizer runtimes to avoid library ordering issues
if [ -n \"${TSAN_PRELOAD}\" ]; then
    export ${PRELOAD_VAR}=\"${TSAN_PRELOAD}\"
fi

exec \"\${SCRIPT_DIR}/${TARGET_NAME}\" \"$@\"
")
            file(CHMOD "${TSAN_SCRIPT}"
                FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                                 GROUP_READ GROUP_EXECUTE
                                 WORLD_READ WORLD_EXECUTE
            )
            message(STATUS "ThreadSanitizer enabled for ${TARGET_NAME} (via launcher script)")
            message(STATUS "Generated TSan launcher script: ${TSAN_SCRIPT}")
        endif()

        # ==================================================================
        # Script 3: MSan (MemorySanitizer) launcher (Clang only)
        # MSan conflicts with ASan and TSan but is compatible with UBSan
        # ==================================================================
        if(SANITIZER_MEMORY)
            if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                message(WARNING "MemorySanitizer is only available with Clang - skipping")
            else()
                set(MSAN_EXPORT "export MSAN_OPTIONS=\"print_stats=1\"")

                # Include UBSan options in MSan script if also enabled
                if(SANITIZER_UNDEFINED)
                    set(MSAN_EXPORT "${MSAN_EXPORT}\nexport UBSAN_OPTIONS=\"print_stacktrace=1\"")
                endif()

                # Build LD_PRELOAD string for MSan (+ UBSan if enabled)
                set(MSAN_PRELOAD "")
                if(_MSAN_LIB)
                    set(MSAN_PRELOAD "${_MSAN_LIB}")
                endif()
                if(_UBSAN_LIB)
                    if(MSAN_PRELOAD)
                        set(MSAN_PRELOAD "${MSAN_PRELOAD}:${_UBSAN_LIB}")
                    else()
                        set(MSAN_PRELOAD "${_UBSAN_LIB}")
                    endif()
                endif()

                set(MSAN_SCRIPT "${TARGET_RUNTIME_DIR}/run_${TARGET_NAME}_msan.sh")
                file(WRITE "${MSAN_SCRIPT}"
"#!/bin/bash
# ==============================================================================
# Launcher script for ${TARGET_NAME} with MemorySanitizer
# Generated by CMake - DO NOT EDIT
# ==============================================================================

SCRIPT_DIR=\"$(cd \"$(dirname \"\${BASH_SOURCE[0]}\")\" && pwd)\"

# Set sanitizer options
${MSAN_EXPORT}

# Preload sanitizer runtimes to avoid library ordering issues
if [ -n \"${MSAN_PRELOAD}\" ]; then
    export ${PRELOAD_VAR}=\"${MSAN_PRELOAD}\"
fi

exec \"\${SCRIPT_DIR}/${TARGET_NAME}\" \"$@\"
")
                file(CHMOD "${MSAN_SCRIPT}"
                    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                                     GROUP_READ GROUP_EXECUTE
                                     WORLD_READ WORLD_EXECUTE
                )
                message(STATUS "MemorySanitizer enabled for ${TARGET_NAME} (via launcher script)")
                message(STATUS "Generated MSan launcher script: ${MSAN_SCRIPT}")
            endif()
        endif()
    endif()

    message(STATUS "Sanitizer suppressions configured for ${TARGET_NAME}")
endfunction()

# ==============================================================================
# Function to apply all build configurations to a target
# ==============================================================================
function(target_configure_build TARGET_NAME)
    set(options ENABLE_LTO ENABLE_SANITIZERS)
    set(oneValueArgs "")
    set(multiValueArgs SANITIZERS)
    cmake_parse_arguments(CONFIG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT TARGET ${TARGET_NAME})
        message(FATAL_ERROR "target_configure_build: Target '${TARGET_NAME}' does not exist")
    endif()

    # Apply Debug configuration
    target_configure_debug(${TARGET_NAME})

    # Apply Release configuration
    target_configure_release(${TARGET_NAME})

    # Enable LTO if requested by the target AND the global option is enabled
    if(CONFIG_ENABLE_LTO AND ENGINE_ENABLE_LTO)
        target_enable_lto(${TARGET_NAME})
    endif()

    # Enable sanitizers if requested by the target AND the global option is enabled
    if(CONFIG_ENABLE_SANITIZERS AND ENGINE_ENABLE_SANITIZERS)
        if(CONFIG_SANITIZERS)
            # Convert explicitly provided list to function arguments
            set(SANITIZER_ARGS "")
            foreach(sanitizer ${CONFIG_SANITIZERS})
                string(TOUPPER "${sanitizer}" sanitizer_upper)
                list(APPEND SANITIZER_ARGS "${sanitizer_upper}")
            endforeach()
            target_enable_sanitizers(${TARGET_NAME} ${SANITIZER_ARGS})
        else()
            # Build sanitizer list from ENGINE_SANITIZER_* global options
            set(SANITIZER_ARGS "")
            if(ENGINE_SANITIZER_ADDRESS)
                list(APPEND SANITIZER_ARGS "ADDRESS")
            endif()
            if(ENGINE_SANITIZER_LEAK)
                list(APPEND SANITIZER_ARGS "LEAK")
            endif()
            if(ENGINE_SANITIZER_UNDEFINED)
                list(APPEND SANITIZER_ARGS "UNDEFINED")
            endif()
            if(ENGINE_SANITIZER_THREAD)
                list(APPEND SANITIZER_ARGS "THREAD")
            endif()
            if(ENGINE_SANITIZER_MEMORY)
                list(APPEND SANITIZER_ARGS "MEMORY")
            endif()
            if(SANITIZER_ARGS)
                target_enable_sanitizers(${TARGET_NAME} ${SANITIZER_ARGS})
            else()
                message(STATUS "No sanitizers enabled for ${TARGET_NAME}")
            endif()
        endif()
    endif()
endfunction()

# ==============================================================================
# Global Build Configuration Options
# ==============================================================================
option(ENGINE_ENABLE_LTO "Enable Link-Time Optimization for Release builds" ON)
option(ENGINE_ENABLE_SANITIZERS "Enable sanitizers for Debug builds" ON)
option(ENGINE_SANITIZER_ADDRESS "Enable AddressSanitizer" ON)
option(ENGINE_SANITIZER_LEAK "Enable LeakSanitizer" ON)
option(ENGINE_SANITIZER_UNDEFINED "Enable UndefinedBehaviorSanitizer" ON)
option(ENGINE_SANITIZER_THREAD "Enable ThreadSanitizer (cannot be combined with ASan/LSan)" OFF)
option(ENGINE_SANITIZER_MEMORY "Enable MemorySanitizer (Clang only, cannot be combined with ASan/TSan)" OFF)

# Release optimization options
option(ENGINE_OPTIMIZE_NATIVE "Optimize for current CPU (not suitable for distribution)" OFF)
option(ENGINE_FAST_MATH "Enable fast-math (breaks IEEE 754 compliance - use with caution)" OFF)

# ==============================================================================
# Print Configuration Summary
# ==============================================================================
function(print_build_configuration)
    message(STATUS "")
    message(STATUS "========================================")
    message(STATUS " Build Configuration")
    message(STATUS "========================================")
    message(STATUS " Build Type: ${CMAKE_BUILD_TYPE}")
    message(STATUS " LTO Enabled: ${ENGINE_ENABLE_LTO}")
    message(STATUS " Native Optimization: ${ENGINE_OPTIMIZE_NATIVE}")
    message(STATUS " Fast Math: ${ENGINE_FAST_MATH}")
    message(STATUS " Sanitizers Enabled: ${ENGINE_ENABLE_SANITIZERS}")
    if(ENGINE_ENABLE_SANITIZERS)
        message(STATUS "   - AddressSanitizer: ${ENGINE_SANITIZER_ADDRESS}")
        message(STATUS "   - LeakSanitizer: ${ENGINE_SANITIZER_LEAK}")
        message(STATUS "   - UndefinedBehaviorSanitizer: ${ENGINE_SANITIZER_UNDEFINED}")
        message(STATUS "   - ThreadSanitizer: ${ENGINE_SANITIZER_THREAD}")
        message(STATUS "   - MemorySanitizer: ${ENGINE_SANITIZER_MEMORY}")
    endif()
    message(STATUS "========================================")
    message(STATUS "")
endfunction()
