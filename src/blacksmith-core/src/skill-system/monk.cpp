#include "skill-system/skill-sets.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_SKILL_SET
namespace {
void golden_bell(int uses, community &player) {
    write_defense({.type_ = defense_type::PERCENTAGE_REDUCTION,
                   .power_ = 100 - 60 * uses,
                   .defender_ = percentage_reduction},
                  player);
}

void lost_clone(community &player) {
    write_attack<3, PHYSICAL, {.delayed_rounds_ = 1}>(player);
    write_attack<2, MAGICAL, {.delayed_rounds_ = 1}>(player);
}

void remove_dead_clones(community &player) {
    const auto lost =
        std::erase_if(player.focus_.monk_.clones_, [](const monk_clone &clone) {
            return clone.health_.is_dead();
        });
    for (std::size_t i = 0; i < lost; ++i) {
        lost_clone(player);
    }
}

void transfer_damage(community &player, community & /*enemy*/,
                     defense_entity & /*defense*/, attack_data &attack) {
    remove_dead_clones(player);
    auto &monk = player.focus_.monk_;
    const int percent = monk.transfer_rounds_ > 0 ? 90 : 50;
    for (auto &clone : monk.clones_) {
        const int redirected = static_cast<int>(
            std::ceil(static_cast<double>(attack.power_) * percent / 100.0));
        // Overkill belongs to the clone as well; do not increase total damage.
        clone.health_.lose_hp(redirected);
        attack.power_ -= redirected;
    }
    remove_dead_clones(player);
}

void maze_hit_armor(community &player, community &enemy,
                    attack_data & /*attack*/) {
    // The existing hook also fires on an unarmored body. Check for real armor.
    if (std::ranges::any_of(enemy.focus_.defense_.defenses_, [](const auto &d) {
            return d.clock_.is_ringing() && d.power_ > 0 &&
                   std::ranges::find(ARMOR_TYPES, d.type_) != ARMOR_TYPES.end();
        })) {
        write_defense<{.delayed_rounds_ = 1}>(
            {.type_ = defense_type::PHYSICAL_IMMUNITY,
             .power_ = 1,
             .defender_ = direct_immunity},
            player);
    }
}
} // namespace

PASSIVE_(REQUIRE_(NOTHING),
         DSL(auto &monk = player.focus_.monk_;
             for (auto &clone : monk.clones_) {
                 golden_bell(clone.golden_bell_uses_++, player);
                 if (clone.mist_rounds_ > 0) {
                     clone.health_.gain_hp(3);
                     --clone.mist_rounds_;
                 }
             } write_defense({.id_ = defense_id::MONK_CLONES,
                              .type_ = defense_type::DAMAGE_TRANSFER,
                              .power_ = 1,
                              .defender_ = transfer_damage},
                             player);
             write_callback<[](community &player, community & /*enemy*/) {
                 // A clone created by paying the final HP also dies this turn.
                 remove_dead_clones(player);
                 auto &monk = player.focus_.monk_;
                 monk.mist_cooldown_ = std::max(0, monk.mist_cooldown_ - 1);
                 monk.transfer_rounds_ = std::max(0, monk.transfer_rounds_ - 1);
             },
                            {}, callback_stage::BEFORE_ROUND_PASS>(player)))

REGISTER(jade, skill::JADE, REQUIRE(RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(write_resource<1.0F, R_JADE>(player)))

REGISTER(ghoststep, skill::GHOST_STEP,
         REQUIRE(RESOURCE(R_JADE, 1.0F) && HP(1) &&
                 player.focus_.monk_.clones_.size() < 2),
         DSL(use_resource<1.0F, R_JADE>(player);
             use_resource<1.0F, R_HP>(player)),
         DSL(const int hp = player.focus_.health_.hp_;
             const int mhp = player.focus_.health_.mhp_;
             // Mist may heal the clone above its HP at creation.
             player.focus_.monk_.clones_.push_back({.health_ = {.hp_ = hp,
                                                                .mhp_ = mhp}})))

REGISTER(goldenbellcover, skill::GOLDEN_BELL_COVER,
         REQUIRE(RESOURCE(R_JADE, 1.0F)),
         DSL(use_resource<1.0F, R_JADE>(player)),
         DSL(golden_bell(player.focus_.monk_.golden_bell_uses_++, player)))

REGISTER(
    mazefist, skill::MAZE_FIST, REQUIRE(!player.focus_.monk_.clones_.empty()),
    DSL(auto &clones = player.focus_.monk_.clones_;
        clones.erase(std::ranges::max_element(clones, {},
                                              [](const monk_clone &clone) {
                                                  return clone.health_.hp_;
                                              }));
        lost_clone(player)),
    DSL(write_attack<
        6, PHYSICAL, {},
        &execute_attack<{.first_time_hit_armor_ = maze_hit_armor}>>(player)))

REGISTER(mist, skill::MIST,
         REQUIRE(RESOURCE(R_IRON, 1.0F) &&
                 !player.focus_.monk_.clones_.empty() &&
                 player.focus_.monk_.mist_cooldown_ == 0),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(auto &monk = player.focus_.monk_;
             auto &clone = *std::ranges::min_element(
                 monk.clones_, {},
                 [](const monk_clone &entry) { return entry.health_.hp_; });
             clone.health_.gain_hp(3); clone.mist_rounds_ = 2;
             monk.transfer_rounds_ = 3; monk.mist_cooldown_ = 3))

REGISTER(disillusionment, skill::DISILLUSIONMENT,
         REQUIRE(RESOURCE(R_JADE, 2.0F)),
         DSL(use_resource<2.0F, R_JADE>(player)),
         DSL(write_attack<6, PHYSICAL>(player);
             write_attack<4, MAGICAL>(player)))

END(monk)
