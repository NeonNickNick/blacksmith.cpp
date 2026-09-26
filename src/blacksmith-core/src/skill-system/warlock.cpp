#include "skill-system/professions.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_PROFESSION

REGISTER(magic, REQUIRE(RESOURCE(IRON, 1.0F)),
         DSL(use_resource<1.0F, IRON>(player);),
         DSL(write_resource<1.0F, MAGIC>(player);))

REPEAT_BATCH(
    magicattack,
    DSL(write_attack<2 * N, MAGICAL, {.delayed_rounds_ = index}>(player)))

REGISTER_BATCH(magicattack, REQUIRE(N > 0 && RESOURCE(MAGIC, N)),
               DSL(use_resource<static_cast<float>(N), MAGIC>(player);),
               DSL(magicattack<N, 3>(player);))

REPEAT_BATCH(magicshield,
             DSL(write_defense<{.type_ = defense_type::REAL_REDUCTION,
                                .power_ = 3 * N,
                                .clock_ = {},
                                .defender_ = default_reduction,
                                .update_ = default_update},
                               {.delayed_rounds_ = index}>(player);))

REGISTER_BATCH(magicshield, REQUIRE(N > 0 && RESOURCE(MAGIC, N)),
               DSL(use_resource<static_cast<float>(N), MAGIC>(player);),
               DSL(magicshield<N, 3>(player)))

REGISTER(sacrifice, REQUIRE(MHP(2)), DSL(use_resource<1.0F, R_MHP>(player);),
         DSL(write_resource<1.5F, IRON>(player);
             write_defense<{.type_ = defense_type::REAL_REDUCTION,
                            .power_ = 7,
                            .clock_ = {},
                            .defender_ = default_reduction,
                            .update_ = default_update}>(player);))

REGISTER_(mute, REQUIRE_(NOTHING), DSL_(),
          DSL(write_effect<
              effect_target_type::ENEMY,
              effect_entity{
                  .clock_ = {},
                  .take_effect_ = [](community & /*player*/, community &enemy,
                                     effect_entity & /*entity*/) {
                      std::erase_if(
                          enemy.focus_.turn_context_.resource_context_.datas_,
                          [](resource_data &r) {
                              return r.type_ == TIME || r.type_ == SPACE;
                          });
                  }}>(player)))

REGISTER(alchemy, REQUIRE(RESOURCE(IRON, 2.5F)),
         DSL(use_resource<2.5F, IRON>(player)),
         DSL(write_profession<&get_alchemy>(player);
             write_free<[](community &player) {
                 player.focus_.profession_.disable_skill("alchemy");
             }>(player)))

END(warlock)