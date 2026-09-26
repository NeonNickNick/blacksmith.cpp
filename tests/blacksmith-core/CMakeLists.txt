add_executable(test-blacksmith-zero test-blacksmith-zero.cpp)
target_link_libraries(test-blacksmith-zero PRIVATE blacksmith-core)
set_project_warnings(test-blacksmith-zero)
add_test(NAME blacksmith-zero COMMAND test-blacksmith-zero)
