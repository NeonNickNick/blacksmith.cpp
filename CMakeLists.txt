cmake_minimum_required(VERSION 3.21)

project(blacksmith-cpp
    VERSION 1.0.0
    DESCRIPTION "C++20 mirror of Blacksmith"
    LANGUAGES CXX
)
if(MSVC)
    add_compile_options(/EHsc)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/compiler-warnings.cmake)
include(cmake/clang-tidy.cmake)

include(CTest)

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(BLACKSMITH_BUILD_BINDINGS "Build the Python bindings" ON)
if(BLACKSMITH_BUILD_BINDINGS)
    add_subdirectory(python)
endif()