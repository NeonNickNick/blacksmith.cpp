#include "battle-platform/standard-platform.hpp"
#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include <atomic>
#include <blacksmith-master/blacksmith-ai.hpp>
#include <charconv>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>
bool to_int(std::string_view sv, int &out) {
    const char *first = sv.data();
    const char *last = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc() && ptr == last;
}
int main() {
    std::atomic_bool enemy_begin{true};
    std::atomic_bool player_begin{true};
    std::mutex enemy_mutex;
    std::condition_variable enemy_cv;
    blacksmith_core::battle_platform::standard_pvp platform{};
    platform.set_callback({[&]() {
        player_begin = true;

        enemy_begin = true;
        enemy_cv.notify_one();
    }});
    blacksmith_core::domain::initialize(platform.player(), platform.enemy());
    /*
    auto round_pass = [&]() {
        std::cout << "blacksmith-zero: " << enemy_context.action_.skill_name_
                  << " " << enemy_context.action_.param_ << '\n';
    };*/
    std::jthread t([&]() {
        blacksmith_core::blacksmith_master::blacksmith_zero ai{};
        ai.init();

        while (true) {
            {
                std::unique_lock lock(enemy_mutex);

                enemy_cv.wait(lock, [&]() { return enemy_begin.load(); });

                enemy_begin = false;
            }

            platform.submit_enemy_context(ai.choose_enemy_skill_impl(
                platform.player(), platform.enemy()));
        }
    });

    std::cout << "Game start." << '\n';

    auto get_context = [](community &com) {
        while (true) {
            std::string skill_name;
            int param = 0;
            skill_context context{.action_ = {.skill_name_ = "iron",
                                              .param_ = 0,
                                              .next_ = nullptr},
                                  .self_ = &com};
            std::string input;
            std::getline(std::cin, input);
            std::vector<std::string> tokens;
            for (auto sub : input | std::views::split(' ')) {
                tokens.emplace_back(sub.begin(), sub.end());
            }
            if (tokens.size() != 2) {
                if (tokens.size() == 1) {
                    tokens.emplace_back("0");
                } else {
                    std::cout << "Wrong format." << '\n';
                    continue;
                }
            }
            skill_name = tokens[0];
            if (!to_int(tokens[1], param)) {
                std::cout << "Wrong format." << '\n';
                continue;
            }
            if (param < 0) {
                std::cout << "Wrong format." << '\n';
                continue;
            }
            context.action_.skill_name_ = skill_name;
            if (param != 0) {
                context.action_.skill_name_ += tokens[1];
            }
            context.action_.param_ = param;
            auto res = check_skill(com, context);
            switch (res) {
            case blacksmith_core::domain::check_result::INVALID:
                std::cout << "Invalid." << '\n';
                continue;
            case blacksmith_core::domain::check_result::REJECTED:
                std::cout << "Rejected." << '\n';
                continue;
            case blacksmith_core::domain::check_result::SUCCESS:
                std::cout << "Succeed." << '\n';
                return context;
            }
        }
    };
    while (true) {
        if (!player_begin) {
            continue;
        }
        player_begin = false;
        platform.submit_player_context(get_context(platform.player()));
    }
    return 0;
}
