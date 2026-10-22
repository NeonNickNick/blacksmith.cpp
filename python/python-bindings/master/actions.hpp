#pragma once

#include "domain/models.hpp"
#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace blacksmith::python_bindings::master {
using namespace blacksmith::domain;
inline constexpr int MAX_SKILL_PARAM = 5;

struct action {
    skill skill_;
    int param_;
    skill next_skill_ = skill::SIZE;
    int next_param_ = 0;

    [[nodiscard]] skill_action to_skill_action() const {
        skill_action result{.skill_ = skill_, .param_ = param_};
        if (next_skill_ != skill::SIZE)
            result.next_ = std::make_unique<skill_action>(
                skill_action{.skill_ = next_skill_, .param_ = next_param_});
        return result;
    }
};
inline constexpr int parameter_begin(skill move) {
    return move == skill::MAGIC_ATTACK || move == skill::MAGIC_SHIELD ||
                   move == skill::SPACE_ATTACK ||
                   move == skill::SPACE_BARRIER || move == skill::EXPLOSION ||
                   move == skill::ICE_BLADE || move == skill::ARROW_RAIN ||
                   move == skill::BLOCK
               ? 1
               : 0;
}
inline constexpr int parameter_end(skill move) {
    if (move == skill::BLOCK)
        return 4;
    return move == skill::SHIELD || move == skill::THORN_SHIELD ||
                   move == skill::RECOVERY || move == skill::MAKE_BOLTS ||
                   parameter_begin(move) == 1
               ? MAX_SKILL_PARAM
               : 0;
}
inline constexpr int BASE_ACTION_COUNT = [] {
    int count = 0;
    for (int i = 0; i < static_cast<int>(skill::SIZE); ++i) {
        const auto move = static_cast<skill>(i);
        if (move != skill::ASSOCIATION)
            count += parameter_end(move) - parameter_begin(move) + 1;
    }
    return count;
}();
// Association cannot copy itself, so every legal chain has exactly two nodes.
inline constexpr int SKILL_NUM = 2 * BASE_ACTION_COUNT;
inline constexpr auto skill_table = [] {
    std::array<action, SKILL_NUM> result{};
    size_t index = 0;
    for (int i = 0; i < static_cast<int>(skill::SIZE); ++i) {
        const auto move = static_cast<skill>(i);
        if (move == skill::ASSOCIATION)
            continue;
        for (int param = parameter_begin(move); param <= parameter_end(move);
             ++param)
            result[index++] = {move, param};
    }
    for (int i = 0; i < BASE_ACTION_COUNT; ++i) {
        const auto &next = result[static_cast<size_t>(i)];
        result[index++] = {skill::ASSOCIATION, 0, next.skill_, next.param_};
    }
    return result;
}();
inline int action_index(skill move, int parameter = 0,
                        skill next = skill::SIZE, int next_parameter = 0) {
    for (size_t i = 0; i < skill_table.size(); ++i) {
        const auto &candidate = skill_table[i];
        if (candidate.skill_ == move && candidate.param_ == parameter &&
            candidate.next_skill_ == next && candidate.next_param_ == next_parameter)
            return static_cast<int>(i);
    }
    throw std::invalid_argument("Action is outside the master action table");
}
inline int action_index(const skill_action &action) {
    if (action.next_ && action.next_->next_)
        throw std::invalid_argument("Master does not support this action chain");
    return action_index(action.skill_, action.param_,
                        action.next_ ? action.next_->skill_ : skill::SIZE,
                        action.next_ ? action.next_->param_ : 0);
}
} // namespace blacksmith::python_bindings::master
