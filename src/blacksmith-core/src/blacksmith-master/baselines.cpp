#include "blacksmith-master/baselines.hpp"
#include "domain/models.hpp"
#include <array>
#include <cstddef>
#include <random>
namespace blacksmith::blacksmith_master {

skill_context
crazy_lancer::choose_enemy_skill_impl(const community & /*player*/, // NOLINT
                                      const community &enemy, int round) {
    if (round <= 3) {
        return {.action_ = {.skill_ = skill::IRON},
                .self_ = &const_cast<community &>(enemy)};
    }
    if (round == 4) {
        return {.action_ = {.skill_ = skill::LANCER},
                .self_ = &const_cast<community &>(enemy)};
    }
    skill_context charge_ctx = {.action_ = {.skill_ = skill::CHARGE},
                                .self_ = &const_cast<community &>(enemy)};
    skill_context dragon_ctx = {.action_ = {.skill_ = skill::RISING_DRAGON},
                                .self_ = &const_cast<community &>(enemy)};
    if (enemy.focus_.profession_.check(charge_ctx) == check_result::SUCCESS) {
        return charge_ctx;
    }
    if (enemy.focus_.profession_.check(dragon_ctx) == check_result::SUCCESS) {
        return dragon_ctx;
    }

    return {.action_ = {.skill_ = skill::IRON},
            .self_ = &const_cast<community &>(enemy)};
}
skill_context
random_lancer::choose_enemy_skill_impl(const community & /*player*/, // NOLINT
                                       const community &enemy, int round) {
    static std::array<skill, 4> skills = {
        skill::SKY_STRIKE, skill::TYRANT_DESTURCTION, skill::DRAGON_TOOTH,
        skill::TRIPLE_STAB};
    std::mt19937 random{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, 3);
    if (round <= 3) {
        return {.action_ = {.skill_ = skill::IRON},
                .self_ = &const_cast<community &>(enemy)};
    }
    if (round == 4) {
        return {.action_ = {.skill_ = skill::LANCER},
                .self_ = &const_cast<community &>(enemy)};
    }
    if (enemy.focus_.resource_.check(resource_type::IRON, 1.0F)) {
        return {.action_ = {.skill_ = skills[pick(random)]},
                .self_ = &const_cast<community &>(enemy)};
    }
    return {.action_ = {.skill_ = skill::IRON},
            .self_ = &const_cast<community &>(enemy)};
}
} // namespace blacksmith::blacksmith_master