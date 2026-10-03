#include <algorithm>
#include <blacksmith-master/blacksmith-zero.hpp>
#include <cmath>
#include <cstddef>
#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <memory>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

using namespace blacksmith::domain;
namespace blacksmith::blacksmith_master {
namespace {
constexpr int ROLLOUT_DEPTH = 5;

bool terminal(const community &player, const community &enemy) {
    return player.focus_.health_.is_dead() || enemy.focus_.health_.is_dead();
}
} // namespace

std::vector<float> blacksmith_zero_param::to_vector() const {
    std::vector<float> res{};
    res.push_back(win_score_);

    res.push_back(early_iron_);
    res.push_back(early_excess_iron_);
    res.push_back(early_space_);
    res.push_back(early_time_);
    res.push_back(early_default_);

    res.push_back(mid_iron_);
    res.push_back(mid_excess_iron_);
    res.push_back(mid_space_);
    res.push_back(mid_time_);
    res.push_back(mid_default_);

    res.push_back(late_iron_);
    res.push_back(late_excess_iron_);
    res.push_back(late_space_);
    res.push_back(late_time_);
    res.push_back(late_default_);

    res.push_back(early_attack_penalty_);
    res.push_back(extra_profession_bonus_);
    res.push_back(late_round_penalty_);

    return res;
}
void blacksmith_zero_param::from_vector(const std::vector<float> &v) {
    win_score_ = v[0];

    early_iron_ = v[1];
    early_excess_iron_ = v[2];
    early_space_ = v[3];
    early_time_ = v[4];
    early_default_ = v[5];

    mid_iron_ = v[6];
    mid_excess_iron_ = v[7];
    mid_space_ = v[8];
    mid_time_ = v[9];
    mid_default_ = v[10];

    late_iron_ = v[11];
    late_excess_iron_ = v[12];
    late_space_ = v[13];
    late_time_ = v[14];
    late_default_ = v[15];

    early_attack_penalty_ = v[16];
    extra_profession_bonus_ = v[17];
    late_round_penalty_ = v[18];
}

mcts_node::mcts_node(community &&player, community &&enemy, mcts_node *parent,
                     std::vector<skill_action> &&actions, int round)
    : player_(std::make_unique<community>(std::move(player))),
      enemy_(std::make_unique<community>(std::move(enemy))), parent_(parent),
      untried_(std::move(actions)), round_(round) {}

skill_context blacksmith_zero::choose_enemy_skill_impl(const community &player,
                                                       const community &enemy,
                                                       int round) {
    auto children =
        run_mcts(community(player), community(enemy), param_.mcts_iterations_);
    return to_context(sample_from_topk(children, round),
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
            const auto PLAYER_ACTION = heuristic(next_player, next_enemy);
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
                heuristic(simulation_player, simulation_enemy);
            const auto ENEMY_ACTION =
                heuristic(simulation_enemy, simulation_player);
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

skill_action blacksmith_zero::heuristic(community &com, community & /*other*/) {
    auto actions = get_all_authorized(com);
    if (actions.empty()) {
        return {.skill_ = skill::IRON};
    }

    std::uniform_int_distribution<std::size_t> pick(0, actions.size() - 1);
    return std::move(actions[pick(random_)]);
}

skill_action blacksmith_zero::sample_from_topk(
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
        return -param_.win_score_;
    }
    if (PLAYER_HP <= 0.0F) {
        return param_.win_score_;
    }
    auto get_score = [this](const community &player, const community &enemy,
                            int round) {
        const auto EARLY = round < 8;
        const auto MID = round >= 8 && round < 15;
        const auto LATE = round >= 15;

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
        }

        if (LATE) {
            score -=
                static_cast<float>(round - 15) * param_.late_round_penalty_;
        }
        return score;
    };

    auto player_score = get_score(player, enemy, round);
    auto enemy_score = get_score(enemy, player, round); // NOLINT

    return enemy_score - (0.5F * player_score);
}

std::vector<skill_action>
blacksmith_zero::get_all_authorized(const community &com) {
    std::vector<skill_action> actions;
    actions.reserve(8);
    for (const auto &name : com.focus_.profession_.authorized_skills()) {
        if (name == skill::STICK || name == skill::DRILL ||
            name == skill::RECOVERY || name == skill::SHIELD ||
            name == skill::THORN_SHIELD || name == skill::MUTE) {
            continue;
        }
        skill_action ACTION{.skill_ = name, .param_ = 0};
        auto context = to_context(ACTION, const_cast<community &>(com));
        if (check_skill(com, context) == check_result::SUCCESS) {
            actions.emplace_back(std::move(ACTION));
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
} // namespace blacksmith::blacksmith_master
