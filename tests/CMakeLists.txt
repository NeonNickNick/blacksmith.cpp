if(MSVC)
    add_compile_options(/EHsc)
    
    add_compile_options($<$<CONFIG:Debug>:/Zi>)     
    add_compile_options($<$<CONFIG:Debug>:/Od>)      
    add_link_options($<$<CONFIG:Debug>:/DEBUG>)    

else()
    add_compile_options(-fexceptions)
    add_compile_options($<$<CONFIG:Debug>:-g>)   
    add_compile_options($<$<CONFIG:Debug>:-O0>)     
endif()

add_subdirectory(blacksmith-core)
