#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include "skill-system/skill-sets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace blacksmith::domain;
using namespace blacksmith::skill_system;

namespace {
void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void equal(float actual, float expected, const char *message) {
    expect(std::abs(actual - expected) < 1e-6F, message);
}

bool has_skill(const community &player, skill move) {
    const auto &skills = player.focus_.skills_.authorized_skills();
    return std::ranges::find(skills, move) != skills.end();
}

update_func observed_armor_update = nullptr;
int observed_dead_updates = 0;

void count_dead_armor_updates(community &player, defense_entity &defense) {
    observed_armor_update(player, defense);
    if (defense.clock_.is_dead()) {
        ++observed_dead_updates;
    }
}

void observe_armor_update(community &player, defense_id id, defense_type type) {
    auto &defenses = player.focus_.defense_.defenses_;
    const auto armor = std::ranges::find_if(defenses, [=](const auto &defense) {
        return defense.id_ == id && defense.type_ == type;
    });
    expect(armor != defenses.end() && armor->update_ != nullptr,
           "[observe_armor_update] Expected armor update hook was missing");
    observed_armor_update = armor->update_;
    observed_dead_updates = 0;
    armor->update_ = count_dead_armor_updates;
}

struct battle {
    community players_[2]{community(&players_[1]), community(&players_[0])};

    battle() { initialize(players_[0], players_[1]); }
    battle(const battle &other) : battle() { *this = other; }
    battle &operator=(const battle &) = default;

