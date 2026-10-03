#include "battle-platform/standard-platform.hpp"
#include "domain/models.hpp"
#include "skill-system/professions.hpp"
#include <algorithm>
#include <blacksmith-master/blacksmith-zero.hpp>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
std::vector<float> scale = {
    1.52641411F,  0.88290204F,  0.35274672F,  0.61542097F, 0.83097131F,
    0.61222351F,  0.3292151F,   1.65260821F,  1.73401696F, 0.77265715F,
    -0.01689368F, 1.39116813F,  1.13587425F,  1.28306686F, -0.28837448F,
    1.69835323F,  -0.93580848F, -0.18005078F, 1.02364501F};
int main() {
    blacksmith::blacksmith_master::blacksmith_zero_param param{};
    auto v = param.to_vector();
    for (size_t i = 0; i < v.size(); ++i) {
        v[i] *= scale[i];
    }
    param.from_vector(v);
    bool enemy_begin{true};
    bool player_begin{true};
    std::mutex enemy_mutex;
    std::mutex player_mutex;
    std::condition_variable enemy_cv;
    std::condition_variable player_cv;
    auto &mapping = blacksmith::skill_system::get_string_skill_mapping();
    blacksmith::battle_platform::standard_pvp platform{};

    platform.set_callback({[&]() {
        const auto &action = platform.enemy_action();

        auto it = std::ranges::find_if(mapping, [&action](const auto &pair) {
            return pair.second == action.skill_;
        });
        std::cout << "blacksmith-zero: " << it->first << " " << action.param_
                  << '\n';

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
        blacksmith::blacksmith_master::blacksmith_zero ai{};
        ai.param_ = param;
        while (true) {
            {
                std::unique_lock lock(enemy_mutex);
                enemy_cv.wait(lock, [&]() { return enemy_begin; });
                enemy_begin = false;
            }

            platform.submit_enemy_context(ai.choose_enemy_skill_impl(
                platform.player(), platform.enemy(), platform.round()));
        }
    });

    std::cout << "Game start." << '\n';
    while (true) {
        {
            std::unique_lock lock(player_mutex);
            player_cv.wait(lock, [&]() { return player_begin; });
            player_begin = false;
        }
        platform.submit_player_context(
            platform.collect_context(platform.player()));
    }
    return 0;
}
