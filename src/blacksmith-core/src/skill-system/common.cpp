#include "skill-system/professions.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>
#include <vector>

BEGIN_PROFESSION

REGISTER_(iron, skill::IRON, REQUIRE_(NOTHING), DSL_(),
          DSL(write_resource<1.0F, R_IRON>(player);))

REGISTER(space, skill::SPACE, REQUIRE(RESOURCE(R_IRON, 3.0F)),
         DSL(use_resource<3.0F, R_IRON>(player);),
         DSL(write_resource<1.0F, R_SPACE>(player);))

REGISTER(time, skill::TIME, REQUIRE(RESOURCE(R_IRON, 3.0F)),
         DSL(use_resource<3.0F, R_IRON>(player);),
         DSL(write_resource<1.0F, R_TIME>(player);))

REGISTER(stick, skill::STICK, REQUIRE(RESOURCE(R_IRON, 0.5F);),
         DSL(use_resource<0.5F, R_IRON>(player);),
         DSL(write_attack<1, PHYSICAL>(player);));

REGISTER(drill, skill::DRILL, REQUIRE(RESOURCE(R_IRON, 1.5F);),
         DSL(use_resource<1.5F, R_IRON>(player);),
         DSL(write_attack<3, PHYSICAL>(player);));

REGISTER(slash, skill::SLASH, REQUIRE(RESOURCE(R_IRON, 2.5F);),
         DSL(use_resource<2.5F, R_IRON>(player);),
         DSL(write_attack<5, PHYSICAL>(player);));

REGISTER_BATCH(shield, skill::SHIELD,
               REQUIRE_BATCH(RESOURCE(R_IRON, 0.0F + (0.5F * N));),
               DSL_BATCH(use_resource<R_IRON>(0.0F + (0.5F * N), player);),
               DSL_BATCH(write_defense({.type_ = defense_type::COMMON_REDUCTION,
                                        .power_ = 2 + N,
                                        .defender_ = default_reduction},
                                       player);))
REGISTER_BATCH(thornshield, skill::THORN_SHIELD,
               REQUIRE_BATCH(RESOURCE(R_IRON, 1.0F + (0.5F * N));),
               DSL_BATCH(use_resource<R_IRON>(1.0F + (0.5F * N), player);),
               DSL_BATCH(write_defense({.type_ = defense_type::THORN_REDUCTION,
                                        .power_ = 4 + N,
                                        .defender_ = thorn_reduction},
                                       player);))
REGISTER_BATCH(recovery, skill::RECOVERY,
               REQUIRE_BATCH(RESOURCE(R_IRON, 0.5F + (0.5F * N))),
               DSL_BATCH(use_resource<R_IRON>(0.5F + (0.5F * N), player);),
               DSL_BATCH(write_recovery(1 + N, player);))

namespace {
template <auto data_getter, auto if_reflect>
void reflect_helper(community &player, community &enemy) {
    auto &pc = data_getter(player);
    auto &ec = data_getter(enemy);
    for (const auto &data : ec) {
        if (if_reflect(data)) {
            pc.push_back(data);
            pc.back().clock_.delayed_rounds_ = 1;
        }
    }
    std::erase_if(ec, if_reflect);
}
} // namespace

REGISTER(
    reflect, skill::REFLECT, REQUIRE(RESOURCE(R_SPACE, 2)),
    DSL(use_resource<2.0F, R_SPACE>(player)),
    DSL(write_callback<reflect_helper<[](community &com) -> auto & {
                           return com.focus_.turn_context_.effect_context_
                               .datas_;
                       },
                                      [](const effect_data &data) {
                                          return data.clock_.is_ringing() &&
                                                 data.type_ ==
                                                     effect_target_type::ENEMY;
                                      }>,
                       {}, callback_stage::BEFORE_APPLY_EFFECT>(player);
        write_callback<reflect_helper<[](community &com) -> auto & {
                           return com.focus_.turn_context_.attack_context_
                               .datas_;
                       },
                                      [](const attack_data &data) {
                                          return data.clock_.is_ringing();
                                      }>,
                       {}, callback_stage::BEFORE_APPLY_ATTACK>(player)))

