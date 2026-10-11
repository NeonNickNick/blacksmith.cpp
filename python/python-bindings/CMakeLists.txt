set(_blacksmith_python_venv "${CMAKE_CURRENT_LIST_DIR}/../blacksmith/.venv")
set(PYBIND11_FINDPYTHON ON)
if(WIN32)
    set(_blacksmith_python_executable "${_blacksmith_python_venv}/Scripts/python.exe")
else()
    set(_blacksmith_python_executable "${_blacksmith_python_venv}/bin/python")
endif()

if(NOT Python3_EXECUTABLE AND EXISTS "${_blacksmith_python_executable}")
    set(Python3_EXECUTABLE "${_blacksmith_python_executable}" CACHE FILEPATH
        "Python interpreter for the bindings")
endif()
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _pybind11_result
)
if(NOT _pybind11_result EQUAL 0)
    message(FATAL_ERROR "Install pybind11 in ${Python3_EXECUTABLE} before configuring bindings")
endif()
find_package(pybind11 CONFIG REQUIRED)

function(blacksmith_add_python_module target_name)
    set(options "")
    set(one_value_args "")
    set(multi_value_args "LIBRARIES")
    cmake_parse_arguments(BPM
        "${options}"
        "${one_value_args}"
        "${multi_value_args}"
        ${ARGN}
    )

    set(_sources ${BPM_UNPARSED_ARGUMENTS})

    if(NOT _sources)
        message(FATAL_ERROR
            "blacksmith_add_python_module(${target_name}) requires at least one source file.")
    endif()

    pybind11_add_module(${target_name} ${_sources})

    if(BPM_LIBRARIES)
        target_link_libraries(${target_name} PRIVATE ${BPM_LIBRARIES})
    endif()

    target_compile_features(${target_name} PRIVATE cxx_std_20)
    set_project_warnings(${target_name})

    set_target_properties(${target_name} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_SOURCE_DIR}/python/blacksmith>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_SOURCE_DIR}/python/blacksmith>"
    )
endfunction()

blacksmith_add_python_module(core
    core.cpp
    LIBRARIES blacksmith-core
)

blacksmith_add_python_module(master
    master/interface.cpp
    master/interface.hpp
    LIBRARIES blacksmith-core
)
option(BLACKSMITH_USE_OPENMP "Parallelize batched master simulation" ON)
if(BLACKSMITH_USE_OPENMP)
    find_package(OpenMP QUIET COMPONENTS CXX)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(master PRIVATE OpenMP::OpenMP_CXX)
    endif()
endif()
