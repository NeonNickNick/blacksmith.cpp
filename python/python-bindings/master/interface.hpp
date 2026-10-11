#pragma once

#include <cstdint>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

namespace blacksmith::python_bindings::master {
namespace py = pybind11;
using float_array = py::array_t<float>;
using int_array = py::array_t<std::int32_t, py::array::c_style>;
} // namespace blacksmith::python_bindings::master
