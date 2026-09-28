#include "skill-system/professions.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_PROFESSION

REGISTER_(iron, REQUIRE_(NOTHING), DSL_(),
          DSL(write_resource<1.0F, IRON>(player);))

REGISTER(space, REQUIRE(RESOURCE(IRON, 3.0F)),
         DSL(use_resource<3.0F, IRON>(player);),
         DSL(write_resource<1.0F, SPACE>(player);))

REGISTER(time, REQUIRE(RESOURCE(IRON, 3.0F)),
         DSL(use_resource<3.0F, IRON>(player);),
         DSL(write_resource<1.0F, TIME>(player);))

REGISTER(stick, REQUIRE(RESOURCE(IRON, 0.5F);),
         DSL(use_resource<0.5F, IRON>(player);),
         DSL(write_attack<1, PHYSICAL>(player);));

REGISTER(drill, REQUIRE(RESOURCE(IRON, 1.5F);),
         DSL(use_resource<1.5F, IRON>(player);),
         DSL(write_attack<3, PHYSICAL>(player);));

REGISTER(slash, REQUIRE(RESOURCE(IRON, 2.5F);),
         DSL(use_resource<2.5F, IRON>(player);),
         DSL(write_attack<5, PHYSICAL>(player);));

REGISTER_BATCH(shield, REQUIRE(RESOURCE(IRON, 0.0F + (0.5F * N));),
               DSL(use_resource<0.0F + (0.5F * N), IRON>(player);),
               DSL(write_defense<{.type_ = defense_type::COMMON_REDUCTION,
                                  .power_ = 2 + N,
                                  .defender_ = default_reduction}>(player);))
REGISTER_BATCH(thornshield, REQUIRE(RESOURCE(IRON, 1.0F + (0.5F * N));),
               DSL(use_resource<1.0F + (0.5F * N), IRON>(player);),
               DSL(write_defense<{.type_ = defense_type::THORN_REDUCTION,
                                  .power_ = 4 + N,
                                  .defender_ = thorn_reduction}>(player);))
REGISTER_BATCH(recovery, REQUIRE(RESOURCE(IRON, 0.5F + (0.5F * N))),
               DSL(use_resource<0.5F + (0.5F * N), IRON>(player);),
               DSL(write_recovery<1 + N>(player);))

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
    reflect, REQUIRE(RESOURCE(SPACE, 2)),
    DSL(use_resource<2.0F, SPACE>(player)),
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
    delayprotection, REQUIRE(RESOURCE(TIME, 2)),
    DSL(use_resource<2.0F, TIME>(player)),
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
    armor12, REQUIRE(RESOURCE(IRON, 7)), DSL(use_resource<7.0F, IRON>(player)),
    DSL(write_defense<{.id_ = defense_id::ARMOR12,
                       .type_ = defense_type::COMMON_ARMOR,
                       .power_ = 12,
                       .clock_ = {.is_infinite_ = true},
                       .defender_ = default_armor,
                       .can_merge_ = true,
                       .merge_ = [](defense_entity &a, defense_entity & /*b*/) {
                           a.power_ = 12;
                       }}>(player)))

REGISTER(warlock, REQUIRE(RESOURCE(IRON, 1.0F)),
         DSL(use_resource<1.0F, IRON>(player)),
         DSL(write_profession<&get_warlock>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill("warlock");
             }>(player)))

REGISTER(cannon, REQUIRE(RESOURCE(IRON, 4.0F)),
         DSL(use_resource<4.0F, IRON>(player)),
         DSL(write_profession<&get_cannon>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill("cannon");
             }>(player)))

REGISTER(driver, REQUIRE(RESOURCE(IRON, 3.0F)),
         DSL(use_resource<3.0F, IRON>(player)),
         DSL(write_profession<&get_driver>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill("driver");
             }>(player)))

REGISTER(lancer, REQUIRE(RESOURCE(IRON, 3.0F)),
         DSL(use_resource<3.0F, IRON>(player)),
         DSL(write_profession<&get_lancer>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill("lancer");
             }>(player)))

END(common)