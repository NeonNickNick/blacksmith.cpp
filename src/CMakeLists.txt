# ... 你原有的代码 ...
if(MSVC)
    # MSVC 默认就启用异常，显式确保开启
    add_compile_options(/EHsc)
    
    # ========== 新增部分 ==========
    # 如果是 Debug 配置，强制生成完整调试信息并关闭优化
    # $<CONFIG:Debug> 是 CMake 的生成器表达式，只在 Debug 模式下生效
    add_compile_options($<$<CONFIG:Debug>:/Zi>)      # 生成调试信息
    add_compile_options($<$<CONFIG:Debug>:/Od>)      # 关闭优化
    add_link_options($<$<CONFIG:Debug>:/DEBUG>)      # 链接调试信息
    # ==============================
else()
    # GCC / Clang
    add_compile_options(-fexceptions)
    add_compile_options($<$<CONFIG:Debug>:-g>)       # GCC/Clang 生成调试信息
    add_compile_options($<$<CONFIG:Debug>:-O0>)      # GCC/Clang 关闭优化
endif()

# Sub-projects under src/
add_subdirectory(blacksmith-core)
add_subdirectory(blacksmith-client)