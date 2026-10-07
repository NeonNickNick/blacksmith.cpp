#include "blacksmith-master/blacksmith-zero.hpp"
#include "skill-system/professions.hpp"
#include <algorithm>
#include <blacksmith-master/blacksmith-zero-plus.hpp>
#include <cmath>
#include <cstddef>
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <tuple>
#include <utility>
#include <vector>

using namespace blacksmith::domain;
namespace blacksmith::blacksmith_master {
namespace {
constexpr int ROLLOUT_DEPTH = 1;

bool terminal(const community &player, const community &enemy) {
    return player.focus_.health_.is_dead() || enemy.focus_.health_.is_dead();
}
} // namespace

skill_context blacksmith_zero_plus::choose_enemy_skill_impl(
    const community &player, const community &enemy, int round) {
    auto children =
        run_mcts(community(player), community(enemy), param_.mcts_iterations_);
    return to_context(sample_from_topk(children, round),
                      const_cast<community &>(enemy));
}

std::vector<std::unique_ptr<mcts_node>>
blacksmith_zero_plus::run_mcts(community &&player, community &&enemy,
                               int iterations) {
    auto combos = get_skill_combo()[static_cast<size_t>(enemy.current_skill_)];
    std::vector<skill_action> combo_actions;
    if (!combos.empty()) {
        combos.erase(std::ranges::remove_if(
                         combos,
                         [&](const auto &c) {
                             return enemy.focus_.profession_.check(
                                        {.action_ = {c}, .self_ = &enemy}) !=
                                    check_result::SUCCESS;
                         })
                         .begin(),
                     combos.end());
        for (const auto S : combos) {
            combo_actions.push_back({S});
        }
    }
    auto root_actions =
        combos.empty() ? get_all_authorized(enemy) : std::move(combo_actions);
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
            const skill_action PLAYER_ACTION = min_max_predict_single(
                next_player, next_enemy, ACTION, node->round_ + 1);
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
        int depth = 0;
        float score = 0;
        for (; depth < ROLLOUT_DEPTH &&
               !terminal(simulation_player, simulation_enemy);
             ++depth, ++round) {
            // Rollouts dominate the cost of MCTS.  Their purpose is to provide
            // an inexpensive, unbiased value estimate; applying the recursive
            // greedy policy here makes each rollout branch exponentially.
            const auto RESULT =
                min_max_predict(simulation_player, simulation_enemy, round);
            score += std::get<2>(RESULT).second;
        }

        for (; node != nullptr; node = node->parent_) {
            ++node->visits_;
            node->wins_ += score / static_cast<float>(depth + 1);
        }
    }
    return std::move(root.children_);
}
namespace {}
std::tuple<skill_action, skill_action, std::pair<float, float>>
blacksmith_zero_plus::min_max_predict(const community &com,
                                      const community &other, int round) const {
    auto get_actions = [](const community &com) {
        auto actions = get_all_authorized(com);
        auto combos =
            get_skill_combo()[static_cast<size_t>(com.current_skill_)];
        if (!combos.empty()) {
            combos.erase(
                std::ranges::remove_if(
                    combos,
                    [&](const auto &c) {
                        return com.focus_.profession_.check(
                                   {.action_ = {c},
                                    .self_ = &const_cast<community &>(com)}) !=
                               check_result::SUCCESS;
                    })
                    .begin(),
                combos.end());
        }
        if (!combos.empty()) {
            actions.clear();
            for (auto skill : combos) {
                actions.push_back({.skill_ = skill});
            }
        }
        return actions;
    };
    auto com_actions = get_actions(com);
    auto other_actions = get_actions(other);
    auto m = com_actions.size();
    auto n = other_actions.size();
    std::vector<std::vector<float>> com_scores(m, std::vector<float>(n));
    std::vector<std::vector<float>> other_scores(m, std::vector<float>(n));
    for (size_t i = 0; i < m; ++i) {
        for (size_t j = 0; j < n; ++j) {
            auto sim_com = com;
            auto sim_other = other;
            play_round(sim_com, sim_other, com_actions[i], other_actions[j]);
            auto s = evaluate(sim_com, sim_other, round);
            com_scores[i][j] = s.first;
            other_scores[i][j] = s.second;
        }
    }
    float com_max = std::numeric_limits<float>::min();
    size_t com_index = 0;
    for (size_t i = 0; i < m; ++i) {
        float local_min = com_scores[i][0];
        for (size_t j = 0; j < n; ++j) {
            local_min = std::min(com_scores[i][j], local_min);
        }
        if (local_min > com_max) {
            com_max = local_min;
            com_index = i;
        }
    }
    float other_max = std::numeric_limits<float>::min();
    size_t other_index = 0;
    for (size_t j = 0; j < n; ++j) {
        float local_min = other_scores[0][j];
        for (size_t i = 0; i < m; ++i) {
            local_min = std::min(other_scores[i][j], local_min);
        }
        if (local_min > other_max) {
            other_max = local_min;
            other_index = j;
        }
    }
    return {std::move(com_actions[com_index]),
            std::move(other_actions[other_index]),
            std::pair{com_scores[com_index][other_index],
                      other_scores[com_index][other_index]}};
}
skill_action blacksmith_zero_plus::min_max_predict_single(
    const community &com, const community &other,
    const skill_action &other_action, int round) const {
    auto get_actions = [](const community &com) {
        auto actions = get_all_authorized(com);
        auto combos =
            get_skill_combo()[static_cast<size_t>(com.current_skill_)];
        if (!combos.empty()) {
            combos.erase(
                std::ranges::remove_if(
                    combos,
                    [&](const auto &c) {
                        return com.focus_.profession_.check(
                                   {.action_ = {c},
                                    .self_ = &const_cast<community &>(com)}) !=
                               check_result::SUCCESS;
                    })
                    .begin(),
                combos.end());
        }
        if (!combos.empty()) {
            actions.clear();
            for (auto skill : combos) {
                actions.push_back({.skill_ = skill});
            }
        }
        return actions;
    };
    auto com_actions = get_actions(com);
    auto m = com_actions.size();
    std::vector<float> com_scores(m);
    for (size_t i = 0; i < m; ++i) {
        auto sim_com = com;
        auto sim_other = other;
        play_round(sim_com, sim_other, com_actions[i], other_action);
        auto s = evaluate(sim_com, sim_other, round);
        com_scores[i] = s.first;
    }
    float max_score = std::numeric_limits<float>::min();
    size_t index = 0;
    for (size_t i = 0; i < m; ++i) {
        if (com_scores[i] > max_score) {
            max_score = com_scores[i];
            index = i;
        }
    }
    return std::move(com_actions[index]);
}

