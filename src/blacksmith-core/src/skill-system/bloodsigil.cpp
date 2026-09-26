/*#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>
#include <string>

BEGIN_PROFESSION
namespace  {
template<int power, float suck_rate>
void bloodsigil_attack(community& player){
    write_attack<power, PHYSICAL, {}, &execute_attack<{.}>>(player)
}
}
REGISTER(bloodblade, REQUIRE(HP(5)), DSL(use_resource<4, R_HP>(player)),
         DSL(write_attack<6, PHYSICAL>(player)))

END(bloodsigil)*/