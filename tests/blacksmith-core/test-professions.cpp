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
    skill::BLOODSIGIL, /* skill::MONK, skill::PROPHET, */ skill::HOLYBOOK,
    skill::PHANTOMBOOK, skill::CROSSBOW};
constexpr std::array ROOT_PROFESSION_COSTS{1.0F, 4.0F, 3.0F, 3.0F, 7.0F,
                                           /* 3.0F, 2.0F, */ 2.0F, 2.5F, 2.0F};
constexpr std::array ROOT_PROFESSION_SKILLS{
    skill::MAGIC,      skill::STRIKE,      skill::SPACE_ATTACK,
    skill::SKY_STRIKE, skill::BLOOD_BLADE, /* skill::JADE, */
    /* skill::CRYSTAL, */ skill::PRAYER,      skill::FANTASIA,
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
               !branch.authorized(skill::CROSS) &&
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
               !game.authorized(skill::FIRE) &&
               !game.authorized(skill::CROSS),
           "[bulk_disable_balances_skill_set_changes] Bulk unlock resurrected "
           "a removed or unowned set");
}

// Temporarily disabled with Monk/Prophet.
// void elemental_armor_preserves_passives() {
//     battle game;
//     game.acquire(skill::MONK, 3.0F);
//     game.acquire(skill::CAULDRON, 3.0F);
//     game.gain(resource_type::EARTH, 2.0F);
//     game.gain(resource_type::WATER, 2.0F);
//     game.player_.focus_.monk_.clones_.push_back(
//         {.health_ = {.hp_ = 100, .mhp_ = 100}});
//     auto &skills = game.player_.focus_.skills_;
//     skill_context context{.action_ = {.skill_ = skill::IRON},
//                           .self_ = &game.player_};
//     skills.lock_all_skills();
//     skills.lock_all_skills();
//     skills.invoke_passive(context);
//     expect(game.player_.focus_.monk_.clones_[0].golden_bell_uses_ == 1,
//            "[elemental_armor_preserves_passives] Two skill locks changed "
//            "passive execution");
//     skills.release_all_skills();
//     skills.invoke_passive(context);
//     expect(game.player_.focus_.monk_.clones_[0].golden_bell_uses_ == 2,
//            "[elemental_armor_preserves_passives] Partial unlock changed "
//            "passive execution");
//     skills.release_all_skills();
//     skills.invoke_passive(context);
//     expect(game.player_.focus_.monk_.clones_[0].golden_bell_uses_ == 3,
//            "[elemental_armor_preserves_passives] Final unlock lost or "
//            "duplicated a passive");
//     for (int cycle = 0; cycle < 2; ++cycle) {
//         const int before_equip =
//             game.player_.focus_.monk_.clones_[0].golden_bell_uses_;
//         game.step(skill::ELEMENTAL_ARMOR);
//         const int uses = game.player_.focus_.monk_.clones_[0].golden_bell_uses_;
//         expect(uses == before_equip + 1,
//                "[elemental_armor_preserves_passives] Equip round changed "
//                "passive execution");
//         for (int i = 0; i < 3; ++i) {
//             game.step(skill::GUARD);
//             expect(game.player_.focus_.monk_.clones_[0].golden_bell_uses_ ==
//                        uses + i + 1,
//                    "[elemental_armor_preserves_passives] Armor mode lost or "
//                    "duplicated a passive");
//         }
//         game.step();
//         expect(game.player_.focus_.monk_.clones_[0].golden_bell_uses_ ==
//                    uses + 4,
//                "[elemental_armor_preserves_passives] Passive was lost or "
//                "duplicated after mode exit");
//     }
// }

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

void holybook() {
    battle game;
    game.acquire(skill::HOLYBOOK, 2.0F);
    game.gain(resource_type::CROSS);
    game.incoming(5);
    game.step(skill::PROFANATION);
    expect(game.player_.focus_.health_.hp_ == 7, "First GreyHP is not 2");
    game.incoming(5);
    game.step(skill::PROFANATION);
    expect(game.player_.focus_.health_.hp_ == 5, "Second GreyHP is not 3");
    game.incoming(2);
    game.step();
    expect(game.player_.focus_.health_.hp_ == 3, "GreyHP survived a round");
    game.step(skill::ABSOLUTION);
    game.incoming(3);
    game.step();
    expect(game.player_.focus_.health_.hp_ == 1, "Permanent reduction expired");
    game.step(skill::REBIRTH);
    expect(game.player_.focus_.health_.hp_ == 4, "Rebirth first recovery");
    game.step();
    expect(game.player_.focus_.health_.hp_ == 7, "Rebirth second recovery");
    game.step();
    expect(game.player_.focus_.health_.hp_ == 10, "Rebirth third recovery");
}

