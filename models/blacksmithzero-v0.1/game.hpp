#pragma once
#include <domain/transformations.hpp>
#include "actions.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace zero {
using namespace blacksmith::domain;
inline skill_context context(community& self, Action action) {
    return {.action_ = {action.move, action.parameter}, .self_ = &self};
}
struct State {
    std::array<community, 2> players{};
    std::array<int, 2> previous{skill_count, skill_count};
    int round = 1;
    State() { initialize(players[0], players[1]); }
    bool legal(int seat, int index) {
        return index >= 0 && index < action_count &&
               check_skill(players[seat], context(players[seat], actions[index])) == check_result::SUCCESS;
    }
    void step(int first, int second) {
        if (!legal(0, first) || !legal(1, second)) throw std::runtime_error("Illegal self-play action");
        auto a = context(players[0], actions[first]);
        auto b = context(players[1], actions[second]);
        previous = {static_cast<int>(actions[first].move), static_cast<int>(actions[second].move)};
        declare(players[0], a, players[1], b);
        judge(players[0], players[1]);
        ++round;
    }
    // 0 = ongoing; 1/2 = first/second winner; 3 = mutual death; 4 = round cap.
    int outcome() const {
        const bool a = players[0].focus_.health_.hp_ <= 0;
        const bool b = players[1].focus_.health_.hp_ <= 0;
        return a && b ? 3 : a ? 2 : b ? 1 : round > 100 ? 4 : 0;
    }
};

// Representation of public engine state, independent of any opponent model.
void append_player(std::vector<float>& out, community& player, int last_move);
inline std::vector<float> observe(State& state, int seat) {
    std::vector<float> result;
    result.reserve(feature_count);
    result.push_back(state.round / 20.0F);
    result.push_back(seat == 0 ? 1.0F : 0.0F);
    append_player(result, state.players[seat], state.previous[seat]);
    append_player(result, state.players[1 - seat], state.previous[1 - seat]);
    if (result.size() != feature_count) throw std::runtime_error("State schema mismatch");
    return result;
}
inline void write_observation(State& state, float* features, float* masks = nullptr) {
    for (int seat = 0; seat < 2; ++seat) {
        const auto values = observe(state, seat);
        std::copy(values.begin(), values.end(), features + seat * feature_count);
        if (masks) for (int action = 0; action < action_count; ++action)
            masks[seat * action_count + action] = state.legal(seat, action) ? 1.0F : 0.0F;
    }
}
}
#include "state-encoder.hpp"
