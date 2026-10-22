#include "skill-system/skill-sets.hpp"
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>

BEGIN_SKILL_SET

REGISTER_BATCH(makebolts, skill::MAKE_BOLTS,
               REQUIRE_BATCH(RESOURCE(R_IRON, static_cast<float>(N))),
               DSL_BATCH(use_resource<R_IRON>(static_cast<float>(N), player)),
               DSL_BATCH(write_resource<R_BOLT>(static_cast<float>(3 * N),
                                                player)))

REGISTER_BATCH(arrowrain, skill::ARROW_RAIN,
               REQUIRE_BATCH(N > 0 && RESOURCE(R_BOLT, static_cast<float>(N))),
               DSL_BATCH(use_resource<R_BOLT>(static_cast<float>(N), player)),
               DSL_BATCH(write_attack<PHYSICAL>(N, player)))

REGISTER_(aim, skill::AIM, REQUIRE_(NOTHING), DSL_(),
          DSL(take_mark<mark_id::CROSSBOW_AIM>(player);
              write_mark<mark_id::CROSSBOW_AIM>(player)))

REGISTER(critical, skill::CRITICAL, REQUIRE(RESOURCE(R_BOLT, 1.0F)),
         DSL(use_resource<1.0F, R_BOLT>(player)),
         DSL(
             if (take_mark<mark_id::CROSSBOW_AIM>(player) > 0) {
                 write_attack<2, REAL>(player);
             } else {
                 write_attack<1, PHYSICAL>(player);
                 write_attack<1, REAL>(player);
             }))

REGISTER_BATCH(block, skill::BLOCK,
               REQUIRE_BATCH(N >= 1 && N <= 4 &&
                             RESOURCE(R_BOLT, 0.5F * static_cast<float>(N))),
               DSL_BATCH(use_resource<R_BOLT>(0.5F * static_cast<float>(N),
                                              player)),
               DSL_BATCH(write_defense({.type_ = defense_type::COMMON_REDUCTION,
                                        .power_ = N * (9 - N) / 2,
                                        .defender_ = default_reduction},
                                       player)))
namespace {
void mark_target(community &player, community & /*enemy*/) {
    for (auto &data : player.focus_.turn_context_.attack_context_.datas_) {
        if (data.clock_.is_ringing()) {
            data.body_damage_factor_ *= 2;
        }
    }
}
} // namespace
REGISTER(markshot, skill::MARK_SHOT,
         REQUIRE(RESOURCE(R_BOLT, 1.0F) && RESOURCE(R_IRON, 1.0F)),
         DSL(use_resource<1.0F, R_BOLT>(player);
             use_resource<1.0F, R_IRON>(player)),
         DSL(write_attack<1, PHYSICAL>(player);
             write_callback<&mark_target, {.delayed_rounds_ = 1},
                            callback_stage::BEFORE_APPLY_ATTACK>(player)))

END(crossbow)
