function(add_blacksmith_test test_name)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE blacksmith-core)
    set_project_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_property(TARGET ${test_name} PROPERTY LABELS unit)
endfunction()


add_blacksmith_test(test-master-schema)
add_blacksmith_test(test-association)
add_blacksmith_test(test-professions)
add_blacksmith_test(test-profession-interactions)
add_blacksmith_test(test-community)
target_include_directories(test-community PRIVATE
    "${CMAKE_SOURCE_DIR}/python/python-bindings/master"
)
target_include_directories(test-master-schema PRIVATE
    "${CMAKE_SOURCE_DIR}/python/python-bindings/master"
)
# Manual terminal client; it requires interactive input and is not a CTest case.
add_executable(pvp "${CMAKE_SOURCE_DIR}/src/blacksmith-client/src/main.cpp")
target_link_libraries(pvp PRIVATE blacksmith-core)
set_project_warnings(pvp)
