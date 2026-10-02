#pragma once
#include <domain/models.hpp>
#include <memory>
#include <random>
#include <vector>
using namespace blacksmith_core::domain;
namespace blacksmith_core::blacksmith_master {
template <typename Derived> class blacksmith_ai {
  public:
    void init() { static_cast<Derived *>(this)->init_impl(); }
    skill_context choose_enemy_skill(const community &player,
                                     const community &enemy, int round) {
        return static_cast<Derived *>(this)->choose_enemy_skill_impl(
            player, enemy, round);
    }
    float predict_win_rate(const community &player, const community &enemy) {
        return static_cast<Derived *>(this)->predict_win_rate_impl(player,
                                                                   enemy);
    }

  private:
    blacksmith_ai() = default;
    friend Derived;
};
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

struct blacksmith_zero_params {
  public:
    float win_score_ = 10.0F;
    float lose_score_ = -10.0F;
    float temperature_coefficient_ = 0.03F;
    int opponent_depth_ = 2;
    int mcts_iterations_ = 4000;
};
class blacksmith_zero : public blacksmith_ai<blacksmith_zero> {
  public:
    void init_impl();
    skill_context choose_enemy_skill_impl(const community &player,
                                          const community &enemy, int round);
    float predict_win_rate_impl(const community &player,
                                const community &enemy);

  private:
    blacksmith_zero_params params_;
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
} // namespace blacksmith_core::blacksmith_master
