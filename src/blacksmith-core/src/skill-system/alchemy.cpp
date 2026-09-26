#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_PROFESSION

REGISTER(midastouch, REQUIRE(RESOURCE_COMMON_ONLY(IRON, 1)),
         DSL(use_resource<1.0F, IRON, true>(player)),
         DSL(write_resource<5.0F, GOLD_IRON>(player)))

END(alchemy)