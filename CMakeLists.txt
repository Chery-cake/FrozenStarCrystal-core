# ==============================================================================
# Modular C++ Engine - Root CMakeLists.txt
# ==============================================================================

cmake_minimum_required(VERSION 4.0)

# Enable experimental import std support – UUID for CMake 4.3.2
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "451f2fe2-a8a2-47c3-bc32-94786d8fc91b"
    CACHE STRING "Enable import std experimental feature" FORCE
)

# Allow older third-party projects to be processed by CMake >= 4.0
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
endif()

enable_testing()

project(FrozenStars
    VERSION 0.1.0
    DESCRIPTION "ECS system, Concurrency modules, Resource Management"
    LANGUAGES CXX C
)

# Enable Objective-C languages on macOS (required by SDL3)
if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

# ==============================================================================
# Enforce Out-of-Source Build
# ==============================================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please use out-of-source builds:\n"
                        "  cmake -B out -S .")
endif()

# ==============================================================================
# Global Settings
# ==============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_MODULE_STD ON)

if(MSVC)
  set(CMAKE_C_STANDARD 17)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
else()
  set(CMAKE_C_STANDARD 23)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable POSIX features TODO check to remove this declaration
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(_GNU_SOURCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Set output directories
set(TARGET_OS ${CMAKE_SYSTEM_NAME})
set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}/${TARGET_OS})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})

# Force color diagnostics
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/diagnostics:color)
    # Force synchronous PDB writes as a safety net.
    add_compile_options(/FS)
    set(CMAKE_PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/pdb)
    # Use embedded debug info (/Z7) instead of separate PDB (/Zi).
    # sccache does not support PDB-based debug info, causing fatal error
    # C1041 when parallel cl.exe processes collide on shared vc*.pdb files.
    # /Z7 embeds debug info directly in .obj files, avoiding contention.
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
    # Force CMP0141=NEW in FetchContent sub-projects that call
    # cmake_minimum_required with versions older than 3.25.  Without this,
    # those sub-projects revert to CMP0141=OLD and CMake injects /Zi
    # via its internal debug-info handling, ignoring the property above.
    set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
endif()

# ==============================================================================
# Custom CMake Modules
# ==============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(BuildConfiguration)
include(CrossCompiling)
include(AddEngineModule)

# ==============================================================================
# Dependencies
# ==============================================================================
#add_subdirectory(lib)

# ==============================================================================
# Engine Subprojects
# ==============================================================================
#add_subdirectory(subprojects)

# ==============================================================================
# Main Application
# ==============================================================================
option(EXECUTABLE "Compiles the project as a executable" OFF)

if(EXECUTABLE)
    set(${PATH} "")
else()
    set(${PATH} "lib/")
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/${PATH}*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/${PATH}*.c*"
)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/${PATH}*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/${PATH}*.h*"
)

file(GLOB_RECURSE PRIVATE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/${PATH}*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/${PATH}*.h*"
)

if(EXECUTABLE)
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${PRIVATE_HEADERS})
else()
    add_library(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${PRIVATE_HEADERS})
endif()

target_sources(${PROJECT_NAME}
    PRIVATE
        ${SOURCES}
        ${PRIVATE_HEADERS}
    PUBLIC
        ${HEADERS}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GENERATED_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE

)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4251>
)

# Add ENGINE_DEBUG definition for Debug builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)

# ==============================================================================
# Apply Build Configuration
# ==============================================================================
target_configure_build(${PROJECT_NAME}
    ENABLE_LTO
    ENABLE_SANITIZERS
)

# Apply platform-specific configuration
target_configure_platform(${PROJECT_NAME})
target_configure_rpath(${PROJECT_NAME})

# ==============================================================================
# Generate .clangd Configuration
# ==============================================================================
include(GenerateClangd)

# ==============================================================================
# Tests
# ==============================================================================
option(ENABLE_TESTS "Add and Compile the project tests" OFF)
if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()

# ==============================================================================
# Installation
# ==============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY ${OUTPUT_DIR}/assets DESTINATION share/engine)

# ==============================================================================
# Debug Output
# ==============================================================================
message(STATUS "")
message(STATUS "========================================")
message(STATUS " Modular C++ Engine v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS " Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS " C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS " C++ Compiler Standard: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS " C Standard: ${CMAKE_C_STANDARD}")
message(STATUS " C Compiler Standard: ${CMAKE_C_COMPILER_ID}")
message(STATUS " Output: ${OUTPUT_DIR}")
message(STATUS " Runtime:  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS " Library: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "========================================")

# Print build configuration summary
print_build_configuration()

# Print cross-compilation info if applicable
print_cross_compilation_info()
