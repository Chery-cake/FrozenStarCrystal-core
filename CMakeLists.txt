# ==============================================================================
# FrozenStarCrystal-core - Root CMakeLists.txt
# ==============================================================================
cmake_minimum_required(VERSION 4.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FSCProject)


project(FrozenStarCrystal-core
    VERSION 0.1.0
    LANGUAGES CXX C
)

fsc_initialize_project()

# ==============================================================================
# Dependencies (CPM)
# ==============================================================================
# CPMAddPackage(...)

# ==============================================================================
# Build the module target
# ==============================================================================
add_module(
    PUBLIC_DEPS
        # fmt::fmt
        # spdlog::spdlog
)

# ==============================================================================
# Tests
# ==============================================================================
option(ENABLE_TESTS "Build tests" OFF)
option(FSC_TESTS    "Force enable tests" OFF)

if((ENABLE_TESTS AND PROJECT_IS_TOP_LEVEL) OR FSC_TESTS)
    add_subdirectory(tests)
endif()

# ==============================================================================
# Top-level finishing touches (no-op when consumed as a dependency)
# ==============================================================================
fsc_finalize_top_level()

