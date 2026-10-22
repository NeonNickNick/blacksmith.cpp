#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include "skill-system/skill-sets.hpp"
#include <battle-platform/standard-platform.hpp>
#include <cassert>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace blacksmith::battle_platform {
standard_pvp::standard_pvp(bool enable_info) {
    enable_info_ = enable_info;
    initialize(player_, enemy_);
}
void standard_pvp::reset() {
    player_ = community(&enemy_);
    enemy_ = community(&player_);
    round_ = 1;
    initialize(player_, enemy_);
}
void standard_pvp::set_callback(std::function<void()> &&callback) {
    callback_ = std::move(callback);
}
community &standard_pvp::player() { return player_; }
community &standard_pvp::enemy() { return enemy_; }
int standard_pvp::round() const { return round_; }
skill_action standard_pvp::player_action() const {
    return player_context_.action_.copy();
}
skill_action standard_pvp::enemy_action() const {
    return enemy_context_.action_.copy();
}
skill_context standard_pvp::collect_context(community &com) {
    for (std::string input; std::getline(std::cin, input);) {
        if (auto context = to_context(input, com))
            return std::move(*context);
    }
    throw std::ios_base::failure("Input closed.");
}
std::optional<skill_context> standard_pvp::to_context(std::string input,
                                                    community &com) {
    try {
        skill_context context{.action_ = parse_skill_action(input), .self_ = &com};
        switch (check_skill(com, context)) {
        case check_result::INVALID:
            std::cout << "Invalid.\n";
            break;
        case check_result::REJECTED:
            std::cout << "Rejected.\n";
            break;
        case check_result::SUCCESS:
            std::cout << "Succeed.\n";
            return context;
        }
    } catch (const std::invalid_argument &error) {
        std::cout << error.what() << '\n';
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
        std::cout << "player: " << format_skill_action(player_context_.action_) << '\n';
        std::cout << "enemy: " << format_skill_action(enemy_context_.action_) << '\n';
    }

    callback_();
}
} // namespace blacksmith::battle_platform
