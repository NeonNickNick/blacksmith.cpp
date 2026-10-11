#pragma once

#include "state.hpp"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace blacksmith::python_bindings::master {
inline state replay_prefix(const std::vector<std::array<int, 2>> &prefix) {
    state result;
    if (prefix.size() >= ROUND_LIMIT)
        throw std::invalid_argument("Prefix is too long");
    for (const auto &actions : prefix)
        result.step(actions[0], actions[1]);
    if (result.outcome())
        throw std::invalid_argument("Prefix ends in a terminal state");
    return result;
}
class arena {
  public:
    std::vector<state> games_;
    int threads_;
    arena(int count, int threads) : threads_(threads) {
        if (count <= 0 || threads <= 0)
            throw std::invalid_argument("Count and threads must be positive");
        games_.resize(static_cast<size_t>(count));
    }
    void reset(int index, const std::vector<std::array<int, 2>> &prefix) {
        games_.at(static_cast<size_t>(index)) = replay_prefix(prefix);
    }
};
class search_tree {
  public:
    std::vector<state> parents_;
    std::vector<bool> active_;
    int threads_;
    explicit search_tree(const arena &env)
        : parents_(env.games_), active_(parents_.size(), true),
          threads_(env.threads_) {}
};
} // namespace blacksmith::python_bindings::master
