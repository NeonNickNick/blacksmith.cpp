#include "battle-platform/standard-platform.hpp"
#include <iostream>

int main() {
    blacksmith::battle_platform::standard_pvp platform{};
    std::cout << "Game start." << '\n';

    while (true) {
        platform.submit_player_context(
            platform.collect_context(platform.player()));
        platform.submit_enemy_context(
            platform.collect_context(platform.enemy()));
    }
    return 0;
}