skill_action blacksmith_zero_plus::sample_from_topk(
    std::vector<std::unique_ptr<mcts_node>> &children, int round) {
    if (children.empty()) {
        return {.skill_ = skill::IRON};
    }
    std::ranges::sort(children, [](const auto &left, const auto &right) {
        return left->wins_ / (left->visits_ + 1e-6F) >
               right->wins_ / (right->visits_ + 1e-6F);
    });
    children.resize(std::min<std::size_t>(2, children.size()));
    const float TEMPERATURE = std::max(0.001F, param_.temperature_coefficient_ *
                                                   static_cast<float>(round));
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

mcts_node *blacksmith_zero_plus::select(mcts_node *node) {
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

std::pair<float, float> blacksmith_zero_plus::evaluate(const community &player,
                                                       const community &enemy,
                                                       int round) const {
    const auto ENEMY_HP = static_cast<float>(enemy.focus_.health_.hp_);
    const auto PLAYER_HP = static_cast<float>(player.focus_.health_.hp_);
    if (round >= 15) {
        return {-param_.win_score_, -param_.win_score_};
    }
    if (ENEMY_HP <= 0.0F) {
        return {param_.win_score_, -param_.win_score_};
    }
    if (PLAYER_HP <= 0.0F) {
        return {-param_.win_score_, param_.win_score_};
    }
    auto get_score = [this](const community &player, const community &enemy,
                            int round) {
        const auto EARLY = round < 7;
        const auto MID = round >= 7 && round < 12;
        const auto LATE = round >= 12;

        // auto player_hp = player.focus_.health_.hp_;
        auto enemy_hp = enemy.focus_.health_.hp_;
        auto enemy_mhp = enemy.focus_.health_.mhp_;

        auto player_iron = player.focus_.resource_.query(resource_type::IRON);
        auto player_space = player.focus_.resource_.query(resource_type::SPACE);
        auto player_time = player.focus_.resource_.query(resource_type::TIME);
        auto player_default =
            player.focus_.resource_.query(resource_type::MAGIC);

        bool have_extra_profession =
            player.focus_.profession_.have_extra_profession_;

        float score = 0;
        float resource_score = 0;
        if (EARLY) {
            resource_score += player_iron * param_.early_iron_;
            if (player_iron > 4) {
                resource_score += (player_iron - 4) * param_.early_excess_iron_;
            }
            resource_score += player_space * param_.early_space_;
            resource_score += player_time * param_.early_time_;
            resource_score += player_default * param_.early_default_;
        } else if (MID) {
            resource_score += player_iron * param_.mid_iron_;
            if (player_iron > 4) {
                resource_score += (player_iron - 4) * param_.mid_excess_iron_;
            }
            resource_score += player_space * param_.mid_space_;
            resource_score += player_time * param_.mid_time_;
            resource_score += player_default * param_.mid_default_;
        } else if (LATE) {
            resource_score += player_iron * param_.late_iron_;
            if (player_iron > 4) {
                resource_score += (player_iron - 4) * param_.late_excess_iron_;
            }
            resource_score += player_space * param_.late_space_;
            resource_score += player_time * param_.late_time_;
            resource_score += player_default * param_.late_default_;
        }
        score += resource_score;

        if (have_extra_profession) {
            score += param_.extra_profession_bonus_;
        }

        if (EARLY) {
            score -= static_cast<float>(enemy_mhp - enemy_hp) *
                     param_.early_attack_penalty_;
        } else {
            score += static_cast<float>(enemy_mhp - enemy_hp) *
                     param_.early_attack_penalty_ * 4;
        }

        if (LATE) {
            score -=
                static_cast<float>(round - 12) * param_.late_round_penalty_;
        }
        return score;
    };

    auto player_score = get_score(player, enemy, round);
    auto enemy_score = get_score(enemy, player, round); // NOLINT

    return {player_score, enemy_score};
}

std::vector<skill_action>
blacksmith_zero_plus::get_all_authorized(const community &com) {
    std::vector<skill_action> actions;
    actions.reserve(7);
    for (const auto &name : com.focus_.profession_.authorized_skills()) {

        if (name == skill::STICK || name == skill::DRILL ||
            name == skill::RECOVERY || name == skill::SHIELD ||
            name == skill::THORN_SHIELD || name == skill::MUTE) {
            continue;
        }
        for (int i = 0; i < 5; ++i) {
            skill_action ACTION{.skill_ = name, .param_ = i};
            auto context = to_context(ACTION, const_cast<community &>(com));
            if (check_skill(com, context) == check_result::SUCCESS) {
                actions.emplace_back(std::move(ACTION));
            }
            if (name != skill::MAGIC_ATTACK && name != skill::MAGIC_SHIELD) {
                break;
            }
        }
    }
    return actions;
}

void blacksmith_zero_plus::play_round(community &player, community &enemy,
                                      const skill_action &player_action,
                                      const skill_action &enemy_action) {
    auto player_context = to_context(player_action, player);
    auto enemy_context = to_context(enemy_action, enemy);
    declare(player, player_context, enemy, enemy_context);
    judge(player, enemy);
}

skill_context blacksmith_zero_plus::to_context(const skill_action &action,
                                               community &self) {
    return {.action_ = action.copy(), .self_ = &self};
}
} // namespace blacksmith::blacksmith_master
