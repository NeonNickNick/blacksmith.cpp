#include "skill-system/professions.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_PROFESSION

REGISTER(midastouch, skill::MIDASTOUCH,
         REQUIRE(RESOURCE_COMMON_ONLY(R_IRON, 1)),
         DSL(use_resource<1.0F, R_IRON, true>(player)),
         DSL(write_resource<5.0F, R_GOLD_IRON>(player)))

END(alchemy)