#pragma once
#include <domain/models.hpp>
using namespace blacksmith::domain;
namespace blacksmith::blacksmith_master {
template <typename Derived, typename Param> class blacksmith_ai {
  public:
    Param param_;
    skill_context choose_enemy_skill(const community &player,
                                     const community &enemy, int round) {
        return static_cast<Derived *>(this)->choose_enemy_skill_impl(
            player, enemy, round);
    }
    float predict_win_rate(const community &player, const community &enemy) {
        return static_cast<Derived *>(this)->predict_win_rate_impl(player,
                                                                   enemy);
    }

  private:
    blacksmith_ai() = default;
    friend Derived;
};
} // namespace blacksmith::blacksmith_master
