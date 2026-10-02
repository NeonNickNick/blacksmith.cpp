#include "skill-system/professions.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_PROFESSION
namespace {
constexpr mark_id DRIVER = mark_id::DRIVER;
template <int power> void driver_attack(community &player) {
    modify<attack_data, write_attack<power, PHYSICAL>,
           [](attack_data &data, community &player) {
               data.power_ += take_mark<DRIVER>(player);
           }>(player);
}
} // namespace
PASSIVE_(REQUIRE_(NOTHING),
         DSL(modify<defense_data,
                    write_defense<{.type_ = defense_type::REAL_REDUCTION,
                                   .power_ = 1,
                                   .clock_ = {},
                                   .defender_ = default_reduction,
                                   .update_ = default_update}>,
                    [](defense_data &data, community &player) {
                        data.defense_.power_ +=
                            2 * static_cast<int>(
                                    player.focus_.resource_.query(R_TIME));
                    }>(player)))

REGISTER_BATCH(spaceattack, skill::SPACE_ATTACK, REQUIRE(RESOURCE(R_SPACE, N)),
               DSL(use_resource<static_cast<float>(N), R_SPACE>(player)),
               DSL(driver_attack<12 * N>(player)))

REGISTER(space2time, skill::SPACE2TIME, REQUIRE(RESOURCE(R_SPACE, 1)),
         DSL(use_resource<1.0F, R_SPACE>(player)),
         DSL(write_resource<1.0F, R_TIME>(player); write_mark<DRIVER>(player)))

REGISTER(time2space, skill::TIME2SPACE, REQUIRE(RESOURCE(R_TIME, 1)),
         DSL(use_resource<1.0F, R_TIME>(player)),
         DSL(write_resource<1.0F, R_SPACE>(player); write_mark<DRIVER>(player)))

REGISTER_BATCH(
    spacebarrier, skill::SPACE_BARRIER,
    REQUIRE(1 <= N && N <= 5 && RESOURCE(R_IRON, N)),
    DSL(use_resource<static_cast<float>(N), R_IRON>(player)),
    DSL(write_defense<{.type_ = defense_type::REAL_REDUCTION,
                       .power_ = static_cast<int>(5.5F * N - 0.5F * N * N),
                       .clock_ = {},
                       .defender_ = default_reduction,
                       .update_ = default_update}>(player)))

END(driver)