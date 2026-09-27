#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <domain/models.hpp>
#include <iostream>
#include <skill-system/professions.hpp>
#include <string>
#include <utility>
using namespace blacksmith_core::skill_system;
namespace blacksmith_core::domain {
// 辅助
inline int cancel(int &a, int &b) {
    assert(a >= 0 && b >= 0);
    auto res = std::min(a, b);
    auto temp = a;
    a = std::max(0, a - b);
    b = std::max(0, b - temp);
    return res;
}
// 初始化
inline void initialize(community &player, community &enemy) {
    player.focus_.profession_.add_profession(SKILL_SET(common));
    enemy.focus_.profession_.add_profession(SKILL_SET(common));
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

template <context_data T>
void default_hook(community & /*player*/, community & /*enemy*/, T & /*data*/) {
};

struct attack_hook_set {
  public:
    attack_execute_func begin_{default_hook<attack_data>};
    attack_execute_func first_time_hit_armor_{default_hook<attack_data>};
    attack_execute_func hit_body_{default_hook<attack_data>};
    attack_execute_func end_{default_hook<attack_data>};
};
template <attack_hook_set hook_set = {}>
void execute_attack(community &player, community &enemy, attack_data &attack) {
    if (attack.power_ <= 0) {
        return;
    }
    hook_set.begin_(player, enemy, attack);
    body &main = enemy.focus_;
    bool hit_armor = false;
    if (attack.type_ != attack_type::REAL) {
        for (auto &d : main.defense_.defenses_) {
            for (auto type : ARMOR_TYPES) {
                if (d.type_ == type) {
                    if (!hit_armor) {
                        hit_armor = true;
                        hook_set.first_time_hit_armor_(player, enemy, attack);
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
                hook_set.end_(player, enemy, attack);
                return;
            }
        }
    }
    if (!hit_armor) {
        hook_set.first_time_hit_armor_(player, enemy, attack);
    }
    hook_set.hit_body_(player, enemy, attack);
    main.health_.lose_hp(attack.power_);
    attack.total_damage_ += attack.power_;
    hook_set.end_(player, enemy, attack);
}

template <defense_execute_func END = default_hook<defense_data>>
void execute_defense(community &player, community &enemy,
                     defense_data &defense) {
    player.focus_.defense_.add(defense.defense_);
    END(player, enemy, defense);
}
template <resource_execute_func END = default_hook<resource_data>>
void execute_resource(community &player, community &enemy,
                      resource_data &resource) {
    player.focus_.resource_.gain(resource.type_, resource.power_);
    END(player, enemy, resource);
}
template <effect_execute_func END = default_hook<effect_data>>
void execute_effect(community &player, community &enemy, effect_data &effect) {
    if (effect.type_ == effect_target_type::ENEMY) {
        enemy.focus_.effect_.add(effect.effect_);
    } else {
        player.focus_.effect_.add(effect.effect_);
    }
    END(player, enemy, effect);
}
// DSL
template <int power, attack_type type, clap_round_clock clock = {},
          attack_execute_func executor = &execute_attack,
          float ap_factor = 1.0F>
attack_data &write_attack(community &player) {
    return player.focus_.turn_context_.attack_context_.write(
        clock, type, power, ap_factor, executor, 0);
}

template <defense_entity defense, clap_round_clock clock = {},
          defense_execute_func executor = &execute_defense>
defense_data &write_defense(community &player) {
    return player.focus_.turn_context_.defense_context_.write(clock, defense,
                                                              executor);
}

template <effect_target_type type, effect_entity effect,
          clap_round_clock clock = {},
          effect_execute_func executor = &execute_effect>
effect_data &write_effect(community &player) {
    return player.focus_.turn_context_.effect_context_.write(clock, type,
                                                             effect, executor);
}

template <context_data T, T &(*getter_func)(community &),
          void (*modifier)(T &, community &)>
void modify(community &player) {
    modifier(getter_func(player), player);
}

template <clap_round_clock clock = {.is_infinite_ = true}>
void write_mark(community &player, std::string mark_name) {
    player.focus_.mark_.add({.clock_ = clock, .name_ = std::move(mark_name)});
}

inline int take_mark(community &player, const std::string &mark_name) {
    return static_cast<int>(
        std::erase_if(player.focus_.mark_.marks_, [&mark_name](const auto &m) {
            return m.name_ == mark_name;
        }));
}

inline int count_mark(const community &player, const std::string &mark_name) {
    return static_cast<int>(std::count_if(
        player.focus_.mark_.marks_.begin(), player.focus_.mark_.marks_.end(),
        [&mark_name](const auto &m) { return m.name_ == mark_name; }));
}

template <profession_func get_profession>
void write_profession(community &player) {
    player.focus_.profession_.add_profession(get_profession());
}

template <void (*free_transformation)(community &player)>
void write_free(community &player) {
    free_transformation(player);
}

template <float need, resource_type type, bool common_only = false>
void use_resource(community &player) {
    if constexpr (type == resource_type::HP) {
        player.focus_.health_.lose_hp(static_cast<int>(need));
    } else if constexpr (type == resource_type::MHP) {
        player.focus_.health_.lose_mhp(static_cast<int>(need));
    } else {
        player.focus_.resource_.use(type, need, common_only);
    }
}

template <float power, resource_type type, clap_round_clock clock = {},
          resource_execute_func executor = &execute_resource>
resource_data &write_resource(community &player) {
    return player.focus_.turn_context_.resource_context_.write(clock, type,
                                                               power, executor);
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
    player.focus_.profession_.invoke_passive(player_context);
    enemy.focus_.profession_.invoke_passive(enemy_context);
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
inline void apply_effect(community &player, community &enemy) {
    auto apply = [](community &p, community &e) {
        for (auto &data : p.focus_.turn_context_.effect_context_.datas_) {
            if (data.clock_.is_ringing()) {
                data.executor_(p, e, data);
            }
        }
    };
    apply(player, enemy);
    apply(enemy, player);
}
inline void take_effect(community &player, community &enemy) {
    auto take = [](community &p, community &e) {
        for (auto &eff : e.focus_.effect_.effects_) {
            if (eff.clock_.is_ringing()) {
                eff.take_effect_(p, e, eff);
            }
        }
    };
    take(player, enemy);
    take(enemy, player);
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
        player.focus_.effect_.round_pass();
        player.focus_.mark_.round_pass();
        player.focus_.turn_context_.round_pass();
    };
    apply(player);
    apply(enemy);
}
inline void judge(community &player, community &enemy) {
    cancel_attack(player, enemy);
    apply_effect(player, enemy);
    take_effect(player, enemy);
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
inline void default_reduction(community & /*player*/, community & /*enemy*/,
                              defense_entity &defense, attack_data &attack) {
    auto damage = std::min(defense.power_, attack.power_);
    attack.power_ -= damage;
}
inline void thorn_reduction(community &player, community & /*enemy*/,
                            defense_entity &defense, attack_data &attack) {
    auto damage = std::min(defense.power_, attack.power_);
    attack.power_ -= damage;
    if (attack.type_ == attack_type::PHYSICAL) {
        auto &d = write_attack<0, attack_type::MAGICAL,
                               clap_round_clock{.delayed_rounds_ = 1}>(player);
        d.power_ = (damage + 1) / 2;
    }
}
inline void default_armor(community & /*player*/, community & /*enemy*/,
                          defense_entity &defense, attack_data &attack) {
    cancel(defense.power_, attack.power_);
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