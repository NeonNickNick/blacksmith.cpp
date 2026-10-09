#include "game.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace zero;
    require(actions.size() == action_count, "Action schema changed");
    State state;
    auto view = observe(state, 0);
    require(view.size() == 718, "Observation size changed");
    require(view[2] == 1 && view[3] == 1, "Health feature mismatch");
    require(view[360] == 1 && view[361] == 1, "Opponent offset mismatch");
    state.players[0].focus_.mark_.marks_.push_back({{}, mark_id::TRIPLE_STAB});
    view = observe(state, 0);
    // 2 global + 9 resources/status + 44 skills + 45 previous-skill slots.
    require(view[100 + static_cast<int>(mark_id::TRIPLE_STAB) * 6] == .5F,
            "Lancer mark encoding mismatch");
    const int callback_offset = 2 + 9 + 44 + 45 + 60 + 54 + 54 + 42;
    state.players[0].focus_.turn_context_.callback_context_.datas_.push_back(
        {{}, callback_stage::BEFORE_APPLY_RESOURCE, nullptr});
    view = observe(state, 0);
    require(view[callback_offset + static_cast<int>(callback_stage::BEFORE_APPLY_RESOURCE)*6] == .5F,
            "Callback encoding mismatch");
    require(std::isfinite(view.back()), "Nonfinite observation");
    std::cout << "C++ action and feature schema passed\n";
}
