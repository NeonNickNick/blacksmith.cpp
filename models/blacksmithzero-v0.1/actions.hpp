#pragma once
#include <domain/models.hpp>
#include <vector>
namespace zero {
using namespace blacksmith::domain;
constexpr int skill_count = static_cast<int>(skill::SIZE);
constexpr int parameter_limit = 16;
constexpr int delay_bins = 6;
constexpr int feature_count = 718;
constexpr int action_count = 156;
struct Action { skill move; int parameter; };
inline bool has_parameter(skill move) {
    return move == skill::SHIELD || move == skill::THORN_SHIELD || move == skill::RECOVERY ||
           move == skill::MAGIC_ATTACK || move == skill::MAGIC_SHIELD ||
           move == skill::SPACE_ATTACK || move == skill::SPACE_BARRIER;
}
inline const std::vector<Action> actions = [] {
    std::vector<Action> result;
    for (int id = 0; id < skill_count; ++id) {
        const auto move = static_cast<skill>(id);
        for (int n = 0; n <= (has_parameter(move) ? parameter_limit : 0); ++n)
            result.push_back({move, n});
    }
    return result;
}();
}
