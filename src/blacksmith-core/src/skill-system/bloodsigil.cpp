#include <cmath>
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_PROFESSION
namespace {
constexpr mark_id BLOODSIGIL = mark_id::BLOODSIGIL;
template <int power, float suck_rate>
void bloodsigil_attack(community &player) {
    modify<attack_data,
           &write_attack<
               power, PHYSICAL, {},
               &execute_attack<{.first_time_hit_armor_ =
                                    [](community &player, community & /*enemy*/,
                                       attack_data &data) {
                                        write_recovery(
                                            static_cast<int>(
                                                std::ceil(static_cast<float>(
                                                              data.power_) *
                                                          suck_rate)),
                                            player);
                                    }}>>,
           [](attack_data &data, community &player) {
               data.power_ = static_cast<int>(
                   static_cast<float>(data.power_) *
                   std::pow(1.5F, take_mark<BLOODSIGIL>(player)));
           }>(player);
}
} // namespace
REGISTER(bloodblade, skill::BLOOD_BLADE, REQUIRE(HP(5)),
         DSL(use_resource<4.0F, R_HP>(player)),
         DSL(bloodsigil_attack<6, 0.75F>(player)))

REGISTER(bloodlust, skill::BLOOD_LUST, REQUIRE(HP(3)),
         DSL(use_resource<2.0F, R_HP>(player)),
         DSL(write_mark<BLOODSIGIL>(player)))

REGISTER_(bloodrecovery, skill::BLOOD_RECOVERY, REQUIRE_(NOTHING), DSL_(),
          DSL(write_recovery<1>(player)))

REGISTER(bloodrage, skill::BLOOD_RAGE, REQUIRE(HP(2) && !HP(6)),
         DSL(use_resource<1.0F, R_HP>(player)),
         DSL(bloodsigil_attack<5, 1.5F>(player)))

END(bloodsigil)