#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace blacksmithzero {
struct Prediction { std::vector<float> logits; float value; };
class Network {
    struct Dense {
        int inputs, outputs;
        std::vector<float> weights, bias;
        std::vector<float> apply(std::span<const float> input, bool activate) const {
            std::vector<float> result(outputs);
            for (int row = 0; row < outputs; ++row) {
                float total = bias[row];
                for (int column = 0; column < inputs; ++column)
                    total += weights[row * inputs + column] * input[column];
                result[row] = activate ? std::tanh(total) : total;
            }
            return result;
        }
    };
    int features_, actions_;
    std::vector<Dense> layers_;
    Dense actor_, critic_;
    static std::uint32_t integer(std::ifstream& input) {
        std::uint32_t value;
        input.read(reinterpret_cast<char*>(&value), sizeof(value));
        return value;
    }
    static Dense layer(std::ifstream& input, int from, int to) {
        Dense result{from, to, std::vector<float>(from * to), std::vector<float>(to)};
        input.read(reinterpret_cast<char*>(result.weights.data()), result.weights.size() * sizeof(float));
        input.read(reinterpret_cast<char*>(result.bias.data()), result.bias.size() * sizeof(float));
        return result;
    }
public:
    explicit Network(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        input.exceptions(std::ios::failbit | std::ios::badbit);
        if (integer(input) != 0x31525a42) throw std::runtime_error("Wrong Zero model format");
        features_ = integer(input);
        actions_ = integer(input);
        int hidden = integer(input), depth = integer(input);
        if (features_ != 718 || actions_ != 156 || hidden < 1 || hidden > 4096 || depth != 3)
            throw std::runtime_error("Unsupported Zero architecture");
        layers_.push_back(layer(input, features_, hidden));
        for (int i = 1; i < depth; ++i) layers_.push_back(layer(input, hidden, hidden));
        actor_ = layer(input, hidden, actions_);
        critic_ = layer(input, hidden, 1);
    }
    Prediction evaluate(std::span<const float> state) const {
        if (state.size() != features_) throw std::runtime_error("Wrong Zero observation shape");
        auto hidden = layers_.front().apply(state, true);
        for (size_t i = 1; i < layers_.size(); ++i) hidden = layers_[i].apply(hidden, true);
        return {actor_.apply(hidden, false), std::tanh(critic_.apply(hidden, false)[0])};
    }
};
}
