#include "battle-platform/standard-platform.hpp"
#include "blacksmith-master/blacksmith-zero-plus.hpp"
#include <blacksmith-master/blacksmith-zero.hpp>
int main() {
    blacksmith::blacksmith_master::blacksmith_zero_param param;
    blacksmith::battle_platform::simple_pvp_with_ai<
        blacksmith::blacksmith_master::blacksmith_zero_plus,
        blacksmith::blacksmith_master::blacksmith_zero_param>
        pvp{param};
    pvp.play();
    return 0;
}