namespace {
template <auto data_getter>
void delay_protection_helper(community & /*player*/, community &enemy) {
    auto &ec = data_getter(enemy);
    for (auto &data : ec) {
        if (data.clock_.is_ringing()) {
            data.clock_.delayed_rounds_ += 3;
        }
    }
}
} // namespace

REGISTER(
    delayprotection, skill::DELAY_PROTECTION, REQUIRE(RESOURCE(R_TIME, 2)),
    DSL(use_resource<2.0F, R_TIME>(player)),
    DSL(write_callback<delay_protection_helper<[](community &com) -> auto & {
                           return com.focus_.turn_context_.effect_context_
                               .datas_;
                       }>,
                       {}, callback_stage::BEFORE_APPLY_EFFECT>(player);
        write_callback<delay_protection_helper<[](community &com) -> auto & {
                           return com.focus_.turn_context_.attack_context_
                               .datas_;
                       }>,
                       {}, callback_stage::BEFORE_APPLY_ATTACK>(player)))

REGISTER(
    armor12, skill::ARMOR12, REQUIRE(RESOURCE(R_IRON, 7)),
    DSL(use_resource<7.0F, R_IRON>(player)),
    DSL(write_defense<{.id_ = defense_id::ARMOR12,
                       .type_ = defense_type::COMMON_ARMOR,
                       .power_ = 12,
                       .clock_ = {.is_infinite_ = true},
                       .defender_ = default_armor,
                       .can_merge_ = true,
                       .merge_ = [](defense_entity &a, defense_entity & /*b*/) {
                           a.power_ = 12;
                       }}>(player)))
namespace {
const std::vector<skill> PROFESSION_SKILLS{skill::WARLOCK, skill::CANNON,
                                           skill::DRIVER, skill::LANCER,
                                           skill::BLOODSIGIL};
}
REGISTER(warlock, skill::WARLOCK, REQUIRE(RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(write_profession<&get_warlock>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill(PROFESSION_SKILLS);
                 player.focus_.profession_.have_extra_profession_ = true;
             }>(player)))

REGISTER(cannon, skill::CANNON, REQUIRE(RESOURCE(R_IRON, 4.0F)),
         DSL(use_resource<4.0F, R_IRON>(player)),
         DSL(write_profession<&get_cannon>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill(PROFESSION_SKILLS);
                 player.focus_.profession_.have_extra_profession_ = true;
             }>(player)))

REGISTER(driver, skill::DRIVER, REQUIRE(RESOURCE(R_IRON, 3.0F)),
         DSL(use_resource<3.0F, R_IRON>(player)),
         DSL(write_profession<&get_driver>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill(PROFESSION_SKILLS);
                 player.focus_.profession_.have_extra_profession_ = true;
             }>(player)))

REGISTER(lancer, skill::LANCER, REQUIRE(RESOURCE(R_IRON, 3.0F)),
         DSL(use_resource<3.0F, R_IRON>(player)),
         DSL(write_profession<&get_lancer>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill(PROFESSION_SKILLS);
                 player.focus_.profession_.have_extra_profession_ = true;
             }>(player)))

REGISTER(bloodsigil, skill::BLOODSIGIL, REQUIRE(RESOURCE(R_IRON, 7.0F)),
         DSL(use_resource<7.0F, R_IRON>(player)),
         DSL(write_profession<&get_lancer>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill(PROFESSION_SKILLS);
                 player.focus_.profession_.have_extra_profession_ = true;
                 player.focus_.profession_.disable_skill(skill::STICK);
                 player.focus_.profession_.disable_skill(skill::DRILL);
                 player.focus_.profession_.disable_skill(skill::SLASH);
             }>(player)))
END(common)