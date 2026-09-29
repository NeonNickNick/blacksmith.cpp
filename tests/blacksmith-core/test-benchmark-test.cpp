#include "battle-platform/standard-platform.hpp"
#include "blacksmith-master/blacksmith-ai.hpp"
#include <iostream>
int main() {
    blacksmith_core::battle_platform::benchmark_test<
        blacksmith_core::blacksmith_master::blacksmith_zero,
        blacksmith_core::blacksmith_master::blacksmith_zero>
        test;
    std::cout << test.win_rate(100) << '\n';
}