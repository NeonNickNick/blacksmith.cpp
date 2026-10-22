#include "actions.hpp"
#include "battle-platform/standard-platform.hpp"
#include "domain/models.hpp"
#include "state.hpp"
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace blacksmith::domain;
using namespace blacksmith::python_bindings::master;

static_assert(!std::is_default_constructible_v<community>);
static_assert(std::is_nothrow_move_constructible_v<state>);

namespace {
void expect_opponents(const community &player, const community &enemy) {
    if (player.opponent_ != &enemy || enemy.opponent_ != &player)
        throw std::runtime_error("Community points outside its battle");
}

void test_opponent_links() {
    bool rejected_null = false;
    try {
        community invalid(nullptr);
    } catch (const std::invalid_argument &) {
        rejected_null = true;
    }
    if (!rejected_null)
        throw std::runtime_error("Community accepted a null opponent");

    blacksmith::battle_platform::standard_pvp platform(false);
    expect_opponents(platform.player(), platform.enemy());
    platform.reset();
    expect_opponents(platform.player(), platform.enemy());

    state original;
    const int iron = action_index(skill::IRON);
    original.step(iron, iron);
    auto copied = original;
    expect_opponents(original.players_[0], original.players_[1]);
    expect_opponents(copied.players_[0], copied.players_[1]);
    auto moved = std::move(copied);
    expect_opponents(moved.players_[0], moved.players_[1]);
    expect_opponents(copied.players_[0], copied.players_[1]);

    state assigned;
    {
        auto temporary = original;
        assigned = temporary;
    }
    expect_opponents(assigned.players_[0], assigned.players_[1]);
    {
        auto temporary = original;
        assigned = std::move(temporary);
        expect_opponents(temporary.players_[0], temporary.players_[1]);
    }
    expect_opponents(assigned.players_[0], assigned.players_[1]);
    assigned.step(iron, iron);
    if (original.round_ != 2 || assigned.round_ != 3)
        throw std::runtime_error("State assignment changed the source battle");

    // Replacing one player (as DreamWake does) retains its battle's opponent.
    assigned.players_[0] = original.players_[0];
    expect_opponents(assigned.players_[0], assigned.players_[1]);
    {
        auto temporary = original;
        assigned.players_[0] = std::move(temporary.players_[0]);
    }
    expect_opponents(assigned.players_[0], assigned.players_[1]);

    std::vector<state> games;
    games.reserve(1);
    games.push_back(original);
    games.push_back(std::move(moved));
    std::swap(games[0], games[1]);
    for (const auto &game : games)
        expect_opponents(game.players_[0], game.players_[1]);
}

} // namespace

int main() { test_opponent_links(); }
