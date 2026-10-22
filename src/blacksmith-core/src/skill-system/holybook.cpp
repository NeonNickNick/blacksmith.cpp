#include "skill-system/skill-sets.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_SKILL_SET

REGISTER(cross, skill::CROSS, REQUIRE(RESOURCE(R_IRON, 0.5F)),
         DSL(use_resource<0.5F, R_IRON>(player)),
         DSL(write_resource<1.0F, R_CROSS>(player)))

REGISTER_(prayer, skill::PRAYER, REQUIRE_(NOTHING), DSL_(),
          DSL(write_defense<{.type_ = defense_type::COMMON_REDUCTION,
                             .power_ = 3,
                             .defender_ = default_reduction}>(player)))

REGISTER(ark, skill::ARK, REQUIRE(RESOURCE(R_CROSS, 2.0F)),
         DSL(use_resource<2.0F, R_CROSS>(player)),
         DSL(write_attack<8, PHYSICAL>(player);
             write_defense<{.type_ = defense_type::PERCENTAGE_REDUCTION,
                            .power_ = 50,
                            .defender_ = percentage_reduction}>(player)))

REGISTER(profanation, skill::PROFANATION, REQUIRE(RESOURCE(R_CROSS, 1.0F)),
         DSL(use_resource<1.0F, R_CROSS>(player)),
         DSL(const int n = count_mark<mark_id::PROFANATION>(player);
             write_mark<mark_id::PROFANATION>(player);
             write_attack<2, REAL>(player);
             write_defense({.type_ = defense_type::GREY_HP,
                            .power_ = 2 + (n + 2) / 3,
                            .defender_ = default_armor},
                           player)))

REPEAT_BATCH(rebirth,
             DSL(write_callback<[](community &player, community & /*enemy*/) {
                     write_recovery<3>(player);
                 },
                                {.delayed_rounds_ = index},
                                callback_stage::BEFORE_APPLY_DEFENSE>(player);
                 write_defense<{.delayed_rounds_ = index}>(
                     {.type_ = defense_type::PERCENTAGE_REDUCTION,
                      .power_ = 25,
                      .defender_ = percentage_reduction},
                     player)))

REGISTER(rebirth, skill::REBIRTH, REQUIRE(RESOURCE(R_CROSS, 1.0F)),
         DSL(use_resource<1.0F, R_CROSS>(player)), DSL(rebirth<3>(0, player)))

REGISTER(absolution, skill::ABSOLUTION, REQUIRE(RESOURCE(R_CROSS, 1.0F)),
         DSL(use_resource<1.0F, R_CROSS>(player)),
         DSL(write_defense<{.type_ = defense_type::REAL_REDUCTION,
                            .power_ = 1,
                            .clock_ = {.is_infinite_ = true},
                            .defender_ = default_reduction}>(player)))

END(holybook)
