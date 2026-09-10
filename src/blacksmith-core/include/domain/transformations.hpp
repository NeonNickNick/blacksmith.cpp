#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <domain/models.hpp>
#include <skill-system/professions.hpp>
using namespace blacksmith_core::skill_system;
namespace blacksmith_core::domain {
// 辅助
inline int cancel(int &a, int &b) {
    assert(a >= 0 && b >= 0);
    auto res = std::abs(a - b);
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
//
// defend_attack_funcs
inline void common_reduction(community & /*player*/, community & /*enemy*/,
                             defense_entity &defense, attack_data &attack) {
    attack.total_damage_ += cancel(defense.power_, attack.power_);
}
//

// update_funcs
inline void default_update(defense_entity &defense) {
    defense.clock_.round_pass();
}
//

// merge_funcs

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
//
} // namespace blacksmith_core::domain