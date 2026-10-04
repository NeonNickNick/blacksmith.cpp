set(_blacksmith_python_venv "${CMAKE_CURRENT_LIST_DIR}/../blacksmith/.venv")
if(WIN32)
    set(_blacksmith_python_executable "${_blacksmith_python_venv}/Scripts/python.exe")
else()
    set(_blacksmith_python_executable "${_blacksmith_python_venv}/bin/python")
endif()

if(NOT EXISTS "${_blacksmith_python_executable}")
    message(FATAL_ERROR
        "Python bindings require the project virtual environment at ${_blacksmith_python_venv}. "
        "Create it in the python directory before configuring with BLACKSMITH_BUILD_BINDINGS=ON.")
endif()

set(Python3_ROOT_DIR "${_blacksmith_python_venv}")
set(Python3_EXECUTABLE "${_blacksmith_python_executable}" CACHE FILEPATH
    "Python interpreter from the project-local python/.venv" FORCE)
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

execute_process(
    COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
    OUTPUT_VARIABLE pybind11_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(core
    core.cpp
    core.hpp)

target_link_libraries(core PRIVATE blacksmith-core)

target_compile_features(core PRIVATE cxx_std_20)
set_project_warnings(core)
set_target_properties(core PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/python/blacksmith"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/python/blacksmith"
)