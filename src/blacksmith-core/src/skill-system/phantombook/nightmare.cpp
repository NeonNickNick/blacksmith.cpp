#include "skill-system/skill-sets.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_SKILL_SET

REGISTER(dreamdive, skill::DREAM_DIVE, REQUIRE(RESOURCE(R_DREAM, 1.0F)),
         DSL(use_resource<1.0F, R_DREAM>(player)),
         DSL(write_attack<2, REAL>(player);
             write_defense<{.type_ = defense_type::COMMON_REDUCTION,
                            .power_ = 5,
                            .defender_ = default_reduction}>(player);
             write_defense<{.type_ = defense_type::MAGICAL_IMMUNITY,
                            .power_ = 1,
                            .defender_ = magical_immunity}>(player);
             write_recovery<1>(player); write_resource<1.0F, R_SPIRIT>(player)))

REGISTER(materialization, skill::MATERIALIZATION,
         REQUIRE(RESOURCE(R_DREAM, 1.0F) && RESOURCE(R_SPIRIT, 2.0F)),
         DSL(use_resource<1.0F, R_DREAM>(player);
             use_resource<2.0F, R_SPIRIT>(player)),
         DSL(write_attack<4, PHYSICAL>(player); write_attack<4, REAL>(player);
             write_defense<{.type_ = defense_type::COMMON_REDUCTION,
                            .power_ = 4,
                            .defender_ = default_reduction}>(player)))

REGISTER(haunt, skill::HAUNT, REQUIRE(RESOURCE(R_SPIRIT, 1.0F)),
         DSL(use_resource<1.0F, R_SPIRIT>(player)),
         DSL(write_attack<2, MAGICAL>(player);
             write_attack<2, MAGICAL, {.delayed_rounds_ = 1}>(player);
             write_attack<1, MAGICAL, {.delayed_rounds_ = 2}>(player)))

REGISTER(channeling, skill::CHANNELING, REQUIRE(RESOURCE(R_SPIRIT, 1.0F)),
         DSL(use_resource<1.0F, R_SPIRIT>(player)),
         DSL(write_recovery<3>(player);
             write_defense<{.type_ = defense_type::COMMON_REDUCTION,
                            .power_ = 2,
                            .defender_ = default_reduction}>(player);
             write_defense<{.type_ = defense_type::MAGICAL_IMMUNITY,
                            .power_ = 1,
                            .defender_ = magical_immunity}>(player)))

END(nightmare)