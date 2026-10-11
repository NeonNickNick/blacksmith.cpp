#pragma once

#include "domain/models.hpp"
#include <array>
#include <cstddef>
#include <stdexcept>

namespace blacksmith::python_bindings::master {
using namespace blacksmith::domain;
inline constexpr int MAX_SKILL_PARAM = 5;

struct action {
    skill skill_;
    int param_;
};
inline constexpr int parameter_begin(skill move) {
    return move == skill::MAGIC_ATTACK || move == skill::MAGIC_SHIELD ||
                   move == skill::SPACE_ATTACK || move == skill::SPACE_BARRIER
               ? 1
               : 0;
}
inline constexpr int parameter_end(skill move) {
    return move == skill::SHIELD || move == skill::THORN_SHIELD ||
                   move == skill::RECOVERY || parameter_begin(move) == 1
               ? MAX_SKILL_PARAM
               : 0;
}
inline constexpr int SKILL_NUM = [] {
    int count = 0;
    for (int i = 0; i < static_cast<int>(skill::SIZE); ++i) {
        const auto move = static_cast<skill>(i);
        count += parameter_end(move) - parameter_begin(move) + 1;
    }
    return count;
}();
inline constexpr auto skill_table = [] {
    std::array<action, SKILL_NUM> result{};
    size_t index = 0;
    for (int i = 0; i < static_cast<int>(skill::SIZE); ++i) {
        const auto move = static_cast<skill>(i);
        for (int param = parameter_begin(move); param <= parameter_end(move);
             ++param)
            result[index++] = {move, param};
    }
    return result;
}();
inline int action_index(skill move, int parameter = 0) {
    for (size_t i = 0; i < skill_table.size(); ++i) {
        if (skill_table[i].skill_ == move && skill_table[i].param_ == parameter)
            return static_cast<int>(i);
    }
    throw std::invalid_argument("Action is outside the master action table");
}
} // namespace blacksmith::python_bindings::master
