#include "skill-system/skill-sets.hpp"
#include <algorithm>
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_SKILL_SET

REGISTER(fire, skill::FIRE, REQUIRE(RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(write_resource<1.0F, R_FIRE>(player)))

REGISTER(water, skill::WATER, REQUIRE(RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(write_resource<1.0F, R_WATER>(player)))

REGISTER(wood, skill::WOOD, REQUIRE(RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(write_resource<1.0F, R_WOOD>(player)))

REGISTER(earth, skill::EARTH, REQUIRE(RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(write_resource<1.0F, R_EARTH>(player)))

REGISTER_BATCH(explosion, skill::EXPLOSION,
               REQUIRE_BATCH(N > 0 && RESOURCE(R_FIRE, static_cast<float>(N))),
               DSL_BATCH(use_resource<R_FIRE>(static_cast<float>(N), player)),
               DSL_BATCH(write_attack<MAGICAL>(4 * N, player)))

REGISTER_BATCH(iceblade, skill::ICE_BLADE,
               REQUIRE_BATCH(N > 0 && RESOURCE(R_WATER, static_cast<float>(N))),
               DSL_BATCH(use_resource<R_WATER>(static_cast<float>(N), player)),
               DSL_BATCH(write_attack<PHYSICAL>(5 * N, player)))

REPEAT(regeneration,
       DSL(write_callback<[](community &player, community & /*enemy*/) {
           write_recovery<index + 1>(player);
       },
                          {.delayed_rounds_ = index},
                          callback_stage::BEFORE_APPLY_DEFENSE>(player)))

REGISTER(regeneration, skill::REGENERATION, REQUIRE(RESOURCE(R_WOOD, 1.0F)),
         DSL(use_resource<1.0F, R_WOOD>(player)), DSL(regeneration<3>(player)))
namespace {
void stone_shell(community & /*player*/, community & /*enemy*/,
                 defense_entity &defense, attack_data &attack) {
    if (attack.type_ == PHYSICAL) {
        attack.total_damage_ += attack.power_;
        attack.power_ = 0;
        defense.clock_.set_dead();
    }
}
} // namespace
REGISTER(stoneshell, skill::STONE_SHELL, REQUIRE(RESOURCE(R_EARTH, 1.0F)),
         DSL(use_resource<1.0F, R_EARTH>(player)),
         DSL(write_defense<{.id_ = defense_id::STONE_SHELL,
                            .type_ = defense_type::STONE_SHELL,
                            .power_ = 1,
                            .clock_ = {.is_infinite_ = true},
                            .defender_ = stone_shell,
                            .can_merge_ = true,
                            .merge_ = [](defense_entity & /*existing*/,
                                         defense_entity & /*addition*/) {}}>(
             player)))

REPEAT(burn_reduction, DSL(write_defense<{.delayed_rounds_ = index}>(
                           {.type_ = defense_type::PERCENTAGE_REDUCTION,
                            .power_ = 50,
                            .defender_ = percentage_reduction},
                           player)))

REGISTER(burnlife, skill::BURN_LIFE,
         REQUIRE(RESOURCE(R_FIRE, 1.0F) && RESOURCE(R_WOOD, 1.0F)),
         DSL(use_resource<1.0F, R_FIRE>(player);
             use_resource<1.0F, R_WOOD>(player)),
         DSL(burn_reduction<4>(player);
             write_callback<[](community & /*player*/, community &enemy) {
                 for (auto &attack :
                      enemy.focus_.turn_context_.attack_context_.datas_) {
                     if (attack.clock_.is_ringing() && attack.type_ == REAL) {
                         attack.power_ /= 2;
                     }
                 }
             },
                            {.remaining_rounds_ = 4},
                            callback_stage::BEFORE_APPLY_ATTACK>(player);
             write_callback<[](community &player, community & /*enemy*/) {
                 for (auto &attack :
                      player.focus_.turn_context_.attack_context_.datas_) {
                     if (attack.clock_.is_ringing()) {
                         attack.power_ *= 2;
                     }
                 }
             },
                            {.delayed_rounds_ = 1, .remaining_rounds_ = 3},
                            callback_stage::BEFORE_APPLY_EFFECT>(player);
             write_callback<[](community &player, community & /*enemy*/) {
                 use_resource<114514.0F, R_MHP>(player);
             },
                            {.delayed_rounds_ = 3},
                            callback_stage::BEFORE_ROUND_PASS>(player)))

REGISTER(firerain, skill::FIRE_RAIN,
         REQUIRE(RESOURCE(R_FIRE, 1.0F) && RESOURCE(R_EARTH, 1.0F)),
         DSL(use_resource<1.0F, R_FIRE>(player);
             use_resource<1.0F, R_EARTH>(player)),
         DSL(write_attack<8, PHYSICAL>(player);
             write_attack<2, REAL, {.delayed_rounds_ = 1}>(player);
             write_attack<1, REAL, {.delayed_rounds_ = 2}>(player)))
namespace {

void elemental_armor(community &player, community &enemy,
                     defense_entity &defense, attack_data &attack) {
    const int INCOMING = attack.power_;
    if (attack.type_ == MAGICAL) {
        default_armor(player, enemy, defense, attack);
    } else if (attack.type_ == PHYSICAL) {
        attack.total_damage_ += INCOMING;
        attack.power_ = 0;
        defense.power_ = std::max(0, defense.power_ - INCOMING);
    } else {
        return;
    }
}

void update_elemental(community &player, defense_entity &defense) {
    defense.power_ = std::max(0, defense.power_ - 2);
    default_update(player, defense);
    if (defense.power_ <= 0) {
        defense.clock_.set_dead();
        player.focus_.skills_.remove_skill_set(SKILL_SET(elementalarmor));
        player.focus_.skills_.release_all_skills(
            SKILL_SET(elementalarmor)->authorized_skills_);
    }
}
} // namespace

REGISTER(elementalarmor, skill::ELEMENTAL_ARMOR,
         REQUIRE(RESOURCE(R_EARTH, 1.0F) && RESOURCE(R_WATER, 1.0F)),
         DSL(use_resource<1.0F, R_EARTH>(player);
             use_resource<1.0F, R_WATER>(player)),
         DSL(write_defense<{.type_ = defense_type::PHYSICAL_IMMUNITY,
                            .power_ = 1,
                            .defender_ = physical_immunity}>(player);
             write_defense<{.id_ = defense_id::ELEMENTAL_ARMOR,
                            .type_ = defense_type::COMMON_ARMOR,
                            .power_ = 8,
                            .clock_ = {.is_infinite_ = true},
                            .defender_ = elemental_armor,
                            .update_ = update_elemental}>(player);
             player.focus_.skills_.lock_all_skills(
                 SKILL_SET(elementalarmor)->authorized_skills_);
             write_skill_set<&get_elementalarmor>(player)))

END(cauldron)
