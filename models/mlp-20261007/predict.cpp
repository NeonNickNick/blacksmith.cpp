#include "neural-policy.hpp"
#include <iostream>
#include <iomanip>

// Input: 718 observation floats, then 156 legal-action mask floats.
// Output: one action index, or all pre-mask logits with --logits.
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) {
            std::cerr << "Usage: predict MODEL.bin [--logits]\n";
            return 2;
        }
        blacksmith::neural::Policy policy(argv[1]);
        std::vector<float> observation(policy.feature_count());
        std::vector<float> mask(policy.action_count());
        for (float& value : observation) {
            if (!(std::cin >> value) || !std::isfinite(value))
                throw std::runtime_error("Expected finite observation values");
        }
        for (float& value : mask) {
            if (!(std::cin >> value) || (value != 0 && value != 1))
                throw std::runtime_error("Expected a binary legal-action mask");
        }
        if (argc == 3) {
            if (std::string(argv[2]) != "--logits") throw std::runtime_error("Unknown option");
            std::cout << std::setprecision(9);
            for (float value : policy.logits(observation)) std::cout << value << ' ';
            std::cout << '\n';
        } else {
            std::mt19937 random(std::random_device{}());
            std::cout << policy.choose(observation, mask, random, false) << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
