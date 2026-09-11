# ---- 源文件（手动列出，新增/删除文件时需同步更新）----
add_library(blacksmith-core STATIC
    src/domain/models.cpp
    src/skill-system/common.cpp

    # ---- 头文件（IDE 可见 & 依赖追踪）----
    include/domain/models.hpp
    include/skill-system/professions.hpp
    include/skill-system/dsl-macro.hpp
    include/domain/transformations.hpp
)

target_include_directories(blacksmith-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_project_warnings(blacksmith-core)
