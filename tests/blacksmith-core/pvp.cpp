#include "battle-platform/standard-platform.hpp"
int main() {
    blacksmith::battle_platform::standard_pvp platform{true};
    while (true) {

        platform.submit_player_context(
            platform.collect_context(platform.player()));
        platform.submit_enemy_context(
            platform.collect_context(platform.enemy()));
    }
}