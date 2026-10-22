#include "skill-system/skill-sets.hpp"
#include <algorithm>
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <limits>
#include <skill-system/dsl-macro.hpp>

BEGIN_SKILL_SET
namespace {
void evade(community &player, community & /*enemy*/,
           defense_entity & /*defense*/, attack_data &attack) {
    auto &prophet = player.focus_.prophet_;
    if (!prophet.evaded_) {
        // Failed evasion attempts keep their pending enhancements.
        prophet.caution_ = take_mark<mark_id::GREATEST_CAUTION>(player);
        prophet.revelation_ = take_mark<mark_id::REVELATION>(player);
        prophet.evaded_ = true;
    }
    attack.power_ = 0;
}

void prepare_evasion(community &player) {
    write_defense({.type_ = defense_type::PHYSICAL_IMMUNITY,
                   .power_ = 1,
                   .defender_ = evade},
                  player);
}

template <mark_id hit_mark>
void crystal_wall(community &player, community & /*enemy*/,
                  defense_entity &defense, attack_data &attack) {
    if (count_mark<hit_mark>(player) == 0) {
        write_resource<1.0F, R_CRYSTAL>(player);
        write_mark<hit_mark, {}>(player);
    }
    const int absorbed = std::min(defense.power_, attack.power_);
    if (attack.type_ == PHYSICAL) {
        write_attack<MAGICAL, {.delayed_rounds_ = 1}>(absorbed, player);
    }
    attack.total_damage_ += absorbed;
    attack.power_ -= absorbed;
}
} // namespace

PASSIVE_(REQUIRE_(NOTHING),
         DSL(player.focus_.prophet_ = {};
             write_callback<[](community &player, community & /*enemy*/) {
                 const auto &prophet = player.focus_.prophet_;
                 if (prophet.evaded_) {
                     write_resource<1.0F, R_CRYSTAL>(player);
                     if (prophet.revelation_ > 0) {
                         player.focus_.resource_.gain(
                             R_CRYSTAL,
                             static_cast<float>(prophet.revelation_));
                     }
                     if (prophet.caution_ > 0) {
                         write_attack<REAL, {.delayed_rounds_ = 1}>(
                             4 * prophet.caution_, player);
                     }
                 }
             },
                            {}, callback_stage::BEFORE_APPLY_RESOURCE>(player)))

REGISTER(crystal, skill::CRYSTAL, REQUIRE(RESOURCE(R_IRON, 1.5F)),
         DSL(use_resource<1.5F, R_IRON>(player)),
         DSL(write_resource<1.0F, R_CRYSTAL>(player)))

REGISTER(crystalball, skill::CRYSTAL_BALL,
         REQUIRE(RESOURCE(R_CRYSTAL, 2.0F) &&
                 player.focus_.resource_.query(R_CRYSTAL_BALL) < 1.0F),
         DSL(use_resource<2.0F, R_CRYSTAL>(player)),
         DSL(write_resource<1.0F, R_CRYSTAL_BALL>(player)))

REGISTER(foretold, skill::FORETOLD, REQUIRE(RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_IRON>(player)), DSL(prepare_evasion(player)))

REGISTER(greatestcaution, skill::GREATEST_CAUTION,
         REQUIRE(RESOURCE(R_CRYSTAL, 1.0F)),
         DSL(use_resource<1.0F, R_CRYSTAL>(player)),
         DSL(write_mark<mark_id::GREATEST_CAUTION>(player)))

REGISTER(revelation, skill::REVELATION, REQUIRE(RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(write_mark<mark_id::REVELATION>(player)))

REGISTER_(assertion, skill::ASSERTION, REQUIRE_(NOTHING), DSL_(),
          DSL(prepare_evasion(player);
              write_callback<[](community &player, community &enemy) {
                  const auto &attacks =
                      enemy.focus_.turn_context_.attack_context_.datas_;
                  if (!std::ranges::any_of(
                          attacks, [](const attack_data &attack) {
                              return attack.clock_.is_ringing() &&
                                     attack.power_ > 0;
                          })) {
                      use_resource<2.0F, R_HP>(player);
                  }
              },
                             {}, callback_stage::BEFORE_CANCEL_ATTACK>(player)))

// Each round owns a fresh wall and its own first-hit Crystal refund.
REPEAT_BATCH(crystal_wall_rounds,
             DSL(constexpr auto hit_mark =
                     index == 0 ? mark_id::CRYSTAL_WALL_HIT
                                : mark_id::DELAYED_CRYSTAL_WALL_HIT;
                 write_defense<{.delayed_rounds_ = index}>(
                     {.id_ = defense_id::CRYSTAL_WALL,
                      .type_ = defense_type::REAL_REDUCTION,
                      .power_ = 7,
                      .defender_ = crystal_wall<hit_mark>},
                     player)))

REGISTER(crystalwall, skill::CRYSTAL_WALL,
         REQUIRE(RESOURCE(R_CRYSTAL_BALL, 1.0F)),
         DSL(use_resource<1.0F, R_CRYSTAL_BALL>(player)),
         DSL(crystal_wall_rounds<2>(0, player)))

REGISTER(refraction, skill::REFRACTION, REQUIRE(RESOURCE(R_CRYSTAL_BALL, 1.0F)),
         DSL_(),
         DSL(write_defense<{.type_ = defense_type::PERCENTAGE_REDUCTION,
                            .power_ = 50,
                            .defender_ = percentage_reduction}>(player)))

REGISTER_BATCH(ultimatum, skill::ULTIMATUM,
               REQUIRE_BATCH(N > 5 && N < std::numeric_limits<int>::max() &&
                             RESOURCE(R_CRYSTAL_BALL, 1.0F) &&
                             RESOURCE(R_CRYSTAL, 2.0F)),
               DSL(use_resource<1.0F, R_CRYSTAL_BALL>(player);
                   use_resource<2.0F, R_CRYSTAL>(player)),
               DSL_BATCH(auto &attack = write_attack<12, REAL>(player);
                         attack.clock_.delayed_rounds_ += N))

END(prophet)
