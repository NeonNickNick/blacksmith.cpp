#include "skill-system/professions.hpp"
#include <algorithm>
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_PROFESSION
namespace {
constexpr mark_id SKY_STRIKE = mark_id::SKY_STRIKE;
constexpr mark_id TYRANT_DESTRUCTION = mark_id::TYRANT_DESTRUCTION;
constexpr mark_id DRAGON_TOOTH = mark_id::DRAGON_TOOTH;
constexpr mark_id TRIPLE_STAB = mark_id::TRIPLE_STAB;
constexpr mark_id CHARGE = mark_id::CHARGE;
constexpr mark_id COUNTER_ATTACK = mark_id::COUNTER_ATTACK;

template <int power, attack_hook_set hookset = {}, float ap_factor = 1.0F>
attack_data &lancer_attack(community &player) {
    auto &base = modify<
        attack_data,
        &write_attack<power, PHYSICAL, {}, &execute_attack<hookset>, ap_factor>,
        [](attack_data &data, community &player) {
            data.power_ += 2 * take_mark<SKY_STRIKE>(player);
        }>(player);
    if (take_mark<TYRANT_DESTRUCTION>(player)) {
        write_recovery<2>(player);
    }
    if (take_mark<DRAGON_TOOTH>(player)) {
        write_defense<{.type_ = defense_type::COMMON_ARMOR,
                       .power_ = 2,
                       .clock_ = {.is_infinite_ = true},
                       .defender_ = default_armor}>(player);
    }
    if (take_mark<TRIPLE_STAB>(player)) {
        use_resource<1.0F, R_MHP>(player);
        write_attack<1, REAL, {.delayed_rounds_ = 0}>(player);
        write_attack<1, REAL, {.delayed_rounds_ = 1}>(player);
    }
    return base;
}
} // namespace

REGISTER(skystrike, skill::SKY_STRIKE, REQUIRE(RESOURCE(R_IRON, 1)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(lancer_attack<3, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark<SKY_STRIKE>(pc);
                                   }}>(player)))

REGISTER(tyrantdestruction, skill::TYRANT_DESTURCTION,
         REQUIRE(RESOURCE(R_IRON, 1)), DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(lancer_attack<3,
                           {.first_time_hit_armor_ =
                                [](community &pc, community & /*ec*/,
                                   attack_data & /*atk*/) {
                                    write_mark<TYRANT_DESTRUCTION>(pc);
                                }},
                           2.0F>(player)))

REGISTER(dragontooth, skill::DRAGON_TOOTH, REQUIRE(RESOURCE(R_IRON, 1)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(lancer_attack<3, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark<DRAGON_TOOTH>(pc);
                                   }}>(player);
             write_defense<{.type_ = defense_type::COMMON_REDUCTION,
                            .power_ = 3,
                            .defender_ = default_reduction}>(player);))

REGISTER(triplestab, skill::TRIPLE_STAB, REQUIRE(RESOURCE(R_IRON, 1)),
         DSL(use_resource<1.0F, R_IRON>(player)),
         DSL(lancer_attack<2, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark<TRIPLE_STAB>(pc);
                                   }}>(player);
             lancer_attack<2, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark<TRIPLE_STAB>(pc);
                                   }}>(player);
             lancer_attack<1, {.first_time_hit_armor_ =
                                   [](community &pc, community & /*ec*/,
                                      attack_data & /*atk*/) {
                                       write_mark<TRIPLE_STAB>(pc);
                                   }}>(player);))

REGISTER(risingdragon, skill::RISING_DRAGON,
         REQUIRE([](const community &player) {
             auto cnt = count_mark<CHARGE>(player);
             return RESOURCE(R_IRON, cnt > 0 ? 0.0F : 4.0F);
         }(player)),
         DSL(if (count_mark<CHARGE>(player) == 0) {
             use_resource<4.0F, R_IRON>(player);
         }),
         DSL(modify<attack_data, &lancer_attack<9>,
                    [](attack_data &data, community &player) {
                        data.power_ += 4 * take_mark<CHARGE>(player);
                        data.type_ = MAGICAL;
                    }>(player)))

REGISTER(charge, skill::CHARGE, REQUIRE([](const community &player) {
             auto cnt = count_mark<CHARGE>(player);
             return cnt < 2 && RESOURCE(R_IRON, cnt > 0 ? 0.0F : 4.0F);
         }(player)),
         DSL(if (count_mark<CHARGE>(player) == 0) {
             use_resource<4.0F, R_IRON>(player);
         }),
         DSL(write_mark<CHARGE>(player);
             write_callback<[](community &player, community &enemy) {
                 const auto &datas =
                     enemy.focus_.turn_context_.attack_context_.datas_;
                 auto it = std::ranges::find_if(datas, [](const auto &data) {
                     return data.clock_.is_ringing();
                 });
                 if (it == datas.end()) {
                     return;
                 }
                 write_mark<COUNTER_ATTACK>(player);
                 modify<attack_data, &lancer_attack<9>,
                        [](attack_data &data, community &player) {
                            data.power_ += 4 * take_mark<CHARGE>(player);
                            data.type_ = MAGICAL;
                        }>(player);
             },
                            {}, callback_stage::BEFORE_CANCEL_ATTACK>(player);
             write_callback<[](community &player, community & /*enemy*/) {
                 if (player.current_skill_ != skill::CHARGE &&
                     take_mark<COUNTER_ATTACK>(player) <= 0) {
                     take_mark<CHARGE>(player);
                 }
             },
                            {.delayed_rounds_ = 1},
                            callback_stage::BEFORE_APPLY_EFFECT>(player)))

END(lancer)