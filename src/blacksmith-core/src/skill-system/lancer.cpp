#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>
#include <string>

BEGIN_PROFESSION
namespace {
const std::string SKY_STRIKE = "skystrike";
const std::string TYRANT_DESTRUCTION = "tyrantdestruction";
const std::string DRAGON_TOOTH = "dragontooth";
const std::string TRIPLE_STAB = "triplestab";
const std::string CHARGE = "charge";

template <int power, attack_hook_set hookset, float ap_factor = 1.0F>
void lancer_attack(community &player) {
    modify<attack_data,
           &write_attack<power, PHYSICAL, {}, &execute_attack<hookset>>,
           [](attack_data &data, community &player) {
               data.power_ += 2 * take_mark(player, SKY_STRIKE);
           }>(player);
    if (take_mark(player, TYRANT_DESTRUCTION)) {
        write_recovery<2>(player);
    }
    if (take_mark(player, DRAGON_TOOTH)) {
        write_defense<{.type_ = defense_type::COMMON_ARMOR,
                       .power_ = 2,
                       .clock_ = {.is_infinite_ = true},
                       .defender_ = default_armor,
                       .update_ = default_update}>(player);
    }
    if (take_mark(player, TRIPLE_STAB)) {
        use_resource<1.0F, R_MHP>(player);
        write_attack<1, REAL, {.delayed_rounds_ = 0}>(player);
        write_attack<1, REAL, {.delayed_rounds_ = 1}>(player);
    }
}
} // namespace

REGISTER(skystrike, REQUIRE(RESOURCE(IRON, 1)),
         DSL(use_resource<1.0F, IRON>(player)),
         DSL(lancer_attack<3, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark(pc, SKY_STRIKE);
                                   }}>(player)))

REGISTER(tyrantdestruction, REQUIRE(RESOURCE(IRON, 1)),
         DSL(use_resource<1.0F, IRON>(player)),
         DSL(lancer_attack<3,
                           {.first_time_hit_armor_ =
                                [](community &pc, community & /*ec*/,
                                   attack_data & /*atk*/) {
                                    write_mark(pc, TYRANT_DESTRUCTION);
                                }},
                           2.0F>(player)))

REGISTER(dragontooth, REQUIRE(RESOURCE(IRON, 1)),
         DSL(use_resource<1.0F, IRON>(player)),
         DSL(lancer_attack<3, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark(pc, DRAGON_TOOTH);
                                   }}>(player);
             write_defense<{defense_type::COMMON_REDUCTION,
                            3,
                            {},
                            default_reduction,
                            default_update}>(player);))

REGISTER(triplestab, REQUIRE(RESOURCE(IRON, 1)),
         DSL(use_resource<1.0F, IRON>(player)),
         DSL(lancer_attack<2, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark(pc, TRIPLE_STAB);
                                   }}>(player);
             lancer_attack<2, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark(pc, TRIPLE_STAB);
                                   }}>(player);
             lancer_attack<1, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark(pc, TRIPLE_STAB);
                                   }}>(player);))

REGISTER(charge, REQUIRE([](const community &player) {
             auto cnt = count_mark(player, CHARGE);
             return cnt < 2 && RESOURCE(IRON, cnt > 0 ? 0.0F : 4.0F);
         }(player)),
         DSL(
             if (take_mark(player, CHARGE) > 0) {
                 use_resource<0.0F, IRON>(player);
             } else { use_resource<4.0F, IRON>(player); }),
         DSL_())

END(lancer)