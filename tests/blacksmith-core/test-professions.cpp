#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include "skill-system/skill-sets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <iostream>
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

constexpr std::array ROOT_PROFESSIONS{
    skill::WARLOCK,     skill::CANNON,  skill::DRIVER,  skill::LANCER,
    skill::BLOODSIGIL,
    skill::PHANTOMBOOK, skill::CROSSBOW};
constexpr std::array ROOT_PROFESSION_COSTS{1.0F, 4.0F, 3.0F, 3.0F, 7.0F,
                                           2.5F, 2.0F};
constexpr std::array ROOT_PROFESSION_SKILLS{
    skill::MAGIC,      skill::STRIKE,      skill::SPACE_ATTACK,
    skill::SKY_STRIKE, skill::BLOOD_BLADE,
          skill::FANTASIA,
    skill::MAKE_BOLTS};

struct battle {
    community player_{&enemy_};
    community enemy_{&player_};
    battle() { initialize(player_, enemy_); }
    battle(const battle &other) : battle() { *this = other; }
    battle &operator=(const battle &) = default;
    bool legal(skill move, int param = 0) {
        skill_context context{.action_ = {.skill_ = move, .param_ = param},
                              .self_ = &player_};
        return check_skill(player_, context) == check_result::SUCCESS;
    }
    bool authorized(skill move) const {
        const auto &skills = player_.focus_.skills_.authorized_skills();
        return std::ranges::find(skills, move) != skills.end();
    }
    void step(skill move = skill::IRON, int param = 0,
              skill enemy_move = skill::IRON) {
        skill_context first{.action_ = {.skill_ = move, .param_ = param},
                            .self_ = &player_};
        skill_context second{.action_ = {.skill_ = enemy_move},
                             .self_ = &enemy_};
        expect(check_skill(player_, first) == check_result::SUCCESS,
               "Player action rejected");
        expect(check_skill(enemy_, second) == check_result::SUCCESS,
               "Enemy action rejected");
        declare(player_, first, enemy_, second);
        judge(player_, enemy_);
    }
    void acquire(skill move, float cost) {
        player_.focus_.resource_.gain(resource_type::IRON, cost);
        step(move);
        equal(player_.focus_.resource_.query(resource_type::IRON), 0.0F,
              "Acquisition cost mismatch");
    }
    void gain(resource_type type, float power = 10.0F) {
        player_.focus_.resource_.gain(type, power);
    }
    void incoming(int physical, int magical = 0, int real = 0) {
        if (physical > 0)
            write_attack<attack_type::PHYSICAL>(physical, enemy_);
        if (magical > 0)
            write_attack<attack_type::MAGICAL>(magical, enemy_);
        if (real > 0)
            write_attack<attack_type::REAL>(real, enemy_);
    }
};

void registrations() {
    for (std::size_t i = 0; i < ROOT_PROFESSIONS.size(); ++i) {
        battle game;
        game.acquire(ROOT_PROFESSIONS[i], ROOT_PROFESSION_COSTS[i]);
        game.gain(resource_type::IRON, 100.0F);
        for (const auto purchase : ROOT_PROFESSIONS) {
            expect(!game.legal(purchase), "A second profession is available");
        }
        expect(static_cast<std::size_t>(
                   game.player_.focus_.skills_.profession_) == i + 1,
               "Profession role mismatch");
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(skill::SIZE); ++i) {
        expect(get_skill_check_mapping()[i] && get_skill_declare_mapping()[i],
               "Unregistered skill");
    }
}

void skill_disable_counts() {
    battle game;
    auto &skills = game.player_.focus_.skills_;
    const auto initial_size = skills.authorized_skills().size();
    skills.disable_skill(skill::IRON);
    skills.disable_skill(skill::IRON);
    skills.enable_skill(skill::IRON);
    expect(!game.legal(skill::IRON),
           "[skill_disable_counts] One enable removed two independent locks");
    skills.enable_skill(skill::IRON);
    expect(game.legal(skill::IRON),
           "[skill_disable_counts] The final enable did not unlock the skill");
    skills.enable_skill(skill::IRON);
    expect(game.legal(skill::IRON) &&
               skills.authorized_skills().size() == initial_size,
           "[skill_disable_counts] An extra enable duplicated or disabled a "
           "skill");
    skills.disable_skill(skill::IRON);
    expect(!game.legal(skill::IRON), "[skill_disable_counts] Extra enable made "
                                     "the next disable ineffective");
    expect(!game.authorized(skill::MAGIC),
           "[skill_disable_counts] An unacquired skill started enabled");
    skills.enable_skill(skill::MAGIC);
    expect(
        game.authorized(skill::MAGIC),
        "[skill_disable_counts] Acquisition did not release the initial lock");
}

void bulk_disable_balances_skill_set_changes() {
    battle game;
    auto &skills = game.player_.focus_.skills_;
    const auto &exceptions = SKILL_SET(elementalarmor)->authorized_skills_;
    skills.add_skill_set(SKILL_SET(alchemy));
    skills.add_skill_set(SKILL_SET(cauldron));
    skills.lock_all_skills(exceptions);
    expect(!game.authorized(skill::MIDASTOUCH),
           "[bulk_disable_balances_skill_set_changes] An existing set escaped "
           "a mode lock");
    skills.add_skill_set(SKILL_SET(warlock));
    expect(!game.authorized(skill::MAGIC),
           "[bulk_disable_balances_skill_set_changes] A new set escaped an "
           "existing mode lock");
    skills.remove_skill_set(SKILL_SET(alchemy));
    skills.add_skill_set(SKILL_SET(alchemy));
    skills.remove_skill_set(SKILL_SET(cauldron));
    skills.lock_all_skills(exceptions);

    auto branch = game;
    branch.player_.focus_.skills_.release_all_skills(exceptions);
    branch.player_.focus_.skills_.release_all_skills(exceptions);
    expect(branch.authorized(skill::IRON) && branch.authorized(skill::MAGIC) &&
               branch.authorized(skill::MIDASTOUCH) &&
               !branch.authorized(skill::FIRE) &&
               !game.authorized(skill::IRON),
           "[bulk_disable_balances_skill_set_changes] A copied lock state "
           "changed its source or ownership");
    skills.release_all_skills(exceptions);
    expect(!game.authorized(skill::IRON) && !game.authorized(skill::MAGIC) &&
               !game.authorized(skill::MIDASTOUCH),
           "[bulk_disable_balances_skill_set_changes] One bulk enable removed "
           "two locks");
    skills.release_all_skills(exceptions);
    expect(game.authorized(skill::IRON) && game.authorized(skill::MAGIC) &&
               game.authorized(skill::MIDASTOUCH) &&
               !game.authorized(skill::FIRE),
           "[bulk_disable_balances_skill_set_changes] Bulk unlock resurrected "
           "a removed or unowned set");
}

void cauldron_coexists_with_professions() {
    for (std::size_t i = 0; i < ROOT_PROFESSIONS.size(); ++i) {
        for (const bool cauldron_first : std::array{true, false}) {
            battle game;
            if (cauldron_first) {
                game.acquire(skill::CAULDRON, 3.0F);
                expect(game.player_.focus_.skills_.profession_ ==
                               profession_type::NONE &&
                           !game.player_.focus_.skills_.has_profession_,
                       "[cauldron_coexists_with_professions] Equipment "
                       "selected a profession");
            }
            game.acquire(ROOT_PROFESSIONS[i], ROOT_PROFESSION_COSTS[i]);
            if (!cauldron_first) {
                game.acquire(skill::CAULDRON, 3.0F);
            }
            game.gain(resource_type::IRON, 100.0F);
            expect(static_cast<std::size_t>(
                       game.player_.focus_.skills_.profession_) == i + 1 &&
                       game.player_.focus_.skills_.has_profession_ &&
                       game.authorized(ROOT_PROFESSION_SKILLS[i]) &&
                       game.legal(skill::FIRE),
                   "[cauldron_coexists_with_professions] Equipment replaced a "
                   "profession");
            expect(!game.legal(skill::CAULDRON),
                   "[cauldron_coexists_with_professions] Cauldron allowed a "
                   "duplicate purchase");
            for (const auto purchase : ROOT_PROFESSIONS) {
                expect(!game.legal(purchase),
                       "[cauldron_coexists_with_professions] Equipment "
                       "reopened root professions");
            }
            expect(game.authorized(skill::ALCHEMY) ==
                       (ROOT_PROFESSIONS[i] == skill::WARLOCK),
                   "[cauldron_coexists_with_professions] Alchemy escaped the "
                   "Warlock node");
        }
    }

    battle unselected;
    unselected.gain(resource_type::IRON, 100.0F);
    expect(!unselected.authorized(skill::ALCHEMY),
           "[cauldron_coexists_with_professions] Root authorized Alchemy");
    unselected.step(skill::CAULDRON);
    expect(!unselected.authorized(skill::ALCHEMY) &&
               unselected.player_.focus_.skills_.profession_ ==
                   profession_type::NONE &&
               !unselected.player_.focus_.skills_.has_profession_,
           "[cauldron_coexists_with_professions] Cauldron authorized the "
           "Warlock subtree");
}

void equipment_coexists_and_restores_after_elemental_armor() {
    for (const bool alchemy_first : std::array{true, false}) {
        battle game;
        game.acquire(skill::WARLOCK, 1.0F);
        if (alchemy_first) {
            game.acquire(skill::ALCHEMY, 2.5F);
        }
        game.acquire(skill::CAULDRON, 3.0F);
        if (!alchemy_first) {
            game.acquire(skill::ALCHEMY, 2.5F);
        }
        game.gain(resource_type::IRON, 100.0F);
        game.gain(resource_type::EARTH, 1.0F);
        game.gain(resource_type::WATER, 1.0F);
        expect(game.legal(skill::MIDASTOUCH) && game.legal(skill::MAGIC) &&
                   game.legal(skill::FIRE) && !game.legal(skill::ALCHEMY) &&
                   !game.legal(skill::CAULDRON) &&
                   game.player_.focus_.skills_.profession_ ==
                       profession_type::WARLOCK,
               "[equipment_coexists_and_restores_after_elemental_armor] "
               "Equipment did not coexist");
        game.step(skill::ELEMENTAL_ARMOR);
        expect(game.authorized(skill::HAMMER) &&
                   game.authorized(skill::GUARD) &&
                   !game.authorized(skill::MAGIC) &&
                   !game.authorized(skill::MIDASTOUCH) &&
                   !game.authorized(skill::FIRE) &&
                   game.player_.focus_.skills_.profession_ ==
                       profession_type::WARLOCK,
               "[equipment_coexists_and_restores_after_elemental_armor] Mode "
               "changed identity or retained skills");
        for (int turn = 0; turn < 3; ++turn) {
            game.step(skill::GUARD);
        }
        expect(game.legal(skill::MAGIC) && game.legal(skill::MIDASTOUCH) &&
                   game.legal(skill::FIRE) && !game.authorized(skill::HAMMER) &&
                   !game.authorized(skill::GUARD) &&
                   !game.legal(skill::ALCHEMY) &&
                   !game.legal(skill::CAULDRON) &&
                   game.player_.focus_.skills_.profession_ ==
                       profession_type::WARLOCK,
               "[equipment_coexists_and_restores_after_elemental_armor] Mode "
               "did not restore all equipment");
        for (const auto purchase : ROOT_PROFESSIONS) {
            expect(!game.legal(purchase),
                   "[equipment_coexists_and_restores_after_elemental_armor] "
                   "Mode reopened root professions");
        }
    }
}

void defender_rounding() {
    battle percentages;
    for (int i = 0; i < 2; ++i) {
        write_defense<{.type_ = defense_type::PERCENTAGE_REDUCTION,
                       .power_ = 50,
                       .defender_ = percentage_reduction}>(percentages.player_);
    }
    percentages.incoming(3);
    percentages.step();
    expect(percentages.player_.focus_.health_.hp_ == 10,
           "[defender_rounding] Two half reductions did not round 3 to 1 to 0");

    battle penetration;
    for (int i = 0; i < 2; ++i) {
        write_defense<{.type_ = defense_type::COMMON_REDUCTION,
                       .power_ = 1,
                       .defender_ = default_reduction}>(penetration.player_);
    }
    auto &attack = write_attack<5, attack_type::PHYSICAL>(penetration.enemy_);
    attack.ap_factor_ = 1.5F;
    penetration.step();
    expect(penetration.player_.focus_.health_.hp_ == 5,
           "[defender_rounding] Each defense did not round both AP scaling "
           "steps up");

    battle armor;
    write_defense<{.type_ = defense_type::COMMON_ARMOR,
                   .power_ = 2,
                   .clock_ = {.is_infinite_ = true},
                   .defender_ = default_armor}>(armor.player_);
    auto &armored_attack = write_attack<1, attack_type::PHYSICAL>(armor.enemy_);
    armored_attack.ap_factor_ = 1.5F;
    armor.step();
    expect(armor.player_.focus_.health_.hp_ == 10 &&
               armor.player_.focus_.defense_.defenses_.size() == 1 &&
               armor.player_.focus_.defense_.defenses_[0].power_ == 0,
           "[defender_rounding] AP-scaled attack did not consume two armor");

    battle empty_armor;
    empty_armor.player_.focus_.health_ = {.hp_ = 100, .mhp_ = 100};
    write_defense<{.type_ = defense_type::COMMON_ARMOR,
                   .power_ = 0,
                   .defender_ = default_armor}>(empty_armor.player_);
    auto &empty_armor_attack =
        write_attack<15, attack_type::PHYSICAL>(empty_armor.enemy_);
    empty_armor_attack.ap_factor_ = 1.2F;
    empty_armor.step();
    expect(empty_armor.player_.focus_.health_.hp_ == 85,
           "[defender_rounding] Zero armor changed damage during AP scaling");

    battle fractional_ap;
    fractional_ap.player_.focus_.health_ = {.hp_ = 100, .mhp_ = 100};
    write_defense<{.type_ = defense_type::COMMON_ARMOR,
                   .power_ = 6,
                   .defender_ = default_armor}>(fractional_ap.player_);
    auto &fractional_ap_attack =
        write_attack<15, attack_type::PHYSICAL>(fractional_ap.enemy_);
    fractional_ap_attack.ap_factor_ = 1.2F;
    fractional_ap.step();
    expect(
        fractional_ap.player_.focus_.health_.hp_ == 90,
        "[defender_rounding] AP 1.2 with six armor did not leave ten damage");

    battle vulnerability;
    vulnerability.player_.focus_.health_ = {.hp_ = 100, .mhp_ = 100};
    write_defense<{.type_ = defense_type::PERCENTAGE_REDUCTION,
                   .power_ = -320,
                   .defender_ = percentage_reduction}>(vulnerability.player_);
    vulnerability.incoming(15);
    vulnerability.step();
    expect(vulnerability.player_.focus_.health_.hp_ == 37,
           "[defender_rounding] 320 percent vulnerability did not cause 63 "
           "damage");
}

void default_defense_update_lifetimes() {
    battle game;
    auto &defenses = game.player_.focus_.defense_.defenses_;
    defenses.push_back({.type_ = defense_type::COMMON_REDUCTION,
                        .power_ = 1,
                        .defender_ = default_reduction});
    defenses.push_back({.type_ = defense_type::COMMON_REDUCTION,
                        .power_ = 3,
                        .clock_ = {.remaining_rounds_ = 3},
                        .defender_ = default_reduction});
    defenses.push_back({.type_ = defense_type::COMMON_REDUCTION,
                        .power_ = 9,
                        .clock_ = {.is_infinite_ = true},
                        .defender_ = default_reduction});
    round_pass(game.player_, game.enemy_);
    expect(defenses.size() == 2 && defenses[0].power_ == 3 &&
               defenses[0].clock_.remaining_rounds_ == 2 &&
               defenses[1].power_ == 9 && defenses[1].clock_.is_ringing(),
           "[default_defense_update_lifetimes] The first update used the wrong "
           "lifetime");
    round_pass(game.player_, game.enemy_);
    expect(
        defenses.size() == 2 && defenses[0].clock_.remaining_rounds_ == 1,
        "[default_defense_update_lifetimes] Three-round defense expired early");
    round_pass(game.player_, game.enemy_);
    expect(defenses.size() == 1 && defenses[0].power_ == 9,
           "[default_defense_update_lifetimes] Three-round defense was not "
           "cleaned up");
    for (int i = 0; i < 3; ++i) {
        round_pass(game.player_, game.enemy_);
    }
    expect(defenses.size() == 1 && defenses[0].clock_.is_ringing(),
           "[default_defense_update_lifetimes] Infinite defense expired");
}

void context_preprocesses_before_storage() {
    turn_context_component context;
    auto &resources = context.resource_context_;
    resources.register_preprocess(
        [](resource_data &data) { data.power_ += 1.0F; });
    resources.register_preprocess(
        [](resource_data &data) { data.power_ *= 2.0F; });
    equal(resources
              .write(clap_round_clock{}, resource_type::IRON, 3.0F,
                     &execute_resource<>)
              .power_,
          8.0F, "[context_preprocesses_before_storage] Callback order changed");
    resources.round_pass();
    auto branch = resources;
    branch.register_preprocess(
        [](resource_data &data) { data.power_ += 5.0F; });
    equal(resources
              .write(clap_round_clock{}, resource_type::IRON, 3.0F,
                     &execute_resource<>)
              .power_,
          8.0F,
          "[context_preprocesses_before_storage] Copy changed its source "
          "or round update removed callbacks");
    equal(branch
              .write(clap_round_clock{}, resource_type::IRON, 3.0F,
                     &execute_resource<>)
              .power_,
          13.0F, "[context_preprocesses_before_storage] Copy lost callbacks");

    auto &rejected = context.attack_context_;
    rejected.register_preprocess([](attack_data & /*attack*/) {
        throw std::runtime_error("Reject attack before insertion");
    });
    bool threw = false;
    try {
        static_cast<void>(rejected.write(clap_round_clock{},
                                         attack_type::PHYSICAL, 1, 1.0F, false,
                                         &execute_attack<>, 0));
    } catch (const std::runtime_error &) {
        threw = true;
    }
    expect(threw && rejected.datas_.empty(),
           "[context_preprocesses_before_storage] Failed preprocessing "
           "inserted an attack");
}

void stacked_hallucination_preprocesses_once() {
    battle game;
    game.acquire(skill::PHANTOMBOOK, 2.5F);
    game.gain(resource_type::DREAM, 4.0F);
    game.step(skill::HALLUCINATION, 0, skill::STICK);
    game.step(skill::HALLUCINATION, 0, skill::STICK);
    expect(game.player_.focus_.health_.hp_ == 10,
           "[stacked_hallucination_preprocesses_once] New layers missed "
           "pending attacks");
    game.step();
    expect(game.player_.focus_.health_.hp_ == 9,
           "[stacked_hallucination_preprocesses_once] Existing attack was "
           "delayed again");
    game.step();
    expect(game.player_.focus_.health_.hp_ == 8,
           "[stacked_hallucination_preprocesses_once] Second attack did not "
           "resolve");

    auto branch = game;
    branch.gain(resource_type::DREAM, 2.0F);
    branch.step(skill::HALLUCINATION);
    const auto &original = write_attack<1, attack_type::PHYSICAL>(game.enemy_);
    const auto &copied = write_attack<1, attack_type::PHYSICAL>(branch.enemy_);
    expect(original.clock_.delayed_rounds_ == 2 &&
               copied.clock_.delayed_rounds_ == 3,
           "[stacked_hallucination_preprocesses_once] New attacks lost layers "
           "or copied callbacks changed the original");
}

void crossbow() {
    battle game;
    game.acquire(skill::CROSSBOW, 2.0F);
    game.gain(resource_type::IRON, 1.0F);
    game.step(skill::MAKE_BOLTS, 1);
    equal(game.player_.focus_.resource_.query(resource_type::BOLT), 3.0F,
          "MakeBolts did not produce three bolts");
    game.incoming(7);
    game.step(skill::BLOCK, 2);
    expect(game.player_.focus_.health_.hp_ == 10, "Block parabola mismatch");
    equal(game.player_.focus_.resource_.query(resource_type::BOLT), 2.0F,
          "Block cost mismatch");
    expect(!game.legal(skill::BLOCK, 0) && !game.legal(skill::BLOCK, 5),
           "Block accepted an invalid parameter");
    game.step(skill::AIM);
    game.step(skill::CRITICAL);
    expect(game.enemy_.focus_.health_.hp_ == 8,
           "Aimed critical is not two true damage");
    game.step(skill::CRITICAL);
    expect(game.enemy_.focus_.health_.hp_ == 6, "Aim did not reset");

    battle mark;
    mark.acquire(skill::CROSSBOW, 2.0F);
    mark.gain(resource_type::BOLT, 3.0F);
    mark.gain(resource_type::IRON, 1.0F);
    mark.enemy_.focus_.skills_.add_skill_set(SKILL_SET(cannon));
    mark.enemy_.focus_.resource_.gain(resource_type::IRON, 2.0F);
    mark.step(skill::MARK_SHOT);
    mark.step(skill::CRITICAL, 0, skill::SHIELD);
    expect(mark.enemy_.focus_.health_.hp_ == 7,
           "Mark did not double incoming true damage after defense");
    mark.step(skill::CRITICAL, 0, skill::SHIELD);
    expect(mark.enemy_.focus_.health_.hp_ == 6,
           "Mark lasted more than one round");

    battle outgoing;
    outgoing.acquire(skill::CROSSBOW, 2.0F);
    outgoing.gain(resource_type::BOLT, 1.0F);
    outgoing.gain(resource_type::IRON, 1.0F);
    outgoing.enemy_.focus_.resource_.gain(resource_type::IRON, 1.0F);
    outgoing.step(skill::MARK_SHOT);
    outgoing.step(skill::IRON, 0, skill::STICK);
    expect(outgoing.player_.focus_.health_.hp_ == 9,
           "Mark incorrectly strengthened the marked enemy's outgoing attack");
}

void phantombook() {
    battle game;
    game.player_.focus_.skills_.add_skill_set(SKILL_SET(phantombook));
    game.player_.focus_.health_.hp_ = 7;
    game.gain(resource_type::DREAM, 5.0F);
    game.gain(resource_type::IRON, 10.0F);
    expect(!game.legal(skill::ASSOCIATION),
           "Association accepted a missing follow-up action");
    game.step(skill::FANTASIA);
    game.step(skill::ILLUSION);
    expect(game.player_.focus_.health_.hp_ == 10,
           "Illusion did not recover health");
    equal(game.player_.focus_.resource_.query(resource_type::DREAM), 5.0F,
          "PhantomBook resource costs changed");

    battle delay;
    delay.acquire(skill::PHANTOMBOOK, 2.5F);
    delay.gain(resource_type::DREAM, 2.0F);
    delay.enemy_.focus_.resource_.gain(resource_type::IRON, 1.0F);
    delay.step(skill::HALLUCINATION, 0, skill::STICK);
    expect(delay.player_.focus_.health_.hp_ == 10,
           "Hallucination did not delay current attack");
    delay.step(skill::IRON, 0, skill::STICK);
    expect(delay.player_.focus_.health_.hp_ == 9,
           "Hallucination delayed the same attack twice");
    delay.step();
    expect(delay.player_.focus_.health_.hp_ == 8,
           "Hallucination did not delay future attack");

    battle armor;
    armor.acquire(skill::PHANTOMBOOK, 2.5F);
    armor.gain(resource_type::DREAM);
    armor.incoming(20);
    armor.step(skill::NIGHTMARE);
    expect(armor.legal(skill::DREAM_DIVE) && !armor.legal(skill::NIGHTMARE),
           "Nightmare skill package mismatch");
    armor.incoming(4);
    armor.step();
    expect(armor.player_.focus_.health_.hp_ == 5,
           "Nightmare armor failed to absorb a later physical hit");
    armor.incoming(0, 2);
    armor.step();
    expect(std::ranges::find(armor.player_.focus_.skills_.authorized_skills(),
                             skill::NIGHTMARE) !=
                   armor.player_.focus_.skills_.authorized_skills().end() &&
               !armor.legal(skill::DREAM_DIVE),
           "Nightmare package did not disappear after armor broke");
}

void cauldron() {
    battle game;
    game.acquire(skill::CAULDRON, 3.0F);
    game.gain(resource_type::WOOD);
    game.player_.focus_.health_.hp_ = 1;
    game.step(skill::REGENERATION);
    expect(game.player_.focus_.health_.hp_ == 2, "Regeneration first round");
    game.step();
    expect(game.player_.focus_.health_.hp_ == 4, "Regeneration second round");
    game.step();
    expect(game.player_.focus_.health_.hp_ == 7, "Regeneration third round");
    game.gain(resource_type::EARTH);
    game.incoming(0, 1);
    game.step(skill::STONE_SHELL);
    game.incoming(1);
    game.step();
    expect(game.player_.focus_.health_.hp_ == 6,
           "StoneShell failed to survive magic");
    game.incoming(1);
    game.step();
    expect(game.player_.focus_.health_.hp_ == 5,
           "StoneShell survived a physical hit");

    battle armor;
    armor.acquire(skill::CAULDRON, 3.0F);
    armor.gain(resource_type::EARTH, 1.0F);
    armor.gain(resource_type::WATER, 1.0F);
    armor.step(skill::ELEMENTAL_ARMOR);
    const auto &skills = armor.player_.focus_.skills_.authorized_skills();
    expect(skills.size() == 2 && armor.legal(skill::HAMMER) &&
               armor.legal(skill::GUARD),
           "Elemental armor did not disable other packages");
    armor.incoming(0, 8);
    armor.step(skill::GUARD);
    expect(!armor.legal(skill::IRON),
           "Guard unexpectedly allowed armor to break");
    armor.incoming(0, 8);
    armor.step(skill::HAMMER);
    expect(armor.legal(skill::IRON) && !armor.legal(skill::HAMMER),
           "Elemental armor did not restore original packages");

    battle burn;
    burn.acquire(skill::CAULDRON, 3.0F);
    burn.gain(resource_type::FIRE);
    burn.gain(resource_type::WOOD);
    burn.incoming(4, 0, 3);
    burn.step(skill::BURN_LIFE);
    expect(burn.player_.focus_.health_.hp_ == 7,
           "BurnLife reductions mismatch");
    burn.enemy_.focus_.health_ = {.hp_ = 100, .mhp_ = 100};
    burn.step(skill::EXPLOSION, 1);
    expect(burn.enemy_.focus_.health_.hp_ == 92,
           "BurnLife delayed doubling mismatch");
    burn.step();
    expect(!burn.player_.focus_.health_.is_dead(),
           "BurnLife destroyed user too early");
    burn.step();
    expect(burn.player_.focus_.health_.mhp_ == 10 - 114514,
           "BurnLife did not destroy user after three rounds");
}

