#pragma once

#include "actions.hpp"
#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace blacksmith::python_bindings::master {
inline constexpr int ROUND_LIMIT = 100;
inline constexpr int HISTORY_LENGTH = 8;
inline constexpr int HISTORY_PLAYER_LENGTH = 8;
inline constexpr int HISTORY_TOKEN_LENGTH = 4 + 2 * HISTORY_PLAYER_LENGTH;

struct history_record {
    std::array<int, 2> actions_{};
    std::array<std::array<float, HISTORY_PLAYER_LENGTH>, 2> players_{};
    int round_{};
};
class state {
  public:
    std::array<community, 2> players_{};
    int round_ = 1;
    std::array<history_record, HISTORY_LENGTH> history_{};
    int history_size_ = 0;

    state() { initialize(players_[0], players_[1]); }
    [[nodiscard]] bool legal(int seat, int index) {
        if (seat < 0 || seat > 1 || index < 0 || index >= SKILL_NUM)
            return false;
        auto &player = players_[static_cast<size_t>(seat)];
        return check_skill(player, context(player, index)) ==
               check_result::SUCCESS;
    }
    [[nodiscard]] int outcome() const {
        const bool first = players_[0].focus_.health_.is_dead();
        const bool second = players_[1].focus_.health_.is_dead();
        return first && second        ? 3
               : first                ? 2
               : second               ? 1
               : round_ > ROUND_LIMIT ? 4
                                      : 0;
    }
    void step(int first, int second) {
        if (outcome() || !legal(0, first) || !legal(1, second))
            throw std::invalid_argument(
                "Illegal joint action or finished game");
        const std::array before{measure(players_[0]), measure(players_[1])};
        auto a = context(players_[0], first);
        auto b = context(players_[1], second);
        declare(players_[0], a, players_[1], b);
        judge(players_[0], players_[1]);
        history_record record{.actions_ = {first, second}, .round_ = round_};
        for (size_t seat = 0; seat < 2; ++seat) {
            const auto after = measure(players_[seat]);
            record.players_[seat][0] = after[0];
            for (size_t i = 0; i < after.size(); ++i)
                record.players_[seat][i + 1] = after[i] - before[seat][i];
        }
        if (history_size_ == HISTORY_LENGTH) {
            std::ranges::rotate(history_, history_.begin() + 1);
            history_.back() = record;
        } else {
            history_[static_cast<size_t>(history_size_++)] = record;
        }
        ++round_;
    }

  private:
    static skill_context context(community &player, int index) {
        const auto &a = skill_table[static_cast<size_t>(index)];
        return {.action_ = {.skill_ = a.skill_, .param_ = a.param_},
                .self_ = &player};
    }
    static std::array<float, HISTORY_PLAYER_LENGTH - 1>
    measure(const community &player) {
        const auto &body = player.focus_;
        return {static_cast<float>(body.health_.hp_) / 10.0F,
                static_cast<float>(body.health_.mhp_) / 10.0F,
                body.resource_.query_common(resource_type::IRON) / 10.0F,
                body.resource_.query_gold(resource_type::IRON) / 10.0F,
                body.resource_.query(resource_type::MAGIC) / 10.0F,
                body.resource_.query(resource_type::SPACE) / 10.0F,
                body.resource_.query(resource_type::TIME) / 10.0F};
    }
};
} // namespace blacksmith::python_bindings::master
