# Shared compiler warning settings for all targets.
#
# Usage: in each sub-project CMakeLists.txt, call:
#   set_project_warnings(<target_name>)

function(set_project_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
