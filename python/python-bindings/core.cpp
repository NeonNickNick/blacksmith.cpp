#include "core.hpp"
#include "battle-platform/standard-platform.hpp"
#include "blacksmith-master/blacksmith-zero.hpp"
#include "domain/models.hpp"
#include <pybind11/detail/common.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h> //NOLINT
using namespace blacksmith::blacksmith_master;
namespace blacksmith::python_bindings {
using zero_test = standard_test<blacksmith_zero, blacksmith_zero_param>;
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

    pybind11::class_<blacksmith_zero_param>(m, "BlacksmithZeroParam")
        .def(pybind11::init())
        .def("to_list", &blacksmith_zero_param::to_vector)
        .def("from_list", &blacksmith_zero_param::from_vector)
        .def("optimize_mode",
             [](blacksmith_zero_param &self) { self.mcts_iterations_ = 400; })
        .def("operate_mode",
             [](blacksmith_zero_param &self) { self.mcts_iterations_ = 4000; });
    pybind11::class_<blacksmith_zero>(m, "BlacksmithZero")
        .def(pybind11::init())
        .def_readwrite("param", &blacksmith_zero::param_)
        .def("choose_enemy_skill", &blacksmith_zero::choose_enemy_skill);

    pybind11::class_<zero_test>(m, "ZeroTest")
        .def(pybind11::init())
        .def("set_baseline_param", &zero_test::set_baseline_param)
        .def("set_test_param", &zero_test::set_test_param)
        .def("win_rate", &zero_test::win_rate);
}
} // namespace blacksmith::python_bindings