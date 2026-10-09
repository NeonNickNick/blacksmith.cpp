#include "neural-policy.hpp"
#include <iomanip>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    try {
        blacksmithzero::Network network(argc > 1 ? argv[1] : "policy.bin");
        std::vector<float> state(718);
        for (float& value : state) {
            if (!(std::cin >> value)) {
                std::cerr << "Expected 718 state features.\n";
                return 1;
            }
        }
        const auto prediction = network.evaluate(state);
        std::cout << std::setprecision(9);
        for (float score : prediction.logits) {
            std::cout << score << ' ';
        }
        std::cout << prediction.value << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
