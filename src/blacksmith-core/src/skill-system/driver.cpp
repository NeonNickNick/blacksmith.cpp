#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>
#include <string>

BEGIN_PROFESSION
namespace {
const std::string DRIVER_MARK = "driver";
template <int power> void driver_attack(community &player) {
    modify<attack_data, write_attack<power, PHYSICAL>,
           [](attack_data &data, community &player) {
               data.power_ += take_mark(player, DRIVER_MARK);
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
                                    player.focus_.resource_.query(TIME));
                    }>(player)))

REGISTER_BATCH(spaceattack, REQUIRE(RESOURCE(SPACE, N)),
               DSL(use_resource<static_cast<float>(N), SPACE>(player)),
               DSL(driver_attack<12 * N>(player)))

REGISTER(space2time, REQUIRE(RESOURCE(SPACE, 1)),
         DSL(use_resource<1.0F, SPACE>(player)),
         DSL(write_resource<1.0F, TIME>(player);
             write_mark(player, DRIVER_MARK)))

REGISTER(time2space, REQUIRE(RESOURCE(TIME, 1)),
         DSL(use_resource<1.0F, TIME>(player)),
         DSL(write_resource<1.0F, SPACE>(player);
             write_mark(player, DRIVER_MARK)))

REGISTER_BATCH(
    spacebarrier, REQUIRE(1 <= N && N <= 5 && RESOURCE(IRON, N)),
    DSL(use_resource<static_cast<float>(N), IRON>(player)),
    DSL(write_defense<{.type_ = defense_type::REAL_REDUCTION,
                       .power_ = static_cast<int>(5.5F * N - 0.5F * N * N),
                       .clock_ = {},
                       .defender_ = default_reduction,
                       .update_ = default_update}>(player)))

END(driver)