#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>
#include <skill-system/skill-sets.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

// Keep the fixture's static REGISTER functions visible for direct calls, independent
// of the core's registrations for the same enum slots.
#include "repeat-fixtures.cpp"
#undef PHYSICAL
#undef MAGICAL
#undef REAL

using namespace blacksmith::domain;
using namespace blacksmith::skill_system;
namespace {
struct battle {
    community player_{&enemy_};
    community enemy_{&player_};
};

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
skill_context context(community& p, skill s, int n = 0) {
    return {.action_ = {.skill_ = s, .param_ = n}, .self_ = &p};
}
bool check(community& p, skill s, int n = 0) {
    auto c = context(p, s, n);
    return get_skill_check_mapping()[static_cast<std::size_t>(s)](c);
}
void invoke(community& p, skill s, int n = 0) {
    auto c = context(p, s, n);
    get_skill_declare_mapping()[static_cast<std::size_t>(s)](c);
}
void test_skill_names() {
    const std::array expected{
        std::pair{"magic", skill::MAGIC}, std::pair{"magicattack", skill::MAGIC_ATTACK},
        std::pair{"magicshield", skill::MAGIC_SHIELD}, std::pair{"sacrifice", skill::SACRIFICE},
        std::pair{"mute", skill::MUTE}, std::pair{"alchemy", skill::ALCHEMY},
        std::pair{"strike", skill::STRIKE}, std::pair{"doublestrike", skill::DOUBLE_STRIKE},
        std::pair{"triplestrike", skill::TRIPLE_STRIKE}, std::pair{"apshell", skill::APSHELL},
        std::pair{"cannonbarrier", skill::CANNON_BARRIER}
    };
    const auto& mapping = get_string_skill_mapping();
    for (const auto& [name, id] : expected) {
        require(std::ranges::count_if(mapping, [&](const auto& entry) {
            return entry.first == name && entry.second == id;
        }) == 1, "registered skill name and enum match the BDSL declaration");
    }
}
void test_repeat_macros() {
    battle game;
    auto& p = game.player_;
    auto c = context(p, skill::STICK, 2);
    repeatplain_declare(c);
    require(p.focus_.turn_context_.attack_context_.datas_.size() == 3, "REPEAT produces three attacks");
    for (int i = 0; i < 3; ++i) {
        const auto& a = p.focus_.turn_context_.attack_context_.datas_[i];
        require(a.power_ == i + 1 && a.clock_.delayed_rounds_ == i, "REPEAT compile-time index");
    }
    p = community{p.opponent_};
    repeatbatch_declare(c);
    require(p.focus_.turn_context_.attack_context_.datas_.size() == 3, "REPEAT_BATCH produces three attacks");
    for (int i = 0; i < 3; ++i) {
        const auto& a = p.focus_.turn_context_.attack_context_.datas_[i];
        require(a.power_ == i + 4 && a.clock_.delayed_rounds_ == i, "REPEAT_BATCH receives N and index");
    }
    p = community{p.opponent_};
    for (int i = 0; i < 3; ++i) write_mark<mark_id::CANNON>(p);
    repeatcapture_declare(c);
    require(p.focus_.turn_context_.attack_context_.datas_.size() == 2, "conditional repeat runs in its source position");
    require(p.focus_.turn_context_.attack_context_.datas_[0].power_ == 5 && p.focus_.turn_context_.attack_context_.datas_[1].power_ == 6, "repeat captures outer mark values and N");
    require(count_mark<mark_id::CANNON>(p) == 0, "outer mark read executes once");
    p = community{p.opponent_};
    repeatcapture_declare(c);
    require(p.focus_.turn_context_.attack_context_.datas_.empty(), "unselected conditional repeat does not run");
    p = community{p.opponent_};
    for (int i = 0; i < 3; ++i) write_mark<mark_id::CANNON>(p);
    repeatnested_declare(c);
    require(p.focus_.turn_context_.resource_context_.datas_.size() == 6, "nested repeats produce two times three entries");
    for (int i = 0; i < 6; ++i) require(p.focus_.turn_context_.resource_context_.datas_[i].power_ == 8.0F + i % 3, "nested repeats retain captured variables and use the nearest index");
    p = community{p.opponent_};
    for (int i = 0; i < 3; ++i) write_mark<mark_id::CANNON>(p);
    repeatlocal_declare(c);
    const int powers[] = {3, 4, 0, 1};
    require(p.focus_.turn_context_.attack_context_.datas_.size() == 4, "nested repeat with per-iteration variables");
    for (int i = 0; i < 4; ++i) require(p.focus_.turn_context_.attack_context_.datas_[i].power_ == powers[i], "repeat-local mark reads execute in each iteration");
    p = community{p.opponent_};
    p.focus_.health_.hp_ = 1;
    passive_declare(c);
    require(p.focus_.health_.hp_ == 3, "passive repeat runs twice; zero and negative counts do nothing");
}
void test_warlock() {
    require(get_warlock()->authorized_skills_.size() == 6, "warlock registration");
    battle game;
    auto& p = game.player_;
    auto& enemy = game.enemy_;
    require(!check(p, skill::MAGIC), "magic requires iron");
    p.focus_.resource_.gain(resource_type::GOLD_IRON, 1);
    require(check(p, skill::MAGIC), "gold iron counts toward iron");
    invoke(p, skill::MAGIC);
    require(p.focus_.resource_.query(resource_type::IRON) == 0, "magic spends iron immediately");
    require(p.focus_.turn_context_.resource_context_.datas_.front().type_ == resource_type::MAGIC, "magic queues resource");
    apply_resource(p, enemy);
    require(p.focus_.resource_.query(resource_type::MAGIC) == 1, "magic gains at resolution");

    p = community{p.opponent_};
    p.focus_.resource_.gain(resource_type::MAGIC, 5);
    require(!check(p, skill::MAGIC_ATTACK, 0), "batch rejects zero");
    require(!check(p, skill::MAGIC_ATTACK, -1), "batch rejects negative");
    require(!check(p, skill::MAGIC_ATTACK, 6), "batch requires magic");
    require(check(p, skill::MAGIC_ATTACK, 2), "batch accepts valid n");
    invoke(p, skill::MAGIC_ATTACK, 2);
    require(p.focus_.resource_.query(resource_type::MAGIC) == 3, "batch spends n magic");
    const auto& attacks = p.focus_.turn_context_.attack_context_.datas_;
    require(attacks.size() == 3, "three attacks");
    for (int i = 0; i < 3; ++i) {
        require(attacks[i].type_ == attack_type::MAGICAL && attacks[i].power_ == 4, "batch attack power/type");
        require(attacks[i].clock_.delayed_rounds_ == i, "batch attack delay");
    }
    apply_attack(p, enemy);
    require(enemy.focus_.health_.hp_ == 6, "only immediate attack resolves");
    round_pass(p, enemy);
    apply_attack(p, enemy);
    require(enemy.focus_.health_.hp_ == 2, "second attack resolves next round");

    p = community{p.opponent_};
    p.focus_.resource_.gain(resource_type::MAGIC, 3);
    invoke(p, skill::MAGIC_SHIELD, 2);
    const auto& defenses = p.focus_.turn_context_.defense_context_.datas_;
    require(defenses.size() == 3, "three shields");
    for (int i = 0; i < 3; ++i) {
        require(defenses[i].defense_.power_ == 6 && defenses[i].defense_.type_ == defense_type::REAL_REDUCTION, "shield power/type");
        require(defenses[i].clock_.delayed_rounds_ == i, "shield delay");
    }

    p = community{p.opponent_};
    require(check(p, skill::SACRIFICE), "sacrifice check");
    invoke(p, skill::SACRIFICE);
    require(p.focus_.health_.mhp_ == 9, "sacrifice lowers max health");
    require(p.focus_.turn_context_.resource_context_.datas_.front().power_ == 1.5F, "fractional resource");
    require(p.focus_.turn_context_.defense_context_.datas_.front().defense_.power_ == 7, "sacrifice shield");
    p.focus_.health_.mhp_ = 1;
    require(!check(p, skill::SACRIFICE), "sacrifice needs two max health");

    p = community{p.opponent_}; enemy = community{enemy.opponent_};
    write_resource<resource_type::TIME>(1, enemy);
    write_resource<resource_type::SPACE>(1, enemy);
    write_resource<resource_type::IRON>(1, enemy);
    write_resource<resource_type::TIME>(1, p);
    invoke(p, skill::MUTE);
    apply_effect(p, enemy); take_effect(p, enemy);
    require(enemy.focus_.turn_context_.resource_context_.datas_.size() == 1, "mute removes enemy time and space");
    require(enemy.focus_.turn_context_.resource_context_.datas_.front().type_ == resource_type::IRON, "mute preserves enemy iron");
    require(p.focus_.turn_context_.resource_context_.datas_.size() == 1, "mute preserves caster resources");

    p = community{p.opponent_};
    p.focus_.skills_.add_skill_set(get_warlock());
    p.focus_.resource_.gain(resource_type::IRON, 3);
    require(check(p, skill::ALCHEMY), "alchemy check");
    invoke(p, skill::ALCHEMY);
    require(p.focus_.resource_.query(resource_type::IRON) == .5F, "alchemy cost");
    auto midas = context(p, skill::MIDASTOUCH);
    require(p.focus_.skills_.check(midas) != check_result::INVALID, "alchemy adds skill set");
    auto alchemy = context(p, skill::ALCHEMY);
    require(p.focus_.skills_.check(alchemy) != check_result::SUCCESS, "alchemy disables itself");

    p = community{p.opponent_};
    p.focus_.turn_context_.attack_context_.register_preprocess(
        [](attack_data& attack) { ++attack.clock_.delayed_rounds_; });
    p.focus_.resource_.gain(resource_type::MAGIC, 1);
    invoke(p, skill::MAGIC_ATTACK, 1);
    for (int i = 0; i < 3; ++i) require(p.focus_.turn_context_.attack_context_.datas_[i].clock_.delayed_rounds_ == i + 1, "hallucination delay retained");
}
void test_cannon() {
    require(get_cannon()->authorized_skills_.size() == 5, "cannon registration");
    const skill ids[] = {skill::STRIKE, skill::DOUBLE_STRIKE, skill::TRIPLE_STRIKE, skill::APSHELL, skill::CANNON_BARRIER};
    const int costs[] = {1, 2, 3, 1, 0}, powers[] = {4, 8, 11, 2, 1};
    for (int index = 0; index < 5; ++index) {
        for (int marks = 0; marks < 3; ++marks) {
            battle game;
            auto& p = game.player_;
            require(check(p, ids[index]) == (costs[index] == 0), "cannon resource check");
            p.focus_.resource_.gain(resource_type::IRON, 10);
            for (int m = 0; m < marks; ++m) write_mark<mark_id::CANNON>(p);
            invoke(p, ids[index]);
            require(p.focus_.resource_.query(resource_type::IRON) == 10 - costs[index], "cannon costs");
            const auto& a = p.focus_.turn_context_.attack_context_.datas_.front();
            require(a.power_ == powers[index] + (marks > 0 ? 1 : 0), "cannon mark bonus capped at one");
            require(a.type_ == attack_type::PHYSICAL, "cannon physical damage");
            require(count_mark<mark_id::CANNON>(p) == (index == 2 ? 1 : 0), "all cannon marks consumed, triple creates one");
            if (index == 2) require(p.focus_.turn_context_.resource_context_.datas_.front().power_ == .5F, "triple iron rebate");
            if (index == 3) require(a.ap_factor_ == 3 && a.interrupt_, "AP shell modifiers");
            if (index == 4) require(p.focus_.turn_context_.defense_context_.datas_.front().defense_.power_ == 2, "cannon barrier");
        }
    }
}
} // namespace
int main() {
    try { test_skill_names(); test_repeat_macros(); test_warlock(); test_cannon(); std::cout << "Generated warlock, cannon and repeat behavior verified\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
