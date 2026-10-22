#include "battle-platform/standard-platform.hpp"
#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include "skill-system/skill-sets.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace blacksmith::domain;
using namespace blacksmith::skill_system;

namespace {
void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void equal(float actual, float expected) {
    expect(std::abs(actual - expected) < 1e-6F, "Unexpected resource cost");
}
void test_parsing() {
    auto chain = parse_skill_action(" \tASSOCIATION 0\tmagicattack 3  ");
    expect(chain.skill_ == skill::ASSOCIATION && chain.param_ == 0 &&
               chain.next_ && chain.next_->skill_ == skill::MAGIC_ATTACK &&
               chain.next_->param_ == 3 && !chain.next_->next_,
           "Input lost a skill or parameter");
    expect(format_skill_action(chain) == "association 0 magicattack 3",
           "Display truncated a skill chain");
    auto copy = chain.copy();
    copy.next_->param_ = 2;
    expect(chain.next_->param_ == 3, "Copied chain shares its nodes");
    auto deep = parse_skill_action("association 0 association 0 stick 0");
    expect(deep.next_->next_->skill_ == skill::STICK, "Third input level lost");
    expect(parse_skill_action("iron").param_ == 0, "Single-skill shorthand lost");
    for (const auto *text : {"", "   ", "association 0 stick", "shield -1",
                             "iron 2147483648", "iron 1x", "iron +1",
                             "unknown 0", "monk 0", "prophet 0"}) {
        bool rejected = false;
        try {
            static_cast<void>(parse_skill_action(text));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        expect(rejected, "Malformed or disabled input was accepted");
    }
}

void test_association() {
    blacksmith::battle_platform::standard_pvp battle(false);
    auto &player = battle.player();
    auto &enemy = battle.enemy();
    player.focus_.skills_.add_skill_set(SKILL_SET(phantombook));
    enemy.focus_.skills_.add_skill_set(SKILL_SET(warlock));
    player.focus_.resource_.gain(resource_type::DREAM, 2);
    enemy.focus_.resource_.gain(resource_type::MAGIC, 3);
    enemy.focus_.health_ = {.hp_ = 100, .mhp_ = 100};
    auto legal = [&](const char *text) {
        skill_context context{.action_ = parse_skill_action(text), .self_ = &player};
        return check_skill(player, context) == check_result::SUCCESS;
    };
    expect(legal("association 0 magicattack 3"), "Opponent's spell was rejected");
    expect(!legal("association"), "Missing next node was accepted");
    expect(!legal("association 0 association 0 stick 0"), "Recursive copy accepted");
    expect(!legal("iron 0 stick 0"), "A non-chain skill ignored its tail");
    expect(!legal("association 0 magicattack 3 stick 0"), "Copied spell ignored its tail");
    expect(!legal("association 0 magicattack 4"), "Opponent cannot afford copied spell");
    expect(!legal("association 0 illusion 0"), "Opponent lacks the copied skill");
    expect(!legal("association 0 iron 0"), "Already usable skill was copied");
    player.focus_.resource_.use(resource_type::DREAM, 1);
    expect(!legal("association 0 magicattack 3"), "Dream cost was not checked");
    player.focus_.resource_.gain(resource_type::DREAM, 1);

    skill_context copied{.action_ = parse_skill_action("association 0 magicattack 3"),
                         .self_ = &player};
    skill_context idle{.action_ = {.skill_ = skill::IRON}, .self_ = &enemy};
    declare(player, copied, enemy, idle);
    equal(player.focus_.resource_.query(resource_type::DREAM), 0);
    equal(player.focus_.resource_.query(resource_type::MAGIC), 0);
    equal(enemy.focus_.resource_.query(resource_type::MAGIC), 3);
    const auto &attacks = player.focus_.turn_context_.attack_context_.datas_;
    expect(attacks.size() == 3 && attacks[0].power_ == 6 &&
               attacks[0].clock_.delayed_rounds_ == 0 &&
               attacks[1].clock_.delayed_rounds_ == 1 &&
               attacks[2].clock_.delayed_rounds_ == 2,
           "Association failed to pass the copied skill's parameter/context");
    expect(player.history_.back().next_->param_ == 3,
           "History discarded the borrowed action");
    judge(player, enemy);
    expect(enemy.focus_.health_.hp_ == 94, "Copied spell's first hit was wrong");
    skill_context wait{.action_ = {.skill_ = skill::IRON}, .self_ = &player};
    declare(player, wait, enemy, idle);
    judge(player, enemy);
    expect(enemy.focus_.health_.hp_ == 88, "Copied delayed spell did not resolve");
}

void test_platform_input() {
    blacksmith::battle_platform::standard_pvp platform(false);
    platform.enemy().focus_.resource_.gain(resource_type::IRON, 1);
    expect(!platform.to_context("stick 0", platform.player()),
           "Player used enemy resources");
    expect(platform.to_context("stick 0", platform.enemy()).has_value(),
           "Enemy was checked against player resources");
    platform.player().focus_.skills_.add_skill_set(SKILL_SET(phantombook));
    platform.player().focus_.resource_.gain(resource_type::DREAM, 2);
    auto &skills = platform.enemy().focus_.skills_;
    skills.add_skill_set(SKILL_SET(warlock));
    platform.enemy().focus_.resource_.gain(resource_type::MAGIC, 3);
    std::istringstream input("association\nassociation 0 magicattack 3\n");
    auto *previous = std::cin.rdbuf(input.rdbuf());
    auto context = platform.collect_context(platform.player());
    std::cin.rdbuf(previous);
    platform.submit_player_context(context);
    auto idle = platform.to_context("iron", platform.enemy());
    platform.submit_enemy_context(*idle);
    expect(platform.player_action().next_->param_ == 3,
           "Submitted action discarded its tail");
}
} // namespace

int main() {
    test_parsing();
    test_association();
    test_platform_input();
}
