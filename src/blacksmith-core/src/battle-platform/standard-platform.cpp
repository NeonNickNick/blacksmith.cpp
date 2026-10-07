#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include "skill-system/professions.hpp"
#include <algorithm>
#include <battle-platform/standard-platform.hpp>
#include <cassert>
#include <charconv>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace blacksmith::battle_platform {
standard_pvp::standard_pvp(bool enable_info) {
    enable_info_ = enable_info;
    initialize(player_, enemy_);
}
void standard_pvp::reset() {
    player_ = {};
    enemy_ = {};
    round_ = 1;
    initialize(player_, enemy_);
}
void standard_pvp::set_callback(std::function<void()> &&callback) {
    callback_ = std::move(callback);
}
community &standard_pvp::player() { return player_; }
community &standard_pvp::enemy() { return enemy_; }
int standard_pvp::round() const { return round_; }
namespace {
bool to_int(std::string_view sv, int &out) {
    const char *first = sv.data();
    const char *last = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc() && ptr == last;
}
} // namespace
skill_action standard_pvp::player_action() const {
    return player_context_.action_.copy();
}
skill_action standard_pvp::enemy_action() const {
    return enemy_context_.action_.copy();
}
skill_context standard_pvp::collect_context(community &com) { // NOLINT
    while (true) {
        std::string skill_name;
        int param = 0;
        skill_context context{
            .action_ = {.skill_ = skill::IRON, .param_ = 0, .next_ = nullptr},
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
        auto &mapping = get_string_skill_mapping();
        auto it =
            std::ranges::find_if(mapping, [&skill_name](const auto &pair) {
                return pair.first == skill_name;
            });
        if (it == mapping.end()) {
            std::cout << "Invalid." << '\n';
            continue;
        }
        context.action_.skill_ = it->second;
        context.action_.param_ = param;
        auto res = check_skill(com, context);
        switch (res) {
        case blacksmith::domain::check_result::INVALID:
            std::cout << "Invalid." << '\n';
            continue;
        case blacksmith::domain::check_result::REJECTED:
            std::cout << "Rejected." << '\n';
            continue;
        case blacksmith::domain::check_result::SUCCESS:
            std::cout << "Succeed." << '\n';
            return context;
        }
    }
}
std::optional<skill_context> standard_pvp::to_context(std::string input,
                                                      community &com) {
    std::string skill_name;
    int param = 0;
    skill_context context{
        .action_ = {.skill_ = skill::IRON, .param_ = 0, .next_ = nullptr},
        .self_ = &com};
    std::vector<std::string> tokens;
    for (auto sub : input | std::views::split(' ')) {
        tokens.emplace_back(sub.begin(), sub.end());
    }
    if (tokens.size() != 2) {
        if (tokens.size() == 1) {
            tokens.emplace_back("0");
        } else {
            std::cout << "Wrong format." << '\n';
            return std::nullopt;
        }
    }
    skill_name = tokens[0];
    if (!to_int(tokens[1], param)) {
        std::cout << "Wrong format." << '\n';
        return std::nullopt;
    }
    if (param < 0) {
        std::cout << "Wrong format." << '\n';
        return std::nullopt;
    }
    auto &mapping = get_string_skill_mapping();
    auto it = std::ranges::find_if(mapping, [&skill_name](const auto &pair) {
        return pair.first == skill_name;
    });
    if (it == mapping.end()) {
        std::cout << "Invalid." << '\n';
        return std::nullopt;
    }
    context.action_.skill_ = it->second;
    context.action_.param_ = param;
    auto res = check_skill(player_, context);
    switch (res) {
    case blacksmith::domain::check_result::INVALID:
        std::cout << "Invalid." << '\n';
        return std::nullopt;
    case blacksmith::domain::check_result::REJECTED:
        std::cout << "Rejected." << '\n';
        return std::nullopt;
    case blacksmith::domain::check_result::SUCCESS:
        std::cout << "Succeed." << '\n';
        return context;
    }
    return std::nullopt;
}
void standard_pvp::submit_player_context(const skill_context &context) {
    assert(!player_submitted_);
    player_context_ = {.action_ = context.action_.copy(),
                       .self_ = context.self_};
    player_submitted_ = true;
    try_pass_round();
}
void standard_pvp::submit_enemy_context(const skill_context &context) {
    assert(!enemy_submitted_);
    enemy_context_ = {.action_ = context.action_.copy(),
                      .self_ = context.self_};
    enemy_submitted_ = true;
    try_pass_round();
}
standard_pvp::battle_result standard_pvp::result() const {
    auto p = player_.focus_.health_.hp_ <= 0;
    auto e = enemy_.focus_.health_.hp_ <= 0;
    if (p && e) {
        return standard_pvp::battle_result::DRAW;
    }
    if (p) {
        return standard_pvp::battle_result::ENEMY_WIN;
    }
    if (e) {
        return standard_pvp::battle_result::PLAYER_WIN;
    }
    return standard_pvp::battle_result::BATTLING;
}
void standard_pvp::try_pass_round() {

    std::lock_guard lock(mtx_);

    if (!(player_submitted_ && enemy_submitted_)) {
        return;
    }

    player_submitted_ = false;
    enemy_submitted_ = false;
    declare(player_, player_context_, enemy_, enemy_context_);
    judge(player_, enemy_);
    round_++;

    if (enable_info_) {
        print_info(player_, enemy_);
        auto &mapping = blacksmith::skill_system::get_string_skill_mapping();
        const auto &p_action = player_action();
        const auto &e_action = enemy_action();

        auto pit = std::ranges::find_if(mapping, [&p_action](const auto &pair) {
            return pair.second == p_action.skill_;
        });
        std::cout << "player: " << pit->first << " " << p_action.param_ << '\n';

        auto eit = std::ranges::find_if(mapping, [&e_action](const auto &pair) {
            return pair.second == e_action.skill_;
        });
        std::cout << "enemy: " << eit->first << " " << e_action.param_ << '\n';
    }

    callback_();
}
} // namespace blacksmith::battle_platform