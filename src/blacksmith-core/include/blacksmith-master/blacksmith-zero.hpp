#pragma once
#include "blacksmith-ai.hpp"
#include <domain/models.hpp>
#include <memory>
#include <random>
#include <vector>
using namespace blacksmith::domain;
namespace blacksmith::blacksmith_master {
struct mcts_node {
  public:
    std::unique_ptr<community> player_{nullptr};
    std::unique_ptr<community> enemy_{nullptr};
    mcts_node *parent_{nullptr};
    std::vector<std::unique_ptr<mcts_node>> children_;
    skill_action action_{};
    int visits_{0};
    float wins_{0.0F};
    std::vector<skill_action> untried_;
    mcts_node(community &&player, community &&enemy, mcts_node *parent,
              std::vector<skill_action> &&actions, int round);
    int round_ = 0;
};

struct blacksmith_zero_param {
  public:
    float win_score_ = 100.0F;

    float early_iron_ = 20;
    float early_excess_iron_ = 15;
    float early_space_ = 4;
    float early_time_ = 3;
    float early_default_ = 3;

    float mid_iron_ = 12;
    float mid_excess_iron_ = 10;
    float mid_space_ = 9;
    float mid_time_ = 6;
    float mid_default_ = 7;

    float late_iron_ = 3;
    float late_excess_iron_ = 3;
    float late_space_ = 3;
    float late_time_ = 3;
    float late_default_ = 4;

    float early_attack_penalty_ = 30;
    float extra_profession_bonus_ = 10;
    float late_round_penalty_ = 40;

    int mcts_iterations_ = 4000;
    float temperature_coefficient_ = 0.03F;
    [[nodiscard]] std::vector<float> to_vector() const;
    void from_vector(const std::vector<float> &v);
};
class blacksmith_zero
    : public blacksmith_ai<blacksmith_zero, blacksmith_zero_param> {
  public:
    skill_context choose_enemy_skill_impl(const community &player,
                                          const community &enemy, int round);
    float predict_win_rate_impl(const community &player,
                                const community &enemy);

  private:
    std::vector<std::unique_ptr<mcts_node>>
    run_mcts(community &&player, community &&enemy, int iterations);
    skill_action heuristic(community &com, community &other);
    skill_action
    sample_from_topk(std::vector<std::unique_ptr<mcts_node>> &children,
                     int round);
    static mcts_node *select(mcts_node *node);
    [[nodiscard]] float evaluate(const community &player,
                                 const community &enemy, int round) const;
    static std::vector<skill_action> get_all_authorized(const community &com);
    static void play_round(community &player, community &enemy,
                           const skill_action &player_action,
                           const skill_action &enemy_action);
    static skill_context to_context(const skill_action &action,
                                    community &self);
    std::mt19937 random_{std::random_device{}()};
};
} // namespace blacksmith::blacksmith_master
