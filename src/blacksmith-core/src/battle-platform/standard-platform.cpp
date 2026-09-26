#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include <battle-platform/standard-platform.hpp>
#include <cassert>
#include <functional>
#include <mutex>
#include <utility>
namespace blacksmith_core::battle_platform {
void standard_pvp::set_callback(std::function<void()> &&callback) {
    callback_ = std::move(callback);
}
community &standard_pvp::player() { return player_; }
community &standard_pvp::enemy() { return enemy_; }
void standard_pvp::submit_player_context(skill_context &&context) {
    player_context_ = std::move(context);
    player_submitted_ = true;
    try_pass_round();
}
void standard_pvp::submit_enemy_context(skill_context &&context) {
    enemy_context_ = std::move(context);
    enemy_submitted_ = true;
    try_pass_round();
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
    print_info(player_, enemy_);

    callback_();
}
} // namespace blacksmith_core::battle_platform