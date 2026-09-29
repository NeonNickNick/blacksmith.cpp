#pragma once

#include "blacksmith-master/blacksmith-ai.hpp"
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <domain/models.hpp>
#include <functional>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <thread>

using namespace blacksmith_core::domain;
namespace blacksmith_core::battle_platform {
class standard_pvp {
  public:
    enum class battle_result : uint8_t {
        PLAYER_WIN,
        ENEMY_WIN,
        DRAW,
        BATTLING
    };
    standard_pvp(bool enable_info = true);
    void reset();
    [[nodiscard]] community &player();
    [[nodiscard]] community &enemy();
    [[nodiscard]] skill_action player_action() const;
    [[nodiscard]] skill_action enemy_action() const;
    [[nodiscard]] skill_context collect_player_context();
    [[nodiscard]] battle_result result() const;
    void submit_player_context(skill_context &&context);
    void submit_enemy_context(skill_context &&context);
    void set_callback(std::function<void()> &&callback);

  private:
    bool enable_info_{true};
    std::mutex mtx_;
    std::function<void()> callback_;
    community player_{};
    community enemy_{};
    skill_context player_context_{};
    skill_context enemy_context_{};
    bool player_submitted_{false};
    bool enemy_submitted_{false};
    void try_pass_round();
};

template <typename T>
concept is_blacksmith_ai =
    std::derived_from<T, blacksmith_master::blacksmith_ai<T>>;

template <is_blacksmith_ai B, is_blacksmith_ai T> class benchmark_test {
  public:
    [[nodiscard]] float win_rate(int battle_times) {
        int win_times = 0;
        int battled = 0;
        float res = 0.5F;
        baseline_.init();
        test_.init();

        bool enemy_begin{true};
        bool player_begin{true};
        bool finished{false};
        std::mutex enemy_mutex;
        std::mutex player_mutex;
        std::mutex finish_mutex;
        std::condition_variable enemy_cv;
        std::condition_variable player_cv;
        std::condition_variable finish_cv;
        blacksmith_core::battle_platform::standard_pvp platform{false};

        platform.set_callback({[&]() {
            auto result = platform.result();
            if (result != standard_pvp::battle_result::BATTLING) {
                if (result == standard_pvp::battle_result::PLAYER_WIN) {
                    win_times++;
                }
                battled++;
                std::cout << battled << '\n';
                platform.reset();
                if (battled >= battle_times) {
                    std::unique_lock lock(finish_mutex);
                    res = static_cast<float>(win_times) /
                          static_cast<float>(battle_times);
                    finished = true;
                    finish_cv.notify_one();
                }
            }

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

        std::jthread baseline_t([&](const std::stop_token &st) {
            B ai{};
            ai.init();

            while (!st.stop_requested()) {
                {
                    std::unique_lock lock(enemy_mutex);
                    enemy_cv.wait(lock, [&]() { return enemy_begin; });
                    enemy_begin = false;
                }

                platform.submit_enemy_context(ai.choose_enemy_skill_impl(
                    platform.player(), platform.enemy()));
            }
        });
        std::jthread test_t([&](const std::stop_token &st) {
            T ai{};
            ai.init();

            while (!st.stop_requested()) {
                {
                    std::unique_lock lock(player_mutex);
                    player_cv.wait(lock, [&]() { return player_begin; });
                    player_begin = false;
                }

                platform.submit_player_context(ai.choose_enemy_skill_impl(
                    platform.enemy(), platform.player()));
            }
        });

        std::unique_lock lock(finish_mutex);
        finish_cv.wait(lock, [&]() { return finished; });
        return res;
    }

  private:
    B baseline_;
    T test_;
};
} // namespace blacksmith_core::battle_platform