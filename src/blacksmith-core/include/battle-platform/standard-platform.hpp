#pragma once

#include "blacksmith-master/blacksmith-ai.hpp"
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <domain/models.hpp>
#include <functional>
#include <iostream>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

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
    [[nodiscard]] int round() const;
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
    int round_{1};
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
        constexpr int THREAD_COUNT = 12;
        baseline_.init();
        test_.init();
        cnt_ = 0;
        std::atomic_int win_times = 0;
        std::vector<std::jthread> threads;
        threads.reserve(THREAD_COUNT);
        for (int i = 0; i < THREAD_COUNT; ++i) {
            threads.emplace_back([i, battle_times, &win_times, this]() {
                win_times += win_rate_unit(i, battle_times);
            });
        }
        for (int i = 0; i < THREAD_COUNT; ++i) {
            threads[i].join();
        }
        return static_cast<float>(win_times) / static_cast<float>(battle_times);
    }

  private:
    int cnt_{0};
    std::mutex cnt_mutex_;
    std::mutex cout_mutex_;
    int win_rate_unit(int index, int battle_times) {
        int win_times = 0;

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
            {
                std::unique_lock cnt_lock(cnt_mutex_);

                if (cnt_ >= battle_times) {
                    {
                        std::unique_lock lock(finish_mutex);
                        finished = true;
                    }
                    finish_cv.notify_one();
                } else {

                    auto result = platform.result();
                    if (result != standard_pvp::battle_result::BATTLING) {
                        if (result == standard_pvp::battle_result::PLAYER_WIN) {
                            win_times++;
                        }

                        cnt_++;
                        if (cnt_ % 50 == 0) {
                            std::cout << "index" << index << ": " << cnt_
                                      << '\n';
                        }
                        cnt_lock.unlock();
                        platform.reset();
                    }
                }
            }
            {
                std::unique_lock lock(player_mutex);
                player_begin = true;
            }
            player_cv.notify_one();
            {
                std::unique_lock lock(enemy_mutex);
                enemy_begin = true;
            }
            enemy_cv.notify_one();
        }});

        std::jthread baseline_t([&](const std::stop_token &st) {
            B ai{};
            ai.init();

            while (!st.stop_requested()) {
                {
                    std::unique_lock lock(enemy_mutex);
                    enemy_cv.wait(lock, [&]() {
                        return enemy_begin || st.stop_requested();
                    });
                    if (st.stop_requested()) {
                        break;
                    }
                    enemy_begin = false;
                }
                platform.submit_enemy_context(ai.choose_enemy_skill(
                    platform.player(), platform.enemy(), platform.round()));
            }
        });
        std::jthread test_t([&](const std::stop_token &st) {
            T ai{};
            ai.init();

            while (!st.stop_requested()) {
                {
                    std::unique_lock lock(player_mutex);
                    player_cv.wait(lock, [&]() {
                        return player_begin || st.stop_requested();
                    });
                    if (st.stop_requested()) {
                        break;
                    }
                    player_begin = false;
                }
                platform.submit_player_context(ai.choose_enemy_skill(
                    platform.enemy(), platform.player(), platform.round()));
            }
        });

        std::unique_lock lock(finish_mutex);
        finish_cv.wait(lock, [&]() { return finished; });
        lock.unlock();
        {
            std::unique_lock el(enemy_mutex);
            baseline_t.request_stop();
        }
        {
            std::unique_lock el(player_mutex);
            test_t.request_stop();
        }
        enemy_cv.notify_all();
        player_cv.notify_all();
        return win_times;
    }
    B baseline_;
    T test_;
};
} // namespace blacksmith_core::battle_platform
