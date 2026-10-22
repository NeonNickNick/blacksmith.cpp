#include "skill-system/skill-sets.hpp"
#include <algorithm>
#include <cstddef>
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>
#include <utility>
#include <vector>

BEGIN_SKILL_SET

REGISTER(fantasia, skill::FANTASIA, REQUIRE(RESOURCE(R_IRON, 0.5F)),
         DSL(use_resource<0.5F, R_IRON>(player)),
         DSL(write_resource<1.0F, R_DREAM>(player)))

REGISTER_CHAIN(
    association, skill::ASSOCIATION,
    REQUIRE_CHAIN(next && RESOURCE(R_DREAM, 2.0F) &&
                  [&player, &next]() {
                      if (next->skill_ == skill::ASSOCIATION) {
                          return false;
                      }
                      auto *opp = player.opponent_;
                      auto opp_result = opp->focus_.skills_.check(
                          {.action_ = (*next).copy(), .self_ = opp});
                      auto player_result = player.focus_.skills_.check(
                          {.action_ = (*next).copy(),
                           .self_ = &const_cast<community &>(player)});
                      return opp_result == check_result::SUCCESS &&
                             player_result != check_result::SUCCESS;
                  }()),
    DSL(use_resource<2.0F, R_DREAM>(player)),
    DSL_CAPTURE(skill_context copied{.action_ = next->copy(),
                                    .self_ = &p};
                const auto NONE_COST_DECLARE =
                    get_none_cost_skill_declare_mapping()[static_cast<size_t>(
                        next->skill_)];
                NONE_COST_DECLARE(copied);))

REGISTER(hallucination, skill::HALLUCINATION, REQUIRE(RESOURCE(R_DREAM, 2.0F)),
         DSL(use_resource<2.0F, R_DREAM>(player)),
         DSL(write_effect<effect_target_type::ENEMY,
                          {.clock_ = {},
                           .take_effect_ = [](community &player,
                                              community & /*enemy*/,
                                              effect_entity & /*effect*/) {
                               auto delay_attack = [](attack_data &attack) {
                                   ++attack.clock_.delayed_rounds_;
                               };
                               auto &context =
                                   player.focus_.turn_context_.attack_context_;
                               for (auto &attack : context.datas_) {
                                   if (!attack.clock_.is_dead()) {
                                       delay_attack(attack);
                                   }
                               }
                               context.register_preprocess(delay_attack);
                           }}>(player)))

REGISTER(dreamwake, skill::DREAM_WAKE, REQUIRE(RESOURCE(R_DREAM, 2.0F)), DSL_(),
         DSL(write_callback<[](community &player, community &enemy) {
             community restored{&enemy};
             community opponent{&restored};
             initialize(restored, opponent);
             // History includes this round's declared action.
             for (std::size_t i = 0; i + 4 < player.history_.size(); ++i) {
                 skill_context first{.action_ = player.history_[i].copy(),
                                     .self_ = &restored};
                 skill_context second{.action_ = enemy.history_[i].copy(),
                                      .self_ = &opponent};
                 declare(restored, first, opponent, second);
                 judge(restored, opponent);
             }
             auto &resource = restored.focus_.resource_;
             resource.use(R_DREAM, std::min(2.0F, resource.query(R_DREAM)));
             restored.current_skill_ = player.current_skill_;
             restored.history_ = std::move(player.history_);
             player = std::move(restored);
         },
                            {}, callback_stage::BEFORE_END>(player)))

REGISTER(illusion, skill::ILLUSION, REQUIRE(RESOURCE(R_DREAM, 1.0F)),
         DSL(use_resource<1.0F, R_DREAM>(player)),
         DSL(write_recovery<5>(player)))

namespace {

void update_nightmare(community &player, defense_entity &defense) {
    default_update(player, defense);
    if (defense.power_ <= 0) {
        defense.clock_.set_dead();
        player.focus_.skills_.enable_skill(skill::NIGHTMARE);
        player.focus_.skills_.remove_skill_set(SKILL_SET(nightmare));
    }
}

} // namespace

REGISTER(nightmare, skill::NIGHTMARE, REQUIRE(RESOURCE(R_DREAM, 1.0F) && HP(5)),
         DSL(use_resource<1.0F, R_DREAM>(player);
             use_resource<5.0F, R_HP>(player)),
         DSL(write_defense<{.type_ = defense_type::PHYSICAL_IMMUNITY,
                            .power_ = 1,
                            .defender_ = physical_immunity}>(player);
             write_defense<{.id_ = defense_id::NIGHTMARE_ARMOR,
                            .type_ = defense_type::REAL_ARMOR,
                            .power_ = 6,
                            .clock_ = {.is_infinite_ = true},
                            .defender_ = default_armor,
                            .update_ = update_nightmare}>(player);
             write_skill_set<&get_nightmare>(player);
             player.focus_.skills_.disable_skill(skill::NIGHTMARE)))

END(phantombook)
