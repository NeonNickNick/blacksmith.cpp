#include "skill-system/professions.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_PROFESSION

REGISTER(magic, skill::MAGIC, REQUIRE(RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_IRON>(player);),
         DSL(write_resource<1.0F, R_MAGIC>(player);))

REPEAT_BATCH(
    magicattack,
    DSL(write_attack<2 * N, MAGICAL, {.delayed_rounds_ = index}>(player)))

REGISTER_BATCH(magicattack, skill::MAGIC_ATTACK,
               REQUIRE(N > 0 && RESOURCE(R_MAGIC, N)),
               DSL(use_resource<static_cast<float>(N), R_MAGIC>(player);),
               DSL(magicattack<N, 3>(player);))

REPEAT_BATCH(magicshield,
             DSL(write_defense<{.type_ = defense_type::REAL_REDUCTION,
                                .power_ = 3 * N,
                                .clock_ = {},
                                .defender_ = default_reduction,
                                .update_ = default_update},
                               {.delayed_rounds_ = index}>(player);))

REGISTER_BATCH(magicshield, skill::MAGIC_SHIELD,
               REQUIRE(N > 0 && RESOURCE(R_MAGIC, N)),
               DSL(use_resource<static_cast<float>(N), R_MAGIC>(player);),
               DSL(magicshield<N, 3>(player)))

REGISTER(sacrifice, skill::SACRIFICE, REQUIRE(MHP(2)),
         DSL(use_resource<1.0F, R_MHP>(player);),
         DSL(write_resource<1.5F, R_IRON>(player);
             write_defense<{.type_ = defense_type::REAL_REDUCTION,
                            .power_ = 7,
                            .clock_ = {},
                            .defender_ = default_reduction,
                            .update_ = default_update}>(player);))

REGISTER_(mute, skill::MUTE, REQUIRE_(NOTHING), DSL_(),
          DSL(write_effect<
              effect_target_type::ENEMY,
              effect_entity{
                  .clock_ = {},
                  .take_effect_ = [](community & /*player*/, community &enemy,
                                     effect_entity & /*entity*/) {
                      std::erase_if(
                          enemy.focus_.turn_context_.resource_context_.datas_,
                          [](resource_data &r) {
                              return r.type_ == R_TIME || r.type_ == R_SPACE;
                          });
                  }}>(player)))

REGISTER(alchemy, skill::ALCHEMY, REQUIRE(RESOURCE(R_IRON, 2.5F)),
         DSL(use_resource<2.5F, R_IRON>(player)),
         DSL(write_profession<&get_alchemy>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill(skill::ALCHEMY);
             }>(player)))

END(warlock)