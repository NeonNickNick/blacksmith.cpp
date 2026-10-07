#pragma once
#include "blacksmith-ai.hpp"
#include "domain/models.hpp"
namespace blacksmith::blacksmith_master {
class crazy_lancer : public blacksmith_ai<crazy_lancer, bool> {
  public:
    skill_context choose_enemy_skill_impl(const community &player,
                                          const community &enemy, int round);
};
class random_lancer : public blacksmith_ai<crazy_lancer, bool> {
  public:
    skill_context choose_enemy_skill_impl(const community &player,
                                          const community &enemy, int round);
};
} // namespace blacksmith::blacksmith_master