void greyhp_pool_and_priority() {
    battle pool;
    pool.acquire(skill::HOLYBOOK, 2.0F);
    pool.gain(resource_type::CROSS, 1.0F);
    pool.incoming(3, 3);
    pool.step(skill::PROFANATION);
    expect(
        pool.player_.focus_.health_.hp_ == 6,
        "[greyhp_pool_and_priority] GreyHP was replenished for a second hit");

    battle priority;
    priority.acquire(skill::HOLYBOOK, 2.0F);
    priority.gain(resource_type::CROSS, 1.0F);
    write_defense<{.type_ = defense_type::COMMON_ARMOR,
                   .power_ = 4,
                   .clock_ = {.is_infinite_ = true},
                   .defender_ = default_armor}>(priority.player_);
    priority.incoming(5);
    priority.step(skill::PROFANATION);
    expect(priority.player_.focus_.health_.hp_ == 10 &&
               priority.player_.focus_.defense_.defenses_.size() == 1 &&
               priority.player_.focus_.defense_.defenses_[0].type_ ==
                   defense_type::COMMON_ARMOR &&
               priority.player_.focus_.defense_.defenses_[0].power_ == 0,
           "[greyhp_pool_and_priority] GreyHP absorbed damage before armor");
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

// Temporarily disabled with Monk/Prophet.
// void prophet() {
//     battle game;
//     game.acquire(skill::PROPHET, 2.0F);
//     game.gain(resource_type::IRON);
//     game.gain(resource_type::CRYSTAL, 1.0F);
//     game.step(skill::GREATEST_CAUTION);
//     game.step(skill::REVELATION);
//     game.incoming(4, 5, 2);
//     game.step(skill::FORETOLD);
//     expect(game.player_.focus_.health_.hp_ == 8, "Evasion changed real damage");
//     equal(game.player_.focus_.resource_.query(resource_type::CRYSTAL), 2.0F,
//           "Evasion reward/revelation mismatch");
//     game.step();
//     expect(game.enemy_.focus_.health_.hp_ == 6,
//            "Caution retaliation not delayed");
//     game.step(skill::ASSERTION);
//     expect(game.player_.focus_.health_.hp_ == 6,
//            "Assertion did not penalize no attack");
//     game.incoming(0, 0, 1);
//     game.step(skill::ASSERTION);
//     expect(game.player_.focus_.health_.hp_ == 5,
//            "Assertion misclassified a real attack");
//     game.step(skill::CRYSTAL_BALL);
//     expect(!game.legal(skill::CRYSTAL_BALL), "CrystalBall exceeded its cap");
//     expect(!game.legal(skill::ULTIMATUM, 5), "Ultimatum accepted n <= 5");

//     battle wall;
//     wall.acquire(skill::PROPHET, 2.0F);
//     wall.gain(resource_type::CRYSTAL_BALL, 1.0F);
//     wall.incoming(10);
//     wall.step(skill::CRYSTAL_WALL);
//     expect(wall.player_.focus_.health_.hp_ == 7, "Wall reduction is not 7");
//     expect(wall.enemy_.focus_.health_.hp_ == 10,
//            "Wall reflection was immediate");
//     wall.incoming(10);
//     wall.step();
//     expect(wall.enemy_.focus_.health_.hp_ == 10 &&
//                wall.player_.focus_.health_.hp_ == 7,
//            "Wall reflection/opposition amount mismatch");
//     equal(wall.player_.focus_.resource_.query(resource_type::CRYSTAL), 2.0F,
//           "Wall did not refund a crystal for each independent round");
//     wall.incoming(5);
//     wall.step();
//     expect(wall.player_.focus_.health_.hp_ == 5,
//            "Wall lasted more than two rounds");

//     battle ultimatum;
//     ultimatum.acquire(skill::PROPHET, 2.0F);
//     ultimatum.gain(resource_type::CRYSTAL_BALL, 1.0F);
//     ultimatum.gain(resource_type::CRYSTAL, 2.0F);
//     ultimatum.step(skill::ULTIMATUM, 6);
//     for (int i = 0; i < 5; ++i)
//         ultimatum.step();
//     expect(ultimatum.enemy_.focus_.health_.hp_ == 10, "Ultimatum fired early");
//     ultimatum.step();
//     expect(ultimatum.enemy_.focus_.health_.hp_ == -2,
//            "Ultimatum failed to fire");
// }

// Temporarily disabled with Monk/Prophet.
// void crystal_wall_hit_marks() {
//     const auto expect_cleared = [](const community &player) {
//         expect(count_mark<mark_id::CRYSTAL_WALL_HIT>(player) == 0 &&
//                    count_mark<mark_id::DELAYED_CRYSTAL_WALL_HIT>(player) == 0,
//                "[crystal_wall_hit_marks] A wall hit mark survived its round");
//     };
//     battle single;
//     single.acquire(skill::PROPHET, 2.0F);
//     single.gain(resource_type::CRYSTAL_BALL, 1.0F);
//     for (const int damage : std::array{2, 3, 1}) {
//         write_attack<attack_type::MAGICAL>(damage, single.enemy_);
//     }
//     single.step(skill::CRYSTAL_WALL);
//     equal(single.player_.focus_.resource_.query(resource_type::CRYSTAL), 1.0F,
//           "[crystal_wall_hit_marks] One wall refunded once per hit");
//     expect_cleared(single.player_);
//     for (const int damage : std::array{2, 3}) {
//         write_attack<attack_type::MAGICAL>(damage, single.enemy_);
//     }
//     single.step();
//     equal(single.player_.focus_.resource_.query(resource_type::CRYSTAL), 2.0F,
//           "[crystal_wall_hit_marks] The second wall round did not refund once");
//     expect(single.player_.focus_.health_.hp_ == 10,
//            "[crystal_wall_hit_marks] A wall did not reduce each magical hit");
//     expect_cleared(single.player_);

//     battle overlap;
//     overlap.acquire(skill::PROPHET, 2.0F);
//     overlap.player_.focus_.health_ = {.hp_ = 100, .mhp_ = 100};
//     overlap.gain(resource_type::CRYSTAL_BALL, 2.0F);
//     overlap.incoming(0, 1);
//     overlap.step(skill::CRYSTAL_WALL);
//     equal(overlap.player_.focus_.resource_.query(resource_type::CRYSTAL), 1.0F,
//           "[crystal_wall_hit_marks] The first cast did not refund");
//     write_attack<20, attack_type::MAGICAL>(overlap.enemy_);
//     write_attack<20, attack_type::MAGICAL>(overlap.enemy_);
//     overlap.step(skill::CRYSTAL_WALL);
//     equal(overlap.player_.focus_.resource_.query(resource_type::CRYSTAL), 3.0F,
//           "[crystal_wall_hit_marks] Overlapping walls did not refund "
//           "independently");
//     expect(
//         overlap.player_.focus_.health_.hp_ == 88,
//         "[crystal_wall_hit_marks] Overlapping walls changed their reduction");
//     expect_cleared(overlap.player_);
//     write_attack<1, attack_type::MAGICAL>(overlap.enemy_);
//     write_attack<1, attack_type::MAGICAL>(overlap.enemy_);
//     overlap.step();
//     equal(overlap.player_.focus_.resource_.query(resource_type::CRYSTAL), 4.0F,
//           "[crystal_wall_hit_marks] The newer delayed wall lost its refund");
//     expect_cleared(overlap.player_);
//     overlap.incoming(0, 1);
//     overlap.step();
//     expect(overlap.player_.focus_.health_.hp_ == 87,
//            "[crystal_wall_hit_marks] An expired wall remained active");
// }

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

// Temporarily disabled with Monk/Prophet.
// void monk() {
//     battle game;
//     game.acquire(skill::MONK, 3.0F);
//     game.gain(resource_type::JADE);
//     game.incoming(6);
//     game.step(skill::GHOST_STEP);
//     expect(game.player_.focus_.health_.hp_ == 6,
//            "New clone did not redirect damage");
//     expect(game.player_.focus_.monk_.clones_[0].health_.hp_ == 6,
//            "Clone damage was not conserved");
//     game.incoming(6, 2);
//     game.step();
//     expect(game.player_.focus_.health_.hp_ == 6,
//            "Clone's first GoldenBellCover is not 100 percent");
//     game.player_.focus_.monk_.clones_[0].golden_bell_uses_ = 1;
//     game.incoming(5);
//     game.step();
//     expect(game.player_.focus_.health_.hp_ == 5,
//            "40 percent cover or rounding mismatch");
//     game.gain(resource_type::IRON, 1.0F);
//     game.step(skill::MIST);
//     expect(!game.legal(skill::MIST), "Mist cooldown did not start");
//     game.step();
//     expect(!game.legal(skill::MIST), "Mist cooldown ended too early");
//     game.step();
//     expect(game.legal(skill::MIST),
//            "Mist cooldown did not end after three rounds");

//     battle bell;
//     bell.acquire(skill::MONK, 3.0F);
//     bell.gain(resource_type::JADE);
//     bell.incoming(5, 0, 1);
//     bell.step(skill::GOLDEN_BELL_COVER);
//     expect(bell.player_.focus_.health_.hp_ == 9, "Bell reduced true damage");
//     bell.incoming(5);
//     bell.step(skill::GOLDEN_BELL_COVER);
//     expect(bell.player_.focus_.health_.hp_ == 6,
//            "Second bell is not 40 percent");
//     bell.incoming(5);
//     bell.step(skill::GOLDEN_BELL_COVER);
//     expect(bell.player_.focus_.health_.hp_ == 0,
//            "Third bell is not vulnerable");

//     battle fist;
//     fist.acquire(skill::MONK, 3.0F);
//     fist.gain(resource_type::JADE);
//     fist.enemy_.focus_.health_ = {.hp_ = 100, .mhp_ = 100};
//     fist.step(skill::GHOST_STEP);
//     write_defense<{.type_ = defense_type::COMMON_ARMOR,
//                    .power_ = 10,
//                    .clock_ = {.is_infinite_ = true},
//                    .defender_ = default_armor}>(fist.enemy_);
//     fist.step(skill::MAZE_FIST);
//     expect(fist.player_.focus_.monk_.clones_.empty(),
//            "MazeFist did not consume clone");
//     fist.incoming(20);
//     fist.step();
//     expect(fist.player_.focus_.health_.hp_ == 9,
//            "MazeFist did not evade after armor hit");
//     expect(fist.enemy_.focus_.defense_.defenses_[0].power_ == 4,
//            "Lost-clone retaliation was not opposed by enemy attack");
// }

// Temporarily disabled with Monk/Prophet.
// void monk_clone_lifecycle() {
//     battle healing;
//     healing.acquire(skill::MONK, 3.0F);
//     healing.player_.focus_.health_ = {.hp_ = 7, .mhp_ = 14};
//     healing.gain(resource_type::JADE, 1.0F);
//     healing.gain(resource_type::IRON, 1.0F);
//     healing.step(skill::GHOST_STEP);
//     expect(healing.player_.focus_.monk_.clones_[0].health_.mhp_ == 14,
//            "[monk_clone_lifecycle] Clone did not inherit the main body's MHP");
//     healing.step(skill::MIST);
//     expect(healing.player_.focus_.monk_.clones_[0].health_.hp_ == 9,
//            "[monk_clone_lifecycle] Mist did not heal on its cast round");
//     healing.step();
//     expect(healing.player_.focus_.monk_.clones_[0].health_.hp_ == 12,
//            "[monk_clone_lifecycle] Mist did not heal on its second round");
//     healing.step();
//     expect(healing.player_.focus_.monk_.clones_[0].health_.hp_ == 14,
//            "[monk_clone_lifecycle] Mist could not heal above creation HP");

//     battle split;
//     split.acquire(skill::MONK, 3.0F);
//     for (int i = 0; i < 2; ++i) {
//         split.player_.focus_.monk_.clones_.push_back(
//             {.health_ = {.hp_ = 100, .mhp_ = 100}, .golden_bell_uses_ = 2});
//     }
//     split.incoming(3);
//     split.step();
//     expect(split.player_.focus_.health_.hp_ == 10 &&
//                split.player_.focus_.monk_.clones_[0].health_.hp_ == 98 &&
//                split.player_.focus_.monk_.clones_[1].health_.hp_ == 99,
//            "[monk_clone_lifecycle] Each clone did not round its own half up");

//     battle lethal;
//     lethal.acquire(skill::MONK, 3.0F);
//     lethal.player_.focus_.health_.hp_ = 1;
//     lethal.gain(resource_type::JADE, 1.0F);
//     expect(lethal.legal(skill::GHOST_STEP),
//            "[monk_clone_lifecycle] GhostStep rejected its final HP payment");
//     lethal.step(skill::GHOST_STEP);
//     expect(
//         lethal.player_.focus_.health_.hp_ == 0 &&
//             lethal.player_.focus_.monk_.clones_.empty(),
//         "[monk_clone_lifecycle] A zero-HP clone survived its creation round");
//     lethal.step();
//     expect(lethal.enemy_.focus_.health_.hp_ == 5,
//            "[monk_clone_lifecycle] The zero-HP clone did not trigger its echo");
// }

// Temporarily disabled with Monk/Prophet.
// void failed_evasion_keeps_enhancements() {
//     battle game;
//     game.acquire(skill::PROPHET, 2.0F);
//     game.gain(resource_type::IRON);
//     game.gain(resource_type::CRYSTAL, 1.0F);
//     game.step(skill::GREATEST_CAUTION);
//     game.step(skill::REVELATION);
//     game.step(skill::FORETOLD);
//     game.incoming(0, 0, 1);
//     game.step(skill::ASSERTION);
//     expect(count_mark<mark_id::GREATEST_CAUTION>(game.player_) == 1 &&
//                count_mark<mark_id::REVELATION>(game.player_) == 1,
//            "[failed_evasion_keeps_enhancements] Failed evasion consumed buffs");
//     game.incoming(1);
//     game.step(skill::FORETOLD);
//     equal(game.player_.focus_.resource_.query(resource_type::CRYSTAL), 2.0F,
//           "[failed_evasion_keeps_enhancements] Revelation was not retained");
//     expect(count_mark<mark_id::GREATEST_CAUTION>(game.player_) == 0 &&
//                count_mark<mark_id::REVELATION>(game.player_) == 0,
//            "[failed_evasion_keeps_enhancements] Successful evasion kept buffs");
//     game.step();
//     expect(game.enemy_.focus_.health_.hp_ == 6,
//            "[failed_evasion_keeps_enhancements] Caution was not retained");
// }

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

// Temporarily disabled with Monk/Prophet.
// void hallucination_delays_ultimatum() {
//     battle game;
//     game.acquire(skill::PROPHET, 2.0F);
//     game.gain(resource_type::CRYSTAL_BALL, 1.0F);
//     game.gain(resource_type::CRYSTAL, 2.0F);
//     game.enemy_.focus_.skills_.add_skill_set(SKILL_SET(phantombook));
//     game.enemy_.focus_.resource_.gain(resource_type::DREAM, 2.0F);
//     game.step(skill::IRON, 0, skill::HALLUCINATION);
//     game.step(skill::ULTIMATUM, 6);
//     for (int i = 0; i < 6; ++i) {
//         game.step();
//         expect(game.enemy_.focus_.health_.hp_ == 10,
//                "[hallucination_delays_ultimatum] Explicit delay erased "
//                "hallucination");
//     }
//     game.step();
//     expect(game.enemy_.focus_.health_.hp_ == -2,
//            "[hallucination_delays_ultimatum] Ultimatum did not fire after "
//            "seven rounds");
// }

// Temporarily disabled with Monk/Prophet.
// void attack_cancellation_order() {
//     battle game;
//     game.acquire(skill::MONK, 3.0F);
//     game.gain(resource_type::JADE, 2.0F);
//     write_defense<{.type_ = defense_type::MAGICAL_IMMUNITY,
//                    .power_ = 1,
//                    .defender_ = magical_immunity}>(game.enemy_);
//     game.incoming(5);
//     game.step(skill::DISILLUSIONMENT);
//     expect(game.enemy_.focus_.health_.hp_ == 9,
//            "[attack_cancellation_order] Attacks did not cancel in declaration "
//            "order");
// }

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
        // elemental_armor_preserves_passives();
        cauldron_coexists_with_professions();
        equipment_coexists_and_restores_after_elemental_armor();
        holybook();
        greyhp_pool_and_priority();
        defender_rounding();
        // prophet();
        // crystal_wall_hit_marks();
        default_defense_update_lifetimes();
        // monk();
        // monk_clone_lifecycle();
        // failed_evasion_keeps_enhancements();
        context_preprocesses_before_storage();
        stacked_hallucination_preprocesses_once();
        // hallucination_delays_ultimatum();
        // attack_cancellation_order();
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
