#pragma once
#include "blacksmith-ai.hpp"
#include "blacksmith-zero.hpp"
#include <domain/models.hpp>
#include <memory>
#include <random>
#include <tuple>
#include <utility>
#include <vector>
using namespace blacksmith::domain;
namespace blacksmith::blacksmith_master {

class blacksmith_zero_plus
    : public blacksmith_ai<blacksmith_zero_plus, blacksmith_zero_param> {
  public:
    skill_context choose_enemy_skill_impl(const community &player,
                                          const community &enemy, int round);

  private:
    std::vector<std::unique_ptr<mcts_node>>
    run_mcts(community &&player, community &&enemy, int iterations);
    skill_action
    sample_from_topk(std::vector<std::unique_ptr<mcts_node>> &children,
                     int round);
    static mcts_node *select(mcts_node *node);
    [[nodiscard]] std::tuple<skill_action, skill_action,
                             std::pair<float, float>>
    min_max_predict(const community &com, const community &other,
                    int round) const;
    [[nodiscard]] skill_action
    min_max_predict_single(const community &com, const community &other,
                           const skill_action &other_action, int round) const;
    [[nodiscard]] std::pair<float, float>
    evaluate(const community &player, const community &enemy, int round) const;
    static std::vector<skill_action> get_all_authorized(const community &com);
    static void play_round(community &player, community &enemy,
                           const skill_action &player_action,
                           const skill_action &enemy_action);
    static skill_context to_context(const skill_action &action,
                                    community &self);
    std::mt19937 random_{std::random_device{}()};
};
} // namespace blacksmith::blacksmith_master
