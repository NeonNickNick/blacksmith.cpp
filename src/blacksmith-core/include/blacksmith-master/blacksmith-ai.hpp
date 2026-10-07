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

  private:
    friend Derived;
};
} // namespace blacksmith::blacksmith_master
