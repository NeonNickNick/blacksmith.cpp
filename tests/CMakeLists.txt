option(BUILD_TESTS "Build the test suite" OFF)

if(BUILD_TESTS)
    add_subdirectory(blacksmith-core)
endif()
