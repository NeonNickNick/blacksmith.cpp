#include "battle-platform/standard-platform.hpp"
#include "blacksmith-master/blacksmith-ai.hpp"
#include <chrono>
#include <iostream>

int main() {
    auto begin = std::chrono::steady_clock::now();
    blacksmith_core::battle_platform::benchmark_test<
        blacksmith_core::blacksmith_master::blacksmith_zero,
        blacksmith_core::blacksmith_master::blacksmith_zero>
        test;
    std::cout << test.win_rate(1000) << '\n';
    auto now = std::chrono::steady_clock::now();
    auto elapsed_s =
        std::chrono::duration_cast<std::chrono::seconds>(now - begin).count();
    std::cout << elapsed_s << "s passed.\n";
}