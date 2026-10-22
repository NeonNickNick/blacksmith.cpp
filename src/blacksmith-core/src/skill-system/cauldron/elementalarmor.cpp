#include "skill-system/skill-sets.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>
BEGIN_SKILL_SET

REGISTER_(hammer, skill::HAMMER, REQUIRE_(NOTHING), DSL_(),
          DSL(write_attack<6, PHYSICAL>(player)))

REGISTER_(guard, skill::GUARD, REQUIRE_(NOTHING), DSL_(),
          DSL(write_defense<{.type_ = defense_type::COMMON_REDUCTION,
                             .power_ = 8,
                             .defender_ = default_reduction}>(player)))

END(elementalarmor)