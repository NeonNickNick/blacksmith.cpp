#include <algorithm>
#include <blacksmith-master/blacksmith-ai.hpp>
#include <cmath>
#include <cstddef>
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

using namespace blacksmith_core::domain;
namespace blacksmith_core::blacksmith_master {
namespace {
constexpr int ROLLOUT_DEPTH = 5;

bool terminal(const community &player, const community &enemy) {
    return player.focus_.health_.is_dead() || enemy.focus_.health_.is_dead();
}
} // namespace

mcts_node::mcts_node(community &&player, community &&enemy, mcts_node *parent,
                     std::vector<skill_action> &&actions, int round)
    : player_(std::make_unique<community>(std::move(player))),
      enemy_(std::make_unique<community>(std::move(enemy))), parent_(parent),
      untried_(std::move(actions)), round_(round) {}

void blacksmith_zero::init_impl() {}

skill_context blacksmith_zero::choose_enemy_skill_impl(const community &player,
                                                       const community &enemy) {
    auto children =
        run_mcts(community(player), community(enemy), params_.mcts_iterations_);
    return to_context(sample_from_topk(children, 0),
                      const_cast<community &>(enemy));
}

float blacksmith_zero::predict_win_rate_impl(const community &player,
                                             const community &enemy) {
    return 1.0F / (1.0F + std::exp(-evaluate(player, enemy, 0) / 10.0F));
}

std::vector<std::unique_ptr<mcts_node>>
blacksmith_zero::run_mcts(community &&player, community &&enemy,
                          int iterations) {
    auto root_actions = get_all_authorized(enemy);
    mcts_node root(std::move(player), std::move(enemy), nullptr,
                   std::move(root_actions), 0);
    for (int i = 0; i < iterations; ++i) {
        mcts_node *node = &root;
        while (node->untried_.empty() && !node->children_.empty()) {
            node = select(node);
        }

        if (!node->untried_.empty() &&
            !terminal(*node->player_, *node->enemy_)) {

            auto ACTION = std::move(node->untried_.back());
            node->untried_.pop_back();

            community next_player = *node->player_;
            community next_enemy = *node->enemy_;
            const auto PLAYER_ACTION =
                heuristic(next_player, next_enemy, true, std::min(1, 0));
            play_round(next_player, next_enemy, PLAYER_ACTION, ACTION);
            auto next_actions = get_all_authorized(next_enemy);
            auto child = std::make_unique<mcts_node>(
                std::move(next_player), std::move(next_enemy), node,
                std::move(next_actions), node->round_ + 1);
            child->action_ = std::move(ACTION);
            node->children_.push_back(std::move(child));
            node = node->children_.back().get();
        }

        community simulation_player = *node->player_;
        community simulation_enemy = *node->enemy_;
        int round = node->round_;
        for (int depth = 0; depth < ROLLOUT_DEPTH &&
                            !terminal(simulation_player, simulation_enemy);
             ++depth, ++round) {
            // Rollouts dominate the cost of MCTS.  Their purpose is to provide
            // an inexpensive, unbiased value estimate; applying the recursive
            // greedy policy here makes each rollout branch exponentially.
            const auto PLAYER_ACTION =
                heuristic(simulation_player, simulation_enemy, true, 0);
            const auto ENEMY_ACTION =
                heuristic(simulation_player, simulation_enemy, false, 0);
            play_round(simulation_player, simulation_enemy, PLAYER_ACTION,
                       ENEMY_ACTION);
        }
        const float RESULT =
            evaluate(simulation_player, simulation_enemy, round);
        for (; node != nullptr; node = node->parent_) {
            ++node->visits_;
            node->wins_ += RESULT;
        }
    }
    return std::move(root.children_);
}

skill_action blacksmith_zero::heuristic(community &player, community &enemy,
                                        bool is_player, int depth) {
    community &actor = is_player ? player : enemy;
    auto actions = get_all_authorized(actor);
    if (actions.empty()) {
        return {.skill_name_ = "iron"};
    }
    std::uniform_real_distribution<float> chance(0.0F, 1.0F);
    if (depth <= 0 || chance(random_) >= params_.opponent_greedy_rate_) {
        std::uniform_int_distribution<std::size_t> pick(0, actions.size() - 1);
        return std::move(actions[pick(random_)]);
    }

    float best_score = -std::numeric_limits<float>::infinity();
    skill_action best = {.skill_name_ = "iron"};
    for (auto &action : actions) {
        community next_player = player;
        community next_enemy = enemy;
        const auto RESPONSE =
            heuristic(next_player, next_enemy, !is_player, depth - 1);
        if (is_player) {
            play_round(next_player, next_enemy, action, RESPONSE);
        } else {
            play_round(next_player, next_enemy, RESPONSE, action);
        }
        float score = evaluate(next_player, next_enemy, 0);
        if (is_player) {
            score = -score;
        }
        if (score > best_score) {
            best_score = score;
            best = std::move(action);
        }
    }
    return best;
}

skill_action blacksmith_zero::sample_from_topk(
    std::vector<std::unique_ptr<mcts_node>> &children, int round) {
    if (children.empty()) {
        return {.skill_name_ = "iron"};
    }
    std::ranges::sort(children, [](const auto &left, const auto &right) {
        return left->wins_ / (left->visits_ + 1e-6F) >
               right->wins_ / (right->visits_ + 1e-6F);
    });
    children.resize(std::min<std::size_t>(2, children.size()));
    const float TEMPERATURE = std::max(
        0.001F, params_.temperature_coefficient_ * static_cast<float>(round));
    const float MAX_SCORE =
        children.front()->wins_ /
        (static_cast<float>(children.front()->visits_) + 1e-6F);
    std::vector<float> weights;
    float sum = 0.0F;
    for (const auto &child : children) {
        const float SCORE =
            child->wins_ / (static_cast<float>(child->visits_) + 1e-6F);
        weights.push_back(std::exp((SCORE - MAX_SCORE) / TEMPERATURE));
        sum += weights.back();
    }
    std::uniform_real_distribution<float> pick(0.0F, sum);
    float threshold = pick(random_);
    for (std::size_t i = 0; i < children.size(); ++i) {
        threshold -= weights[i];
        if (threshold <= 0.0F) {
            return std::move(children[i]->action_);
        }
    }
    return std::move(children.back()->action_);
}

mcts_node *blacksmith_zero::select(mcts_node *node) {
    return std::ranges::max_element(
               node->children_,
               [node](const auto &left, const auto &right) {
                   const auto UCT = [node](const auto &child) {
                       const float MEAN =
                           child->wins_ / (child->visits_ + 1e-6F);
                       return MEAN + (std::numbers::sqrt2 *
                                      std::sqrt(std::log(static_cast<float>(
                                                             node->visits_) +
                                                         1.0F) /
                                                (child->visits_ + 1e-6F)));
                   };
                   return UCT(left) < UCT(right);
               })
        ->get();
}

float blacksmith_zero::evaluate(const community &player, const community &enemy,
                                int round) const {
    const auto ENEMY_HP = static_cast<float>(enemy.focus_.health_.hp_);
    const auto PLAYER_HP = static_cast<float>(player.focus_.health_.hp_);
    if (ENEMY_HP <= 0.0F) {
        return params_.lose_score_;
    }
    if (PLAYER_HP <= 0.0F) {
        return params_.win_score_;
    }
    const auto RESOURCE_SCORE = [](const community &com) {
        const auto &resource = com.focus_.resource_;
        return resource.query(resource_type::IRON) +
               (3.0F * resource.query(resource_type::SPACE)) +
               (2.0F * resource.query(resource_type::MAGIC));
    };
    const float ENEMY_RESOURCES = RESOURCE_SCORE(enemy);
    const float PLAYER_RESOURCES = RESOURCE_SCORE(player);
    return (10.0F * ((ENEMY_RESOURCES / (PLAYER_HP + 1e-6F)) -
                     (PLAYER_RESOURCES / (ENEMY_HP + 1e-6F)))) +
           (0.5F * (ENEMY_HP - PLAYER_HP)) - static_cast<float>(round);
}

std::vector<skill_action>
blacksmith_zero::get_all_authorized(const community &com) {
    std::vector<skill_action> actions;
    for (const auto &name : com.focus_.profession_.available_skills()) {
        if (name == "stick" || name == "drill" || name == "recovery" ||
            name == "shield" || name == "thornshield" || name == "mute") {
            continue;
        }
        skill_action ACTION{.skill_name_ = name, .param_ = 0};
        auto context = to_context(ACTION, const_cast<community &>(com));
        if (check_skill(com, context) == check_result::SUCCESS) {
            actions.push_back({.skill_name_ = name, .param_ = 0});
        }
    }
    return actions;
}

void blacksmith_zero::play_round(community &player, community &enemy,
                                 const skill_action &player_action,
                                 const skill_action &enemy_action) {
    auto player_context = to_context(player_action, player);
    auto enemy_context = to_context(enemy_action, enemy);
    declare(player, player_context, enemy, enemy_context);
    judge(player, enemy);
}

skill_context blacksmith_zero::to_context(const skill_action &action,
                                          community &self) {
    return {.action_ = action.copy(), .self_ = &self};
}
} // namespace blacksmith_core::blacksmith_master
