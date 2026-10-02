if(MSVC)
    add_compile_options(/EHsc)

    add_compile_options(/Zi)        
    add_link_options(/DEBUG)          
    add_link_options(/OPT:REF /OPT:ICF)  

    add_compile_options($<$<CONFIG:Debug>:/Od>) 

else()
    add_compile_options(-fexceptions)
    add_compile_options(-g)          
    add_compile_options($<$<CONFIG:Debug>:-O0>)
endif()

add_subdirectory(blacksmith-core)