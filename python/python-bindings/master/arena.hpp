#pragma once

#include "state.hpp"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>
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
    void reset_play(int index, int human_seat, float iron) {
        if ((human_seat != 0 && human_seat != 1) ||
            (iron != 0.0F && iron != 0.5F && iron != 1.0F))
            throw std::invalid_argument("Play requires seat 0/1 and iron 0/0.5/1");
        state game;
        game.players_[static_cast<size_t>(human_seat)].focus_.resource_.gain(
            resource_type::IRON, iron);
        games_.at(static_cast<size_t>(index)) = std::move(game);
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
    explicit search_tree(int threads) : threads_(threads) {}
    search_tree subset(const std::vector<size_t> &indices) const {
        search_tree result(threads_);
        result.parents_.reserve(indices.size());
        result.active_.reserve(indices.size());
        for (const auto index : indices) {
            result.parents_.push_back(parents_.at(index));
            result.active_.push_back(active_.at(index));
        }
        return result;
    }
};
} // namespace blacksmith::python_bindings::master
