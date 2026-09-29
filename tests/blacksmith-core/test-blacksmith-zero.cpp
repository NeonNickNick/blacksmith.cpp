#include "battle-platform/standard-platform.hpp"
#include "domain/models.hpp"
#include <blacksmith-master/blacksmith-ai.hpp>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
int main() {
    bool enemy_begin{true};
    bool player_begin{true};
    std::mutex enemy_mutex;
    std::mutex player_mutex;
    std::condition_variable enemy_cv;
    std::condition_variable player_cv;
    blacksmith_core::battle_platform::standard_pvp platform{};

    platform.set_callback({[&]() {
        const auto &action = platform.enemy_action();
        std::cout << "blacksmith-zero: " << action.skill_name_ << " "
                  << action.param_ << '\n';

        {
            std::unique_lock lock(player_mutex);
            player_begin = true;
            player_cv.notify_one();
        }
        {
            std::unique_lock lock(enemy_mutex);
            enemy_begin = true;
            enemy_cv.notify_one();
        }
    }});

    std::jthread t([&]() {
        blacksmith_core::blacksmith_master::blacksmith_zero ai{};
        ai.init();

        while (true) {
            {
                std::unique_lock lock(enemy_mutex);
                enemy_cv.wait(lock, [&]() { return enemy_begin; });
                enemy_begin = false;
            }

            platform.submit_enemy_context(ai.choose_enemy_skill_impl(
                platform.player(), platform.enemy()));
        }
    });

    std::cout << "Game start." << '\n';
    while (true) {
        {
            std::unique_lock lock(player_mutex);
            player_cv.wait(lock, [&]() { return player_begin; });
            player_begin = false;
        }
        platform.submit_player_context(platform.collect_player_context());
    }
    return 0;
}
