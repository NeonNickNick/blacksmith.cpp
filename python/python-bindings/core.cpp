#include "battle-platform/standard-platform.hpp"
#include "domain/models.hpp"
#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> // NOLINT

namespace blacksmith::python_bindings {
using namespace blacksmith::domain;
using namespace blacksmith::battle_platform;
PYBIND11_MODULE(core, m) {
    pybind11::class_<community>(m, "Community").def(pybind11::init());
    pybind11::class_<skill_context>(m, "SkillContext").def(pybind11::init());
    pybind11::class_<standard_pvp>(m, "StandardPVP")
        .def(pybind11::init<bool>())
        .def("player", &standard_pvp::player,
             pybind11::return_value_policy::reference_internal)
        .def("enemy", &standard_pvp::enemy,
             pybind11::return_value_policy::reference_internal)
        .def("round", &standard_pvp::round)
        .def("to_context", &standard_pvp::to_context)
        .def("submit_player_context", &standard_pvp::submit_player_context)
        .def("submit_enemy_context", &standard_pvp::submit_enemy_context);
}
} // namespace blacksmith::python_bindings
