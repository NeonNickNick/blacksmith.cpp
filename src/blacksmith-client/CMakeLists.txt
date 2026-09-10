add_executable(blacksmith-client
    src/main.cpp
)

target_link_libraries(blacksmith-client
    PRIVATE blacksmith-core
)

set_project_warnings(blacksmith-client)
