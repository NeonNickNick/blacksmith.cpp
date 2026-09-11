#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <domain/models.hpp>
#include <iostream>
#include <skill-system/professions.hpp>
using namespace blacksmith_core::skill_system;
namespace blacksmith_core::domain {
// 辅助
inline int cancel(int &a, int &b) {
    assert(a >= 0 && b >= 0);
    auto res = std::max(a, b);
    auto temp = a;
    a = std::max(0, a - b);
    b = std::max(0, b - temp);
    return res;
}
// 初始化
inline void initialize(community &player, community &enemy) {
    player.focus_.profession_.add_profession(common_skill_set());
    enemy.focus_.profession_.add_profession(common_skill_set());
}
//
// executor
inline constexpr std::array<defense_type, 5> AP_TYPES{
    defense_type::THORN_REDUCTION, defense_type::COMMON_REDUCTION,
    defense_type::STONE_SHELL, defense_type::REAL_ARMOR,
    defense_type::COMMON_ARMOR};
inline constexpr std::array<defense_type, 3> ARMOR_TYPES{
    defense_type::STONE_SHELL, defense_type::REAL_ARMOR,
    defense_type::COMMON_ARMOR};

template <analyzable_data T>
inline auto default_hook =
    [](community & /*player*/, community & /*enemy*/, T & /*data*/) {};

template <attack_execute_func FirstTimeHitArmor, attack_execute_func HitBody,
          attack_execute_func End>
void execute_attack(community &player, community &enemy, attack_data &attack) {
    if (attack.power_ <= 0) {
        return;
    }
    body &main = enemy.focus_;
    bool ifHitArmor = false;
    if (attack.type_ != attack_type::REAL) {
        for (auto &d : main.defense_.defenses_) {
            for (auto type : ARMOR_TYPES) {
                if (d.type_ == type) {
                    if (!ifHitArmor) {
                        ifHitArmor = true;
                        FirstTimeHitArmor(player, enemy, attack);
                    }
                    break;
                }
            }
            for (auto type : AP_TYPES) {
                if (d.type_ == type) {
                    attack.power_ = static_cast<int>(std::ceil(
                        static_cast<float>(attack.power_) * attack.ap_factor_));
                }
            }
            int origin = attack.power_;
            d.defender_(enemy, player, d, attack);
            attack.total_damage_ += (origin - attack.power_);
            for (auto type : AP_TYPES) {
                if (d.type_ == type) {
                    attack.power_ = static_cast<int>(std::ceil(
                        static_cast<float>(attack.power_) / attack.ap_factor_));
                }
            }
            if (attack.power_ <= 0) {
                End(player, enemy, attack);
                return;
            }
        }
    }
    if (!ifHitArmor) {
        FirstTimeHitArmor(player, enemy, attack);
    }
    HitBody(player, enemy, attack);
    main.health_.lose_hp(attack.power_);
    attack.total_damage_ += attack.power_;
    End(player, enemy, attack);
}
inline void default_attack(community &player, community &enemy,
                           attack_data &attack) {
    execute_attack<default_hook<attack_data>, default_hook<attack_data>,
                   default_hook<attack_data>>(player, enemy, attack);
}
template <defense_execute_func END>
void execute_defense(community &player, community &enemy,
                     defense_data &defense) {
    player.focus_.defense_.add(defense.defense_);
    END(player, enemy, defense);
}
inline void default_defense(community &player, community &enemy,
                            defense_data &defense) {
    execute_defense<default_hook<defense_data>>(player, enemy, defense);
}
template <resource_execute_func END>
void execute_resource(community &player, community &enemy,
                      resource_data &resource) {
    player.focus_.resource_.gain(resource.type_, resource.power_);
    END(player, enemy, resource);
}
inline void default_resource(community &player, community &enemy,
                             resource_data &resource) {
    execute_resource<default_hook<resource_data>>(player, enemy, resource);
}

// DSL
template <int power, attack_type type, clap_round_clock clock = {},
          attack_execute_func executor = &default_attack,
          float ap_factor = 1.0F>
void write_attack(community &player) {
    player.focus_.turn_context_.attack_context_.write(clock, type, power,
                                                      ap_factor, executor, 0);
}
template <attack_type type, clap_round_clock clock = {},
          attack_execute_func executor = &default_attack,
          float ap_factor = 1.0F>
void write_attack(int power, community &player) {
    player.focus_.turn_context_.attack_context_.write(clock, type, power,
                                                      ap_factor, executor, 0);
}
template <defense_entity defense, clap_round_clock clock = {},
          defense_execute_func executor = &default_defense>
void write_defense(community &player) {
    player.focus_.turn_context_.defense_context_.write(clock, defense,
                                                       executor);
}

template <float power, resource_type type, clap_round_clock clock = {},
          resource_execute_func executor = &default_resource>
void write_resource(community &player) {
    player.focus_.turn_context_.resource_context_.write(clock, type, power,
                                                        executor);
}
template <int power> void write_recovery(community &player) {
    player.focus_.health_.gain_hp(power);
}
//

// 技能检查及声明
inline check_result check_skill(const community &player,
                                const skill_context &context) {
    return player.focus_.profession_.check(context);
}
inline void declare(community &player, skill_context &player_context,
                    community &enemy, skill_context &enemy_context) {
    player.focus_.profession_.declare(player_context);
    enemy.focus_.profession_.declare(enemy_context);
}
//

// 判定规则
using transform_func = void(community &, community &);
inline void cancel_attack(community &player, community &enemy) {
    auto &player_data = player.focus_.turn_context_.attack_context_.datas_;
    auto &enemy_data = enemy.focus_.turn_context_.attack_context_.datas_;
    const auto NP = player_data.size();
    const auto NE = enemy_data.size();
    size_t ip = 0;
    size_t ie = 0;
    while (ip < NP && ie < NE) {
        if (!player_data[ip].clock_.is_ringing()) {
            ip++;
            continue;
        }
        if (!enemy_data[ie].clock_.is_ringing()) {
            ie++;
            continue;
        }
        cancel(player_data[ip].power_, enemy_data[ie].power_);
        if (player_data[ip].power_ == 0) {
            ip++;
        }
        if (enemy_data[ie].power_ == 0) {
            ie++;
        }
    }
    auto zero = [](const attack_data &data) { return data.power_ == 0; };
    std::erase_if(player_data, zero);
    std::erase_if(enemy_data, zero);
}
inline void apply_defense(community &player, community &enemy) {
    auto apply = [](community &p, community &e) {
        for (auto &data : p.focus_.turn_context_.defense_context_.datas_) {
            if (data.clock_.is_ringing()) {
                data.executor_(p, e, data);
            }
        }
    };
    apply(player, enemy);
    apply(enemy, player);
}
inline void apply_attack(community &player, community &enemy) {
    auto apply = [](community &p, community &e) {
        for (auto &attack : p.focus_.turn_context_.attack_context_.datas_) {
            if (attack.clock_.is_ringing()) {
                attack.executor_(p, e, attack);
            }
        }
    };
    apply(player, enemy);
    apply(enemy, player);
}
inline void apply_resource(community &player, community &enemy) {
    auto apply = [](community &p, community &e) {
        for (auto &data : p.focus_.turn_context_.resource_context_.datas_) {
            if (data.clock_.is_ringing()) {
                data.executor_(p, e, data);
            }
        }
    };
    apply(player, enemy);
    apply(enemy, player);
}
inline void round_pass(community &player, community &enemy) {
    auto apply = [](community &player) {
        player.focus_.defense_.round_pass();
        player.focus_.turn_context_.round_pass();
    };
    apply(player);
    apply(enemy);
}
inline void judge(community &player, community &enemy) {
    cancel_attack(player, enemy);
    apply_defense(player, enemy);
    apply_attack(player, enemy);
    apply_resource(player, enemy);
    round_pass(player, enemy);
}
inline void print_info(const community &player, const community &enemy) {
    std::cout << '\n';
    player.focus_.print_info();
    enemy.focus_.print_info();
}
//
//
// defend_attack_funcs
inline void common_reduction(community & /*player*/, community & /*enemy*/,
                             defense_entity &defense, attack_data &attack) {
    attack.total_damage_ += cancel(defense.power_, attack.power_);
}
inline void thorn_reduction(community &player, community & /*enemy*/,
                            defense_entity &defense, attack_data &attack) {
    auto damage = cancel(defense.power_, attack.power_);
    std::cout << "damage:" << damage << '\n';
    attack.total_damage_ += damage;
    if (attack.type_ == attack_type::PHYSICAL) {
        write_attack<attack_type::MAGICAL,
                     clap_round_clock{.delayed_rounds_ = 1}>((damage + 1) / 2,
                                                             player);
    }
}
//

// update_funcs
inline void default_update(defense_entity &defense) {
    defense.clock_.round_pass();
}
//

// merge_funcs

//

//
} // namespace blacksmith_core::domain