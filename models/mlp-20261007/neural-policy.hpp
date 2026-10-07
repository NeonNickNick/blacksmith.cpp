#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace blacksmith::neural {

// CPU inference for the trained policy. No Python or GPU runtime is needed.
class Policy {
    struct Dense {
        std::uint32_t inputs;
        std::uint32_t outputs;
        std::vector<float> weights;
        std::vector<float> bias;

        std::vector<float> evaluate(std::span<const float> input, bool activation) const {
            std::vector<float> output(outputs);
            for (std::uint32_t row = 0; row < outputs; ++row) {
                float value = bias[row];
                const float* weight = weights.data() + row * inputs;
                for (std::uint32_t column = 0; column < inputs; ++column) {
                    value += weight[column] * input[column];
                }
                output[row] = activation ? std::tanh(value) : value;
            }
            return output;
        }
    };

    std::uint32_t feature_count_;
    std::uint32_t action_count_;
    std::vector<Dense> layers_;

    static std::uint32_t read_integer(std::ifstream& file) {
        std::uint32_t value = 0;
        file.read(reinterpret_cast<char*>(&value), sizeof(value));
        return value;
    }

    static Dense read_layer(std::ifstream& file, std::uint32_t inputs, std::uint32_t outputs) {
        Dense layer{inputs, outputs, std::vector<float>(inputs * outputs), std::vector<float>(outputs)};
        file.read(reinterpret_cast<char*>(layer.weights.data()), layer.weights.size() * sizeof(float));
        file.read(reinterpret_cast<char*>(layer.bias.data()), layer.bias.size() * sizeof(float));
        return layer;
    }

public:
    explicit Policy(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        if (read_integer(file) != 0x314e5342) throw std::runtime_error("Unsupported neural policy format");
        feature_count_ = read_integer(file);
        action_count_ = read_integer(file);
        const auto hidden = read_integer(file);
        const auto depth = read_integer(file);
        if (feature_count_ > 4096 || action_count_ > 4096 || hidden > 4096 || depth != 3 ||
            !feature_count_ || !action_count_ || !hidden) {
            throw std::runtime_error("Invalid neural policy dimensions");
        }
        layers_.push_back(read_layer(file, feature_count_, hidden));
        for (std::uint32_t layer = 1; layer < depth; ++layer) layers_.push_back(read_layer(file, hidden, hidden));
        layers_.push_back(read_layer(file, hidden, action_count_));
    }

    std::uint32_t feature_count() const { return feature_count_; }
    std::uint32_t action_count() const { return action_count_; }

    std::vector<float> logits(std::span<const float> observation) const {
        if (observation.size() != feature_count_) throw std::runtime_error("Observation size mismatch");
        auto values = layers_.front().evaluate(observation, true);
        for (size_t layer = 1; layer < layers_.size(); ++layer) {
            values = layers_[layer].evaluate(values, layer + 1 != layers_.size());
        }
        return values;
    }

    int choose(std::span<const float> observation, std::span<const float> mask,
               std::mt19937& random, bool greedy = false) const {
        if (mask.size() != action_count_) throw std::runtime_error("Action mask size mismatch");
        const auto scores = logits(observation);
        float maximum = -std::numeric_limits<float>::infinity();
        int best = -1;
        for (size_t i = 0; i < scores.size(); ++i) {
            if (mask[i] > 0 && scores[i] > maximum) {
                maximum = scores[i];
                best = static_cast<int>(i);
            }
        }
        if (best < 0) throw std::runtime_error("No finite legal action");
        if (greedy) return best;
        std::vector<double> probabilities(scores.size());
        for (size_t i = 0; i < scores.size(); ++i) {
            if (mask[i] > 0) probabilities[i] = std::exp(static_cast<double>(scores[i] - maximum));
        }
        std::discrete_distribution<int> distribution(probabilities.begin(), probabilities.end());
        return distribution(random);
    }
};
}