    void step(skill first, skill second, bool reverse_declare = false,
              bool reverse_judge = false) {
        skill_context a{.action_ = {.skill_ = first}, .self_ = &players_[0]};
        skill_context b{.action_ = {.skill_ = second}, .self_ = &players_[1]};
        expect(check_skill(players_[0], a) == check_result::SUCCESS,
               "Seat 0 action rejected");
        expect(check_skill(players_[1], b) == check_result::SUCCESS,
               "Seat 1 action rejected");
        if (reverse_declare) {
            declare(players_[1], b, players_[0], a);
        } else {
            declare(players_[0], a, players_[1], b);
        }
        if (reverse_judge) {
            judge(players_[1], players_[0]);
        } else {
            judge(players_[0], players_[1]);
        }
        expect(players_[0].opponent_ == &players_[1] &&
                   players_[1].opponent_ == &players_[0],
               "Round resolution changed the battle's opponents");
    }
};

void prepare_phantombook(battle &game, int iron_rounds = 10,
                         int dream_rounds = 6) {
    for (int i = 0; i < iron_rounds; ++i) {
        game.step(skill::IRON, skill::IRON);
    }
    game.step(skill::PHANTOMBOOK, skill::IRON);
    for (int i = 0; i < dream_rounds; ++i) {
        game.step(skill::FANTASIA, skill::IRON);
    }
}

void community_history_keeps_actions() {
    battle game;
    auto &first = game.players_[0];
    auto &second = game.players_[1];
    skill_context a{
        .action_ = {.skill_ = skill::IRON,
                    .param_ = 7,
                    .next_ = std::make_unique<skill_action>(
                        skill_action{.skill_ = skill::IRON, .param_ = 3})},
        .self_ = &first};
    skill_context b{.action_ = {.skill_ = skill::IRON, .param_ = 9},
                    .self_ = &second};
    declare(second, b, first, a);
    judge(first, second);
    a.action_.next_->param_ = 20;
    expect(first.history_.size() == 1 && second.history_.size() == 1 &&
               first.history_[0].param_ == 7 &&
               first.history_[0].next_->param_ == 3 &&
               second.history_[0].param_ == 9,
           "[community_history_keeps_actions] History lost action ownership "
           "or parameters");
    auto branch = game;
    branch.players_[0].history_[0].next_->param_ = 11;
    battle assigned;
    assigned = game;
    assigned.players_[0].history_[0].next_->param_ = 22;
    branch.step(skill::IRON, skill::IRON);
    expect(first.history_.size() == 1 && first.history_[0].next_->param_ == 3 &&
               branch.players_[0].history_.size() == 2,
           "[community_history_keeps_actions] Copied history changed its "
           "source");
}

void reversed_round_order() {
    battle game;
    auto &first = game.players_[0];
    auto &second = game.players_[1];
    first.focus_.skills_.add_skill_set(SKILL_SET(bloodsigil));
    second.focus_.skills_.add_skill_set(SKILL_SET(crossbow));
    second.focus_.resource_.gain(resource_type::BOLT, 1.0F);
    game.step(skill::IRON, skill::AIM);
    game.step(skill::BLOOD_BLADE, skill::CRITICAL, true, true);
    expect(first.focus_.health_.hp_ == 9,
           "Reversed judge did not apply true damage before capped healing");
    expect(first.history_.size() == 2 && second.history_.size() == 2 &&
               first.history_.back().skill_ == skill::BLOOD_BLADE &&
               second.history_.back().skill_ == skill::CRITICAL,
           "Reversed calls recorded actions for the wrong community");
}

void effect_owner_is_player() {
    constexpr effect_entity effect{
        .clock_ = {},
        .take_effect_ = [](community &player, community &enemy,
                           effect_entity & /*effect*/) {
            player.focus_.health_.lose_hp(1);
            enemy.focus_.health_.lose_hp(2);
        }};
    for (const auto target :
         std::array{effect_target_type::SELF, effect_target_type::ENEMY}) {
        for (const bool reverse : std::array{false, true}) {
            battle game;
            auto &player = game.players_[0];
            auto &enemy = game.players_[1];
            if (target == effect_target_type::SELF) {
                write_effect<effect_target_type::SELF, effect>(player);
            } else {
                write_effect<effect_target_type::ENEMY, effect>(player);
            }
            if (reverse) {
                apply_effect(enemy, player);
                take_effect(enemy, player);
            } else {
                apply_effect(player, enemy);
                take_effect(player, enemy);
            }
            const bool self = target == effect_target_type::SELF;
            expect(player.focus_.health_.hp_ == (self ? 9 : 8) &&
                       enemy.focus_.health_.hp_ == (self ? 8 : 9),
                   "Effect callback did not receive its owner as player");
        }
    }
}

void seat_one_rewind_and_branches() {
    battle game;
    for (int i = 0; i < 10; ++i) {
        game.step(skill::IRON, skill::IRON);
    }
    game.step(skill::IRON, skill::PHANTOMBOOK);
    for (int i = 0; i < 6; ++i) {
        game.step(skill::IRON, skill::FANTASIA);
    }
    auto branch = game;
    branch.step(skill::IRON, skill::DREAM_WAKE);
    const auto &rewound = branch.players_[1];
    equal(rewound.focus_.resource_.query(resource_type::DREAM), 1.0F,
          "Seat 1 DreamWake did not charge restored Dream");
    equal(rewound.focus_.resource_.query(resource_type::IRON), 6.0F,
          "Seat 1 DreamWake restored the wrong round");
    equal(branch.players_[0].focus_.resource_.query(resource_type::IRON), 18.0F,
          "Seat 1 DreamWake rewound its opponent");
    expect(game.players_[1].history_.size() == 17 &&
               rewound.history_.size() == 18 &&
               rewound.history_.back().skill_ == skill::DREAM_WAKE,
           "Branch rewind truncated history or changed its source");
    equal(game.players_[1].focus_.resource_.query(resource_type::DREAM), 6.0F,
          "Branch rewind mutated the original body");
    for (int i = 0; i < 3; ++i) {
        branch.step(skill::IRON, skill::IRON);
    }
    branch.step(skill::IRON, skill::FANTASIA);
    branch.step(skill::IRON, skill::DREAM_WAKE);
    equal(branch.players_[1].focus_.resource_.query(resource_type::DREAM), 0.0F,
          "Nested DreamWake did not limit payment to restored Dream");
    equal(branch.players_[1].focus_.resource_.query(resource_type::IRON), 7.0F,
          "Nested DreamWake restored the wrong round");
    expect(game.players_[1].history_.size() == 17 &&
               branch.players_[1].history_.size() == 23,
           "Nested rewind changed full action history");
}

void nightmare_break_in_attack_phase() {
    battle game;
    auto &player = game.players_[0];
    auto &enemy = game.players_[1];
    player.focus_.skills_.add_skill_set(SKILL_SET(phantombook));
    player.focus_.resource_.gain(resource_type::DREAM, 1.0F);
    write_attack<20, attack_type::PHYSICAL>(enemy);
    game.step(skill::NIGHTMARE, skill::IRON);
    expect(player.focus_.health_.hp_ == 5,
           "Nightmare did not grant physical immunity on its equip round");

    write_attack<6, attack_type::MAGICAL>(enemy);
    write_attack<2, attack_type::PHYSICAL>(enemy);
    game.step(skill::IRON, skill::IRON);
    expect(player.focus_.health_.hp_ == 3,
           "Nightmare immunity survived armor breaking earlier in the phase");
    expect(has_skill(player, skill::NIGHTMARE) &&
               !has_skill(player, skill::DREAM_DIVE),
           "Broken Nightmare armor did not restore its skill package");
}

void dreamwake_after_current_round() {
    battle game;
    prepare_phantombook(game);
    auto &player = game.players_[0];
    write_callback<[](community &self, community & /*enemy*/) {
        expect(self.focus_.health_.hp_ == 7,
               "[dreamwake_after_current_round] Rewind ran before the "
               "current round's incoming attack");
    },
                   {}, callback_stage::BEFORE_ROUND_PASS>(player);
    game.step(skill::DREAM_WAKE, skill::DRILL);
    expect(player.focus_.health_.hp_ == 10,
           "[dreamwake_after_current_round] End-of-round rewind did not "
           "replace this round's damage");
    equal(player.focus_.resource_.query(resource_type::DREAM), 1.0F,
          "[dreamwake_after_current_round] Rewind charged the wrong body");
    equal(player.focus_.resource_.query(resource_type::IRON), 6.0F,
          "[dreamwake_after_current_round] Rewind restored the wrong round");
    expect(player.history_.size() == 18 &&
               player.current_skill_ == skill::DREAM_WAKE,
           "[dreamwake_after_current_round] Rewind replaced the current "
           "action or history");

    battle unavailable;
    prepare_phantombook(unavailable, 10, 1);
    skill_context context{.action_ = {.skill_ = skill::DREAM_WAKE},
                          .self_ = &unavailable.players_[0]};
    expect(
        check_skill(unavailable.players_[0], context) != check_result::SUCCESS,
        "[dreamwake_after_current_round] Insufficient Dream allowed rewind");
}

void dreamwake_resumes_pending_attacks() {
    battle game;
    prepare_phantombook(game);
    auto &enemy = game.players_[1];
    game.step(skill::NIGHTMARE, skill::IRON);
    game.step(skill::DREAM_DIVE, skill::IRON);
    game.step(skill::HAUNT, skill::IRON);
    for (int i = 0; i < 3; ++i) {
        game.step(skill::IRON, skill::IRON);
    }
    const int before = enemy.focus_.health_.hp_;
    game.step(skill::DREAM_WAKE, skill::IRON);
    expect(enemy.focus_.health_.hp_ == before,
           "[dreamwake_resumes_pending_attacks] Restored attack fired on "
           "the rewind round");
    game.step(skill::IRON, skill::IRON);
    expect(enemy.focus_.health_.hp_ == before - 2,
           "[dreamwake_resumes_pending_attacks] Restored attack did not "
           "resume on the next round");
    game.step(skill::IRON, skill::IRON);
    expect(enemy.focus_.health_.hp_ == before - 3,
           "[dreamwake_resumes_pending_attacks] Restored delayed attack used "
           "the wrong clock");
}

void dreamwake_replays_batch_parameters() {
    battle game;
    prepare_phantombook(game);
    auto &player = game.players_[0];
    auto &enemy = game.players_[1];
    skill_context a{.action_ = {.skill_ = skill::SHIELD, .param_ = 2},
                    .self_ = &player};
    skill_context b{.action_ = {.skill_ = skill::DRILL}, .self_ = &enemy};
    declare(player, a, enemy, b);
    judge(player, enemy);
    game.step(skill::IRON, skill::DRILL);
    game.step(skill::IRON, skill::IRON);
    game.step(skill::IRON, skill::IRON);
    expect(player.focus_.health_.hp_ == 7,
           "[dreamwake_replays_batch_parameters] Incoming damage setup failed");
    game.step(skill::DREAM_WAKE, skill::IRON);
    expect(
        player.focus_.health_.hp_ == 10 && player.history_[17].param_ == 2,
        "[dreamwake_replays_batch_parameters] Replay lost a batch parameter");
    equal(player.focus_.resource_.query(resource_type::IRON), 3.5F,
          "[dreamwake_replays_batch_parameters] Replayed batch cost changed");
}

void dreamwake_restores_hallucination_preprocess() {
    battle game;
    for (int i = 0; i < 10; ++i) {
        game.step(skill::IRON, skill::IRON);
    }
    game.step(skill::PHANTOMBOOK, skill::PHANTOMBOOK);
    for (int i = 0; i < 6; ++i) {
        game.step(skill::FANTASIA, skill::FANTASIA);
    }
    auto &player = game.players_[0];
    auto &enemy = game.players_[1];
    game.step(skill::IRON, skill::HALLUCINATION);
    for (int i = 0; i < 3; ++i) {
        game.step(skill::IRON, skill::IRON);
    }
    game.step(skill::DREAM_WAKE, skill::IRON);
    const auto &attack = write_attack<1, attack_type::PHYSICAL>(player);
    expect(attack.clock_.delayed_rounds_ == 1,
           "[dreamwake_restores_hallucination_preprocess] Rewind lost or "
           "duplicated the callback");
    game.step(skill::IRON, skill::IRON);
    expect(enemy.focus_.health_.hp_ == 10,
           "[dreamwake_restores_hallucination_preprocess] Restored callback "
           "did not delay the attack");
    game.step(skill::IRON, skill::IRON);
    expect(enemy.focus_.health_.hp_ == 9,
           "[dreamwake_restores_hallucination_preprocess] Restored effect "
           "delayed the attack twice");
}

void simultaneous_dreamwake() {
    battle game;
    for (int i = 0; i < 10; ++i) {
        game.step(skill::IRON, skill::IRON);
    }
    game.step(skill::PHANTOMBOOK, skill::PHANTOMBOOK);
    for (int i = 0; i < 6; ++i) {
        game.step(skill::FANTASIA, skill::FANTASIA);
    }
    game.step(skill::DREAM_WAKE, skill::DREAM_WAKE);
    for (const auto &player : game.players_) {
        equal(player.focus_.resource_.query(resource_type::DREAM), 1.0F,
              "[simultaneous_dreamwake] Rewind lost one of the two effects");
        expect(player.history_.size() == 18,
               "[simultaneous_dreamwake] Rewind truncated history");
    }
}

void markshot_survives_reflect() {
    battle game;
    auto &player = game.players_[0];
    auto &enemy = game.players_[1];
    player.focus_.skills_.add_skill_set(SKILL_SET(crossbow));
    player.focus_.resource_.gain(resource_type::BOLT, 3.0F);
    player.focus_.resource_.gain(resource_type::IRON, 1.0F);
    enemy.focus_.resource_.gain(resource_type::SPACE, 2.0F);
    game.step(skill::AIM, skill::IRON);
    game.step(skill::MARK_SHOT, skill::REFLECT);
    game.step(skill::CRITICAL, skill::IRON);
    expect(
        enemy.focus_.health_.hp_ == 6 && player.focus_.health_.hp_ == 9,
        "[markshot_survives_reflect] Reflect transported or delayed the mark");
    game.step(skill::CRITICAL, skill::IRON);
    expect(enemy.focus_.health_.hp_ == 4 && player.focus_.health_.hp_ == 9,
           "[markshot_survives_reflect] Reflected mark changed a later round");
}

void nightmare_lethal_payment() {
    battle game;
    auto &player = game.players_[0];
    player.focus_.skills_.add_skill_set(SKILL_SET(phantombook));
    player.focus_.health_.hp_ = 5;
    player.focus_.resource_.gain(resource_type::DREAM, 1.0F);
    game.step(skill::NIGHTMARE, skill::IRON);
    expect(player.focus_.health_.hp_ == 0 &&
               has_skill(player, skill::DREAM_DIVE),
           "[nightmare_lethal_payment] Nightmare rejected a lethal HP payment");
    equal(player.focus_.resource_.query(resource_type::DREAM), 0.0F,
          "[nightmare_lethal_payment] Nightmare did not charge one Dream");
}

void repeated_rewinds() {
    battle game;
    prepare_phantombook(game, 20, 12);
    auto before = game;
    constexpr std::array dreams{7.0F, 8.0F, 9.0F, 10.0F, 5.0F, 6.0F};
    constexpr std::array irons{13.0F, 12.5F, 12.0F, 11.5F, 13.0F, 12.5F};
    for (std::size_t i = 0; i < dreams.size(); ++i) {
        game.step(skill::DREAM_WAKE, skill::IRON);
        equal(game.players_[0].focus_.resource_.query(resource_type::DREAM),
              dreams[i], "Repeated rewinds lost their cumulative Dream cost");
        equal(game.players_[0].focus_.resource_.query(resource_type::IRON),
              irons[i], "Repeated rewinds reconstructed the wrong round");
    }
    expect(game.players_[0].history_.size() == 39 &&
               game.players_[1].history_.size() == 39 &&
               before.players_[0].history_.size() == 33,
           "Repeated rewinds truncated history or changed its copy");
    equal(game.players_[1].focus_.resource_.query(resource_type::IRON), 39.0F,
          "Repeated rewinds changed enemy resources");
}

void elemental_break_in_attack_phase() {
    battle game;
    auto &player = game.players_[0];
    auto &enemy = game.players_[1];
    player.focus_.skills_.add_skill_set(SKILL_SET(cauldron));
    player.focus_.resource_.gain(resource_type::EARTH, 1.0F);
    player.focus_.resource_.gain(resource_type::WATER, 1.0F);
    game.step(skill::ELEMENTAL_ARMOR, skill::IRON);

    // Hammer cancels 6 of the first attack. The remaining 6 breaks the armor,
    // which still blocks all physical damage until the round ends.
    write_attack<12, attack_type::MAGICAL>(enemy);
    write_attack<2, attack_type::PHYSICAL>(enemy);
    game.step(skill::HAMMER, skill::IRON);
    expect(player.focus_.health_.hp_ == 10,
           "Elemental armor lost physical immunity during its breaking round");
    expect(has_skill(player, skill::IRON) && has_skill(player, skill::FIRE) &&
               !has_skill(player, skill::HAMMER) &&
               !has_skill(player, skill::GUARD) &&
               player.focus_.defense_.defenses_.empty(),
           "Broken elemental armor did not restore its original skill package");
}
void elemental_armor_restores_from_update() {
    for (const bool natural_expiry : std::array{true, false}) {
        battle game;
        auto &player = game.players_[0];
        auto &enemy = game.players_[1];
        player.focus_.skills_.add_skill_set(SKILL_SET(cauldron));
        player.focus_.resource_.gain(resource_type::EARTH, 1.0F);
        player.focus_.resource_.gain(resource_type::WATER, 1.0F);
        game.step(skill::ELEMENTAL_ARMOR, skill::IRON);
        expect(player.focus_.turn_context_.callback_context_.datas_.empty(),
               "[elemental_armor_restores_from_update] Armor installed a "
               "global callback");
        observe_armor_update(player, defense_id::ELEMENTAL_ARMOR,
                             defense_type::COMMON_ARMOR);
        if (natural_expiry) {
            for (int i = 0; i < 3; ++i) {
                game.step(skill::GUARD, skill::IRON);
            }
        } else {
            write_attack<6, attack_type::MAGICAL>(enemy);
            apply_attack(player, enemy);
            expect(has_skill(player, skill::HAMMER) &&
                       !has_skill(player, skill::IRON),
                   "[elemental_armor_restores_from_update] Armor restored "
                   "before update");
            player.focus_.defense_.round_pass(player);
        }
        expect(observed_dead_updates == 1 && has_skill(player, skill::IRON) &&
                   has_skill(player, skill::FIRE) &&
                   !has_skill(player, skill::HAMMER) &&
                   player.focus_.defense_.defenses_.empty() &&
                   player.focus_.turn_context_.callback_context_.datas_.empty(),
               "[elemental_armor_restores_from_update] Update did not restore "
               "and remove armor once");
        for (int i = 0; i < 3; ++i) {
            round_pass(player, enemy);
        }
        expect(observed_dead_updates == 1,
               "[elemental_armor_restores_from_update] A dead armor update ran "
               "again");
    }
}

void nightmare_armor_restores_from_update() {
    battle game;
    auto &player = game.players_[0];
    auto &enemy = game.players_[1];
    player.focus_.skills_.add_skill_set(SKILL_SET(phantombook));
    player.focus_.resource_.gain(resource_type::DREAM, 10.0F);
    game.step(skill::NIGHTMARE, skill::IRON);
    expect(player.focus_.turn_context_.callback_context_.datas_.empty(),
           "[nightmare_armor_restores_from_update] PhantomBook installed a "
           "passive callback");
    observe_armor_update(player, defense_id::NIGHTMARE_ARMOR,
                         defense_type::REAL_ARMOR);
    write_attack<6, attack_type::MAGICAL>(enemy);
    apply_attack(player, enemy);
    expect(!has_skill(player, skill::NIGHTMARE) &&
               has_skill(player, skill::DREAM_DIVE),
           "[nightmare_armor_restores_from_update] Nightmare restored before "
           "update");
    player.focus_.defense_.round_pass(player);
    expect(observed_dead_updates == 1 && has_skill(player, skill::NIGHTMARE) &&
               !has_skill(player, skill::DREAM_DIVE) &&
               player.focus_.defense_.defenses_.empty() &&
               player.focus_.turn_context_.callback_context_.datas_.empty(),
           "[nightmare_armor_restores_from_update] Armor hook did not "
           "immediately restore its entry");
    for (int i = 0; i < 3; ++i) {
        round_pass(player, enemy);
    }
    expect(
        observed_dead_updates == 1,
        "[nightmare_armor_restores_from_update] A dead armor update ran again");
}

void nightmare_break_while_skills_disabled() {
    for (const bool break_both : std::array{false, true}) {
        battle game;
        auto &player = game.players_[0];
        auto &enemy = game.players_[1];
        player.focus_.resource_.gain(resource_type::IRON, 5.5F);
        player.focus_.resource_.gain(resource_type::DREAM, 20.0F);
        player.focus_.resource_.gain(resource_type::EARTH, 1.0F);
        player.focus_.resource_.gain(resource_type::WATER, 1.0F);
        game.step(skill::PHANTOMBOOK, skill::IRON);
        game.step(skill::CAULDRON, skill::IRON);
        game.step(skill::NIGHTMARE, skill::IRON);
        game.step(skill::ELEMENTAL_ARMOR, skill::IRON);
        write_attack<attack_type::MAGICAL>(break_both ? 18 : 12, enemy);
        game.step(skill::HAMMER, skill::IRON);
        if (!break_both) {
            expect(player.focus_.skills_.authorized_skills().size() == 2 &&
                       has_skill(player, skill::HAMMER) &&
                       has_skill(player, skill::GUARD) &&
                       !has_skill(player, skill::NIGHTMARE) &&
                       !has_skill(player, skill::DREAM_DIVE),
                   "[nightmare_break_while_skills_disabled] Broken armor "
                   "leaked its entry into the active mode");
            game.step(skill::GUARD, skill::IRON);
            game.step(skill::GUARD, skill::IRON);
        }
        expect(has_skill(player, skill::NIGHTMARE) &&
                   has_skill(player, skill::IRON) &&
                   has_skill(player, skill::FIRE) &&
                   !has_skill(player, skill::DREAM_DIVE) &&
                   !has_skill(player, skill::MATERIALIZATION) &&
                   !has_skill(player, skill::HAUNT) &&
                   !has_skill(player, skill::CHANNELING) &&
                   !has_skill(player, skill::HAMMER) &&
                   !has_skill(player, skill::GUARD) &&
                   player.focus_.skills_.profession_ ==
                       profession_type::PHANTOMBOOK &&
                   player.focus_.turn_context_.callback_context_.datas_.empty(),
               "[nightmare_break_while_skills_disabled] Restored skills "
               "retained a broken child package");
    }
}
void nightmare_lock_survives_elemental_expiry() {
    battle game;
    auto &player = game.players_[0];
    auto &enemy = game.players_[1];
    player.focus_.health_ = {.hp_ = 100, .mhp_ = 100};
    player.focus_.resource_.gain(resource_type::IRON, 5.5F);
    player.focus_.resource_.gain(resource_type::DREAM, 20.0F);
    player.focus_.resource_.gain(resource_type::EARTH, 2.0F);
    player.focus_.resource_.gain(resource_type::WATER, 2.0F);
    game.step(skill::PHANTOMBOOK, skill::IRON);
    game.step(skill::CAULDRON, skill::IRON);
    for (int cycle = 0; cycle < 2; ++cycle) {
        game.step(skill::NIGHTMARE, skill::IRON);
        game.step(skill::ELEMENTAL_ARMOR, skill::IRON);
        for (int i = 0; i < 3; ++i) {
            game.step(skill::GUARD, skill::IRON);
        }
        expect(!has_skill(player, skill::NIGHTMARE) &&
                   has_skill(player, skill::DREAM_DIVE) &&
                   has_skill(player, skill::IRON) &&
                   !has_skill(player, skill::HAMMER),
               "[nightmare_lock_survives_elemental_expiry] Mode exit released "
               "Nightmare's own lock");
        write_attack<6, attack_type::MAGICAL>(enemy);
        game.step(skill::IRON, skill::IRON);
        expect(has_skill(player, skill::NIGHTMARE) &&
                   !has_skill(player, skill::DREAM_DIVE) &&
                   !has_skill(player, skill::MATERIALIZATION) &&
                   !has_skill(player, skill::HAUNT) &&
                   !has_skill(player, skill::CHANNELING) &&
                   !has_skill(player, skill::PHANTOMBOOK) &&
                   !has_skill(player, skill::WARLOCK) &&
                   !has_skill(player, skill::CAULDRON),
               "[nightmare_lock_survives_elemental_expiry] Repeated equipment "
               "unbalanced skill locks");
    }
}

void simultaneous_armor_break_with_reversed_updates() {
    battle game;
    auto &player = game.players_[0];
    auto &enemy = game.players_[1];
    player.focus_.resource_.gain(resource_type::IRON, 5.5F);
    player.focus_.resource_.gain(resource_type::DREAM, 10.0F);
    player.focus_.resource_.gain(resource_type::EARTH, 1.0F);
    player.focus_.resource_.gain(resource_type::WATER, 1.0F);
    game.step(skill::PHANTOMBOOK, skill::IRON);
    game.step(skill::CAULDRON, skill::IRON);
    game.step(skill::NIGHTMARE, skill::IRON);
    game.step(skill::ELEMENTAL_ARMOR, skill::IRON);
    write_attack<12, attack_type::MAGICAL>(enemy);
    apply_attack(player, enemy);
    std::ranges::reverse(player.focus_.defense_.defenses_);
    round_pass(player, enemy);
    expect(has_skill(player, skill::NIGHTMARE) &&
               has_skill(player, skill::IRON) &&
               has_skill(player, skill::FIRE) &&
               !has_skill(player, skill::DREAM_DIVE) &&
               !has_skill(player, skill::MATERIALIZATION) &&
               !has_skill(player, skill::HAUNT) &&
               !has_skill(player, skill::CHANNELING) &&
               !has_skill(player, skill::HAMMER) &&
               !has_skill(player, skill::GUARD) &&
               player.focus_.defense_.defenses_.empty(),
           "[simultaneous_armor_break_with_reversed_updates] Restoration "
           "depended on armor update order");
}
} // namespace

int main() {
    try {
        community_history_keeps_actions();
        reversed_round_order();
        effect_owner_is_player();
        seat_one_rewind_and_branches();
        repeated_rewinds();
        dreamwake_after_current_round();
        dreamwake_resumes_pending_attacks();
        dreamwake_replays_batch_parameters();
        dreamwake_restores_hallucination_preprocess();
        simultaneous_dreamwake();
        markshot_survives_reflect();
        nightmare_break_in_attack_phase();
        nightmare_lethal_payment();
        elemental_break_in_attack_phase();
        elemental_armor_restores_from_update();
        nightmare_armor_restores_from_update();
        nightmare_break_while_skills_disabled();
        nightmare_lock_survives_elemental_expiry();
        simultaneous_armor_break_with_reversed_updates();
        std::cout << "Profession interaction checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
