#include "interface.hpp"
#include "actions.hpp"
#include "arena.hpp"
#include "domain/models.hpp"
#include "encoding.hpp"
#include "skill-system/skill-sets.hpp"
#include "state.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <pybind11/attr.h>
#include <pybind11/cast.h>
#include <pybind11/detail/common.h>
#include <pybind11/gil.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h> // NOLINT
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace blacksmith::python_bindings::master {
namespace {
void write_observation(state &game, float *observations, float *masks) {
    for (int seat = 0; seat < 2; ++seat) {
        const auto values = encode(game, seat);
        std::ranges::copy(values, observations + seat * OBSERVATION_LENGTH);
        if (masks) {
            for (int i = 0; i < SKILL_NUM; ++i)
                masks[seat * SKILL_NUM + i] = game.legal(seat, i) ? 1.0F : 0.0F;
        }
    }
}
std::tuple<float_array, float_array> observations(arena &env) {
    const auto count = static_cast<py::ssize_t>(env.games_.size());
    float_array values(
        {count, py::ssize_t{2}, static_cast<py::ssize_t>(OBSERVATION_LENGTH)});
    float_array masks(
        {count, py::ssize_t{2}, static_cast<py::ssize_t>(SKILL_NUM)});
    auto *out = values.mutable_data();
    auto *mask = masks.mutable_data();
    {
        py::gil_scoped_release release;
#ifdef _OPENMP
#pragma omp parallel for num_threads(env.threads_) schedule(static)
#endif
        for (py::ssize_t i = 0; i < count; ++i)
            write_observation(env.games_[static_cast<size_t>(i)],
                              out + i * 2 * OBSERVATION_LENGTH,
                              mask + i * 2 * SKILL_NUM);
    }
    return {values, masks};
}
float_array step(arena &env, const int_array &actions, bool reset_finished) {
    if (actions.ndim() != 2 ||
        actions.shape(0) != static_cast<py::ssize_t>(env.games_.size()) ||
        actions.shape(1) != 2)
        throw std::invalid_argument("Actions must have shape (envs, 2)");
    const auto *moves = actions.data();
    // Validate the whole batch before mutation.
    for (size_t i = 0; i < env.games_.size(); ++i) {
        auto &game = env.games_[i];
        if (game.outcome() || !game.legal(0, moves[2 * i]) ||
            !game.legal(1, moves[2 * i + 1]))
            throw std::invalid_argument("Illegal joint action");
    }
    float_array results(
        {static_cast<py::ssize_t>(env.games_.size()), py::ssize_t{6}});
    auto *out = results.mutable_data();
    {
        py::gil_scoped_release release;
#ifdef _OPENMP
#pragma omp parallel for num_threads(env.threads_) schedule(static)
#endif
        for (int i = 0; i < static_cast<int>(env.games_.size()); ++i) {
            auto &game = env.games_[static_cast<size_t>(i)];
            game.step(moves[2 * i], moves[2 * i + 1]);
            out[6 * i] = static_cast<float>(game.outcome());
            out[6 * i + 1] = static_cast<float>(game.round_ - 1);
            out[6 * i + 2] =
                static_cast<float>(game.players_[0].focus_.health_.hp_);
            out[6 * i + 3] =
                static_cast<float>(game.players_[1].focus_.health_.hp_);
            out[6 * i + 4] = static_cast<float>(role(game.players_[0]));
            out[6 * i + 5] = static_cast<float>(role(game.players_[1]));
            if (game.outcome() && reset_finished)
                game = state{};
        }
    }
    return results;
}
std::tuple<float_array, float_array, float_array>
expand(search_tree &tree, const int_array &candidates, bool retain) {
    if (candidates.ndim() != 3 ||
        candidates.shape(0) != static_cast<py::ssize_t>(tree.parents_.size()) ||
        candidates.shape(1) != 2 || candidates.shape(2) <= 0)
        throw std::invalid_argument(
            "Candidates must have shape (parents, 2, width)");
    const auto width = candidates.shape(2);
    const auto cells = candidates.shape(0) * width * width;
    const auto *choices = candidates.data();
    for (size_t parent = 0; parent < tree.parents_.size(); ++parent) {
        if (!tree.active_[parent] || tree.parents_[parent].outcome())
            continue;
        for (int seat = 0; seat < 2; ++seat) {
            for (py::ssize_t j = 0; j < width; ++j) {
                const auto index =
                    choices[(parent * 2 + static_cast<size_t>(seat)) *
                                static_cast<size_t>(width) +
                            static_cast<size_t>(j)];
                if (index < -1 ||
                    (index >= 0 && !tree.parents_[parent].legal(seat, index)))
                    throw std::invalid_argument("Illegal search candidate");
            }
        }
    }
    float_array values(
        {cells, py::ssize_t{2}, static_cast<py::ssize_t>(OBSERVATION_LENGTH)});
    float_array masks(
        {cells, py::ssize_t{2}, static_cast<py::ssize_t>(SKILL_NUM)});
    float_array outcomes({candidates.shape(0), width, width});
    auto *out = values.mutable_data();
    auto *mask = masks.mutable_data();
    auto *result = outcomes.mutable_data();
    std::fill(out, out + values.size(), 0.0F);
    std::fill(mask, mask + masks.size(), 0.0F);
    std::fill(result, result + cells, -1.0F);
    std::vector<state> children(retain ? static_cast<size_t>(cells) : 0);
    // vector<bool> shares words; workers must write independent bytes.
    std::vector<unsigned char> active(retain ? static_cast<size_t>(cells) : 0,
                                      0);
    {
        py::gil_scoped_release release;
#ifdef _OPENMP
#pragma omp parallel for num_threads(tree.threads_) schedule(dynamic)
#endif
        for (py::ssize_t cell = 0; cell < cells; ++cell) {
            const auto parent = static_cast<size_t>(cell / (width * width));
            const int first =
                choices[parent * 2 * static_cast<size_t>(width) +
                        static_cast<size_t>(cell / width % width)];
            const int second =
                choices[(parent * 2 + 1) * static_cast<size_t>(width) +
                        static_cast<size_t>(cell % width)];
            if (!tree.active_[parent] || tree.parents_[parent].outcome() ||
                first < 0 || second < 0)
                continue;
            auto child = tree.parents_[parent];
            child.step(first, second);
            result[cell] = static_cast<float>(child.outcome());
            if (!child.outcome())
                write_observation(child, out + cell * 2 * OBSERVATION_LENGTH,
                                  retain ? mask + cell * 2 * SKILL_NUM
                                         : nullptr);
            if (retain) {
                children[static_cast<size_t>(cell)] = std::move(child);
                active[static_cast<size_t>(cell)] = 1;
            }
        }
    }
    if (retain) {
        tree.parents_ = std::move(children);
        tree.active_.assign(active.begin(), active.end());
    }
    return {values, masks, outcomes};
}
} // namespace
PYBIND11_MODULE(master, m) {
    m.attr("SCHEMA_VERSION") = SCHEMA_VERSION;
    m.attr("OBSERVATION_LENGTH") = OBSERVATION_LENGTH;
    m.attr("COMMON_ENCODING_LENGTH") = COMMON_ENCODING_LENGTH;
    m.attr("PROFESSION_ENCODING_LENGTH") = PROFESSION_ENCODING_LENGTH;
    m.attr("SKILL_NUM") = SKILL_NUM;
    m.attr("BASE_ACTION_COUNT") = BASE_ACTION_COUNT;
    m.attr("GLOBAL_OFFSET") = GLOBAL_OFFSET;
    m.attr("HISTORY_OFFSET") = HISTORY_OFFSET;
    m.attr("HISTORY_LENGTH") = HISTORY_LENGTH;
    m.attr("HISTORY_TOKEN_LENGTH") = HISTORY_TOKEN_LENGTH;
    m.attr("HISTORY_PLAYER_LENGTH") = HISTORY_PLAYER_LENGTH;
    m.attr("ROUND_LIMIT") = ROUND_LIMIT;
    m.attr("DELAY_BINS") = DELAY_BINS;
    m.attr("AUTHORIZED_OFFSET") = AUTHORIZED_OFFSET;
    m.attr("ATTACK_OFFSET") = ATTACK_OFFSET;
    m.attr("DEFENSE_OFFSET") = DEFENSE_OFFSET;
    m.attr("PENDING_DEFENSE_OFFSET") = PENDING_DEFENSE_OFFSET;
    m.attr("MARK_OFFSET") = MARK_OFFSET;
    m.attr("PENDING_RESOURCE_OFFSET") = PENDING_RESOURCE_OFFSET;
    m.attr("CALLBACK_OFFSET") = CALLBACK_OFFSET;
    m.attr("EFFECT_OFFSET") = EFFECT_OFFSET;
    py::list actions;
    std::vector<std::string> names;
    for (const auto &a : skill_table) {
        py::tuple chain(a.next_skill_ == skill::SIZE ? 1 : 2);
        chain[0] = py::make_tuple(static_cast<int>(a.skill_), a.param_);
        if (chain.size() == 2)
            chain[1] = py::make_tuple(static_cast<int>(a.next_skill_), a.next_param_);
        actions.append(chain);
        names.push_back(blacksmith::skill_system::format_skill_action(a.to_skill_action()));
    }
    m.attr("ACTIONS") = actions;
    m.attr("ACTION_NAMES") = py::cast(names);
    m.attr("ROLES") = py::cast(std::vector<std::string>{
        "none", "warlock", "cannon", "driver", "lancer", "bloodsigil",
        /* "monk", "prophet", */ "holybook", "phantombook", "crossbow"});
    m.def("parse_action", [](const std::string &input) {
        return action_index(blacksmith::skill_system::parse_skill_action(input));
    });
    m.def(
        "action_index",
        [](int move, int param, int next, int next_param) {
            if (move < 0 || move >= static_cast<int>(skill::SIZE) ||
                next < -1 || next >= static_cast<int>(skill::SIZE))
                throw std::invalid_argument("Invalid skill ID");
            return action_index(static_cast<skill>(move), param,
                                next == -1 ? skill::SIZE : static_cast<skill>(next),
                                next_param);
        },
        py::arg("skill"), py::arg("parameter") = 0,
        py::arg("next_skill") = -1, py::arg("next_parameter") = 0);
    py::class_<arena>(m, "Arena")
        .def(py::init<int, int>(), py::arg("count") = 32,
             py::arg("threads") = 6)
        .def_property_readonly(
            "count", [](const arena &env) { return env.games_.size(); })
        .def("observe", &observations)
        .def("step", &step, py::arg("actions"),
             py::arg("reset_finished") = true)
        .def("reset", &arena::reset, py::arg("index"),
             py::arg("prefix") = std::vector<std::array<int, 2>>{},
             py::call_guard<py::gil_scoped_release>())
        .def("reset_play", &arena::reset_play, py::arg("index"),
             py::arg("human_seat"), py::arg("iron"),
             py::call_guard<py::gil_scoped_release>())
        .def(
            "status",
            [](const arena &env, int index) {
                const auto &game = env.games_.at(static_cast<size_t>(index));
                py::dict result;
                result["round"] = game.round_;
                result["outcome"] = game.outcome();
                py::list players;
                for (const auto &player : game.players_) {
                    const auto &body = player.focus_;
                    py::dict info;
                    info["hp"] = body.health_.hp_;
                    info["mhp"] = body.health_.mhp_;
                    info["role"] = role(player);
                    info["iron"] =
                        body.resource_.query_common(resource_type::IRON);
                    info["gold_iron"] =
                        body.resource_.query_gold(resource_type::IRON);
                    info["magic"] =
                        body.resource_.query_common(resource_type::MAGIC);
                    info["space"] =
                        body.resource_.query_common(resource_type::SPACE);
                    info["time"] =
                        body.resource_.query_common(resource_type::TIME);
                    info["cross"] = body.resource_.query(resource_type::CROSS);
                    info["dream"] = body.resource_.query(resource_type::DREAM);
                    info["spirit"] = body.resource_.query(resource_type::SPIRIT);
                    info["fire"] = body.resource_.query(resource_type::FIRE);
                    info["water"] = body.resource_.query(resource_type::WATER);
                    info["wood"] = body.resource_.query(resource_type::WOOD);
                    info["earth"] = body.resource_.query(resource_type::EARTH);
                    info["bolt"] = body.resource_.query(resource_type::BOLT);
                    players.append(info);
                }
                result["players"] = players;
                return result;
            },
            py::arg("index") = 0)
        .def("metadata", [](const arena &env) {
            int_array result(
                {static_cast<py::ssize_t>(env.games_.size()), py::ssize_t{3}});
            auto out = result.mutable_unchecked<2>();
            for (size_t i = 0; i < env.games_.size(); ++i) {
                out(static_cast<py::ssize_t>(i), 0) = env.games_[i].round_;
                out(static_cast<py::ssize_t>(i), 1) =
                    role(env.games_[i].players_[0]);
                out(static_cast<py::ssize_t>(i), 2) =
                    role(env.games_[i].players_[1]);
            }
            return result;
        });
    py::class_<search_tree>(m, "SearchTree")
        .def(py::init<const arena &>())
        .def("subset", &search_tree::subset, py::arg("indices"),
             py::call_guard<py::gil_scoped_release>())
        .def("expand", &expand, py::arg("candidates"),
             py::arg("retain") = true);
}
} // namespace blacksmith::python_bindings::master
