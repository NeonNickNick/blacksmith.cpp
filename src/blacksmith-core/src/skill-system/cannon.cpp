#include "skill-system/professions.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_PROFESSION
namespace {
constexpr mark_id CANNON = mark_id::CANNON;
template <int power, float ap_factor = 1.0F>
void cannon_attack(community &player) {
    const auto NUM = take_mark<CANNON>(player);
    if (NUM > 0) {
        write_attack<power + 1, PHYSICAL, {}, execute_attack, ap_factor>(
            player);
    } else {
        write_attack<power, PHYSICAL, {}, execute_attack, ap_factor>(player);
    }
}
} // namespace
REGISTER(strike, skill::STRIKE, REQUIRE(RESOURCE(R_IRON, 1)),
         DSL(use_resource<1.0F, R_IRON>(player)), DSL(cannon_attack<4>(player)))

REGISTER(doublestrike, skill::DOUBLE_STRIKE, REQUIRE(RESOURCE(R_IRON, 2)),
         DSL(use_resource<2.0F, R_IRON>(player)), DSL(cannon_attack<8>(player)))

REGISTER(triplestrike, skill::TRIPLE_STRIKE, REQUIRE(RESOURCE(R_IRON, 3)),
         DSL(use_resource<3.0F, R_IRON>(player)),
         DSL(cannon_attack<11>(player); write_mark<CANNON>(player);
             write_resource<0.5F, R_IRON>(player)))

REGISTER(apshell, skill::APSHELL, REQUIRE(RESOURCE(R_IRON, 1)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(cannon_attack<2, 3.0F>(player)))

REGISTER_(cannonbarrier, skill::CANNON_BARRIER, REQUIRE_(NOTHING), DSL_(),
          DSL(write_defense<{.type_ = defense_type::COMMON_REDUCTION,
                             .power_ = 2,
                             .clock_ = {},
                             .defender_ = default_reduction,
                             .update_ = default_update}>(player);
              cannon_attack<1>(player)))
END(cannon)