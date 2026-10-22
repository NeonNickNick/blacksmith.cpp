add_library(bdsl compiler/src/bdsl.cpp)
target_include_directories(bdsl PUBLIC compiler/include)
target_compile_features(bdsl PUBLIC cxx_std_20)
add_executable(bdslc compiler/src/main.cpp)
target_link_libraries(bdslc PRIVATE bdsl)
set_property(TARGET bdsl bdslc PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
    target_compile_options(bdsl PRIVATE /W4 /utf-8)
    target_compile_options(bdslc PRIVATE /W4 /utf-8)
else()
    target_compile_options(bdsl PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(bdslc PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS bdslc RUNTIME DESTINATION bin)

# Packaging is part of the root build and uses only CMake's built-in ZIP support.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(BDSL_PLATFORM win32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(BDSL_PLATFORM darwin)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BDSL_PLATFORM linux)
endif()
set(BDSL_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
    set(BDSL_PROCESSOR "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
endif()
if(CMAKE_OSX_ARCHITECTURES)
    set(BDSL_PROCESSOR "${CMAKE_OSX_ARCHITECTURES}")
endif()
string(TOLOWER "${BDSL_PROCESSOR}" BDSL_PROCESSOR)
if(BDSL_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(BDSL_ARCH arm64)
elseif(BDSL_PROCESSOR MATCHES "^(amd64|x86_64|x64)$")
    set(BDSL_ARCH x64)
endif()
set(BDSL_VSIX_TARGET "${BDSL_PLATFORM}-${BDSL_ARCH}" CACHE STRING "VSIX platform and architecture")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vscode/package.json" BDSL_MANIFEST)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS vscode/package.json)
string(JSON BDSL_EXTENSION_NAME GET "${BDSL_MANIFEST}" name)
string(JSON BDSL_VERSION GET "${BDSL_MANIFEST}" version)
add_custom_target(bdsl-vsix
    COMMAND "${CMAKE_COMMAND}"
        "-DBDSL_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DBDSL_COMPILER=$<TARGET_FILE:bdslc>"
        "-DBDSL_STAGE=${CMAKE_CURRENT_BINARY_DIR}/package/$<CONFIG>"
        "-DBDSL_TARGET=${BDSL_VSIX_TARGET}"
        "-DBDSL_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/dist/${BDSL_EXTENSION_NAME}-${BDSL_VERSION}-${BDSL_VSIX_TARGET}.vsix"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/package_extension.cmake"
    DEPENDS bdslc VERBATIM)

if(BUILD_TESTING)
    add_executable(bdsl-compiler-test tests/compiler_test.cpp)
    target_link_libraries(bdsl-compiler-test PRIVATE bdsl)
    set_property(TARGET bdsl-compiler-test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(MSVC)
        target_compile_options(bdsl-compiler-test PRIVATE /W4 /utf-8)
    else()
        target_compile_options(bdsl-compiler-test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME bdsl-compiler COMMAND bdsl-compiler-test)
    set_tests_properties(bdsl-compiler PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME bdsl-cli COMMAND "${CMAKE_COMMAND}"
        "-DBDSLC=$<TARGET_FILE:bdslc>" "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake")
    find_program(BDSL_NODE node)
    if(BDSL_NODE)
        add_test(NAME bdsl-extension COMMAND "${BDSL_NODE}" --test
            "${CMAKE_CURRENT_SOURCE_DIR}/vscode/test/extension.test.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/vscode/test/client.test.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/vscode/test/grammar.test.js")
        set_tests_properties(bdsl-extension PROPERTIES ENVIRONMENT "BDSLC=$<TARGET_FILE:bdslc>")
    endif()
endif()

option(BDSL_TEST_CORE "Compile and execute generated examples against the adjacent core" ON)
if(BUILD_TESTING AND BDSL_TEST_CORE)
    set(CORE "${CMAKE_CURRENT_SOURCE_DIR}/../src/blacksmith-core")
    file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "${CORE}/src/*.cpp")
    list(FILTER CORE_SOURCES EXCLUDE REGEX "/skill-system/(warlock/warlock|cannon)\\.cpp$")
    set(GENERATED)
    foreach(NAME warlock cannon)
        set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/${NAME}.cpp")
        add_custom_command(OUTPUT "${OUTPUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
            COMMAND bdslc "${CMAKE_CURRENT_SOURCE_DIR}/examples/${NAME}.bdsl" -o "${OUTPUT}"
            DEPENDS bdslc "${CMAKE_CURRENT_SOURCE_DIR}/examples/${NAME}.bdsl"
            VERBATIM)
        list(APPEND GENERATED "${OUTPUT}")
    endforeach()
    add_executable(bdsl-core-test tests/core_test.cpp ${GENERATED} ${CORE_SOURCES})
    set(REPEAT_FIXTURES "${CMAKE_CURRENT_BINARY_DIR}/generated/repeat-fixtures.cpp")
    add_custom_command(OUTPUT "${REPEAT_FIXTURES}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND bdslc "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/repeats.bdsl" -o "${REPEAT_FIXTURES}"
        DEPENDS bdslc "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/repeats.bdsl" VERBATIM)
    add_custom_target(bdsl-repeat-fixtures DEPENDS "${REPEAT_FIXTURES}")
    add_dependencies(bdsl-core-test bdsl-repeat-fixtures)
    target_include_directories(bdsl-core-test PRIVATE "${CORE}/include" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_features(bdsl-core-test PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(bdsl-core-test PRIVATE /utf-8 /EHsc)
    endif()
    add_test(NAME bdsl-core COMMAND bdsl-core-test)
    set(FEATURES "${CMAKE_CURRENT_BINARY_DIR}/generated/features.cpp")
    add_custom_command(OUTPUT "${FEATURES}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND bdslc "${CMAKE_CURRENT_SOURCE_DIR}/examples/features.bdsl" -o "${FEATURES}"
        DEPENDS bdslc "${CMAKE_CURRENT_SOURCE_DIR}/examples/features.bdsl" VERBATIM)
    add_library(bdsl-features-test OBJECT "${FEATURES}")
    target_include_directories(bdsl-features-test PRIVATE "${CORE}/include")
    target_compile_features(bdsl-features-test PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(bdsl-features-test PRIVATE /utf-8 /EHsc)
    endif()
endif()
