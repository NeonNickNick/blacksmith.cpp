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
               DSL(write_defense<{defense_type::COMMON_REDUCTION,
                                  2 + N,
                                  {},
                                  default_reduction,
                                  default_update}>(player);))
REGISTER_BATCH(thornshield, REQUIRE(RESOURCE(IRON, 1.0F + (0.5F * N));),
               DSL(use_resource<1.0F + (0.5F * N), IRON>(player);),
               DSL(write_defense<{defense_type::THORN_REDUCTION,
                                  4 + N,
                                  {},
                                  thorn_reduction,
                                  default_update}>(player);))
REGISTER_BATCH(recovery, REQUIRE(RESOURCE(IRON, 0.5F + (0.5F * N))),
               DSL(use_resource<0.5F + (0.5F * N), IRON>(player);),
               DSL(write_recovery<1 + N>(player);))

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

END(common)