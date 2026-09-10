cmake_minimum_required(VERSION 3.21)

project(blacksmith-cpp
    VERSION 1.0.0
    DESCRIPTION "C++20 mirror of HighPerformanceBlacksmith"
    LANGUAGES CXX
)

# ---- C++20 ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Export compile commands for clangd / tools ----
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Naming convention (documented here, enforced by convention) ----
# File names:   lowercase-with-hyphens (kebab-case)
#   clap-round-clock.hpp, analyzer-registry.cpp
#
# Code names:   snake_case (STL style)
#   namespace:  blacksmith-core::domain::clock
#   class:      clap_round_clock
#   function:   round_pass()
#   variable:   remaining_rounds
#   member:     remaining_rounds_ (trailing underscore)
#   constant:   k_default_delay
#   macro:      BLACKSMITH_CORE_API

# ---- Global compile options ----
if(MSVC)
    add_compile_options(/utf-8)
endif()
include(cmake/compiler-warnings.cmake)

# ---- Build-time include checking (clang-tidy misc-include-cleaner) ----
# Fails the build when a source file uses a symbol without directly
# including the header that provides it.  Controlled by -DENABLE_CLANG_TIDY=ON/OFF
include(cmake/clang-tidy.cmake)

# ---- Sub-projects ----
add_subdirectory(src)
add_subdirectory(tests)
