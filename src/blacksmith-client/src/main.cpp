#include "battle-platform/standard-platform.hpp"
#include <iostream>

int main() {
    blacksmith::battle_platform::standard_pvp platform{};
    std::cout << "Game start." << '\n';

    try {
        while (platform.result() == decltype(platform)::battle_result::BATTLING) {
            platform.submit_player_context(
                platform.collect_context(platform.player()));
            platform.submit_enemy_context(
                platform.collect_context(platform.enemy()));
        }
    } catch (const std::ios_base::failure &) {
        return std::cin.eof() ? 0 : 1;
    }
    return 0;
}
