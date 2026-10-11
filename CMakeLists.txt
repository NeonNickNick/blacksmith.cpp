cmake_minimum_required(VERSION 3.21)

project(blacksmith-cpp
    VERSION 1.0.0
    DESCRIPTION "C++20 mirror of Blacksmith"
    LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

if(MSVC)
    add_compile_options(/EHsc)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <algorithm>
    #include <array>
    #include <span>
    template<float Value> struct constant {};
    int main() {
        std::array<int, 2> values{2, 1};
        std::span view{values};
        std::ranges::sort(view);
        constant<1.0F> value;
        (void)value;
        return values[0] - 1;
    }
" BLACKSMITH_CXX20_SUPPORTED)
if(NOT BLACKSMITH_CXX20_SUPPORTED)
    message(FATAL_ERROR
        "The compiler must support C++20 ranges, span and floating-point template arguments. "
        "Select a compatible compiler with CXX or CMAKE_CXX_COMPILER.")
endif()

include(cmake/compiler-warnings.cmake)
include(cmake/clang-tidy.cmake)

include(CTest)

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(BLACKSMITH_BUILD_BINDINGS "Build the Python bindings" ON)
if(BLACKSMITH_BUILD_BINDINGS)
    add_subdirectory(python/python-bindings)
endif()