void cauldron_defense_lifecycle() {
    battle shell;
    shell.acquire(skill::CAULDRON, 3.0F);
    shell.gain(resource_type::EARTH, 2.0F);
    shell.step(skill::STONE_SHELL);
    shell.step(skill::STONE_SHELL);
    expect(shell.player_.focus_.defense_.defenses_.size() == 1,
           "[cauldron_defense_lifecycle] StoneShell stacked");
    write_attack<3, attack_type::PHYSICAL>(shell.enemy_);
    write_attack<3, attack_type::PHYSICAL>(shell.enemy_);
    shell.step();
    expect(shell.player_.focus_.health_.hp_ == 10 &&
               shell.player_.focus_.defense_.defenses_.empty(),
           "[cauldron_defense_lifecycle] StoneShell did not preserve physical "
           "immunity through its trigger round or clear at round end");
    shell.incoming(3);
    shell.step();
    expect(shell.player_.focus_.health_.hp_ == 7,
           "[cauldron_defense_lifecycle] Expired StoneShell stayed immune");

    battle expiry;
    expiry.acquire(skill::CAULDRON, 3.0F);
    expiry.gain(resource_type::EARTH, 1.0F);
    expiry.gain(resource_type::WATER, 1.0F);
    expiry.step(skill::ELEMENTAL_ARMOR);
    for (const int remaining : std::array{6, 4, 2}) {
        const auto &defenses = expiry.player_.focus_.defense_.defenses_;
        expect(defenses.size() == 1 && defenses[0].power_ == remaining,
               "[cauldron_defense_lifecycle] Elemental armor did not decay by "
               "two");
        expiry.step(skill::GUARD);
    }
    expect(
        expiry.legal(skill::IRON) && !expiry.legal(skill::HAMMER) &&
            expiry.player_.focus_.defense_.defenses_.empty(),
        "[cauldron_defense_lifecycle] Expired elemental armor kept its mode");

    battle physical;
    physical.acquire(skill::CAULDRON, 3.0F);
    physical.gain(resource_type::EARTH, 1.0F);
    physical.gain(resource_type::WATER, 1.0F);
    physical.step(skill::ELEMENTAL_ARMOR);
    write_attack<20, attack_type::PHYSICAL>(physical.enemy_);
    write_attack<1, attack_type::PHYSICAL>(physical.enemy_);
    physical.step(skill::HAMMER);
    expect(physical.player_.focus_.health_.hp_ == 10 &&
               physical.legal(skill::IRON) &&
               physical.player_.focus_.defense_.defenses_.empty(),
           "[cauldron_defense_lifecycle] Armor did not preserve physical "
           "immunity through its breaking round or clear at round end");
    physical.incoming(1);
    physical.step();
    expect(physical.player_.focus_.health_.hp_ == 9,
           "[cauldron_defense_lifecycle] Broken elemental armor stayed immune");
}

void existing_hallucination_before_burn() {
    battle game;
    game.acquire(skill::CAULDRON, 3.0F);
    game.gain(resource_type::FIRE, 2.0F);
    game.gain(resource_type::WOOD, 1.0F);
    game.enemy_.focus_.skills_.add_skill_set(SKILL_SET(phantombook));
    game.enemy_.focus_.resource_.gain(resource_type::DREAM, 2.0F);
    game.step(skill::BURN_LIFE, 0, skill::HALLUCINATION);
    game.step(skill::EXPLOSION, 1);
    expect(game.enemy_.focus_.health_.hp_ == 10,
           "[existing_hallucination_before_burn] Attack was not delayed");
    game.step();
    expect(game.enemy_.focus_.health_.hp_ == 2,
           "[existing_hallucination_before_burn] BurnLife buffed a new delayed "
           "attack");
}

void burn_before_first_hallucination() {
    battle game;
    game.acquire(skill::CAULDRON, 3.0F);
    game.gain(resource_type::FIRE, 2.0F);
    game.gain(resource_type::WOOD, 1.0F);
    game.enemy_.focus_.skills_.add_skill_set(SKILL_SET(phantombook));
    game.enemy_.focus_.resource_.gain(resource_type::DREAM, 2.0F);
    game.step(skill::BURN_LIFE);
    game.step(skill::EXPLOSION, 1, skill::HALLUCINATION);
    expect(game.enemy_.focus_.health_.hp_ == 10,
           "[burn_before_first_hallucination] Attack was not delayed");
    game.step();
    expect(game.enemy_.focus_.health_.hp_ == -6,
           "[burn_before_first_hallucination] First delay preceded BurnLife's "
           "buff");
}
} // namespace

int main() {
    try {
        registrations();
        skill_disable_counts();
        bulk_disable_balances_skill_set_changes();

        cauldron_coexists_with_professions();
        equipment_coexists_and_restores_after_elemental_armor();

        defender_rounding();

        default_defense_update_lifetimes();

        context_preprocesses_before_storage();
        stacked_hallucination_preprocesses_once();

        crossbow();
        phantombook();
        cauldron();
        cauldron_defense_lifecycle();
        existing_hallucination_before_burn();
        burn_before_first_hallucination();
        std::cout << "Profession regression checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
