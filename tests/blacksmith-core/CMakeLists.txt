function(add_blacksmith_test test_name)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE blacksmith-core)
    set_project_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_property(TARGET ${test_name} PROPERTY LABELS unit)
endfunction()


add_blacksmith_test(test-blacksmith-zero)
add_blacksmith_test(test-benchmark-test)
