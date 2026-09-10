#pragma once

#include <domain/models.hpp>
#include <domain/transformations.hpp>

// 此头文件定义了用于编写技能的DSL模型
using namespace blacksmith_core::domain;
namespace blacksmith_core::skill_system {

template <int power, attack_type type, clap_round_clock clock = {},
          attack_execute_func executor = &default_attack,
          float ap_factor = 1.0F>
void write_attack(community &player) {
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

} // namespace blacksmith_core::skill_system
