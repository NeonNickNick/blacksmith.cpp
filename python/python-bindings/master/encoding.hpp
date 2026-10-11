#pragma once

#include "actions.hpp"
#include "domain/models.hpp"
#include "state.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace blacksmith::python_bindings::master {
inline constexpr int SCHEMA_VERSION = 3;
inline constexpr int DELAY_BINS = 3;
inline constexpr int CLOCK_LENGTH = 3;
inline constexpr int ATTACK_UNIT_LENGTH = 4;
inline constexpr int DEFENSE_UNIT_LENGTH = 3;
inline constexpr int PROFESSION_ONE_HOT_OFFSET = 6;
inline constexpr int AUTHORIZED_OFFSET =
    PROFESSION_ONE_HOT_OFFSET + static_cast<int>(profession_type::SIZE);
inline constexpr int ATTACK_OFFSET =
    AUTHORIZED_OFFSET + static_cast<int>(skill::SIZE);
inline constexpr int DEFENSE_OFFSET =
    ATTACK_OFFSET +
    static_cast<int>(attack_type::SIZE) * DELAY_BINS * ATTACK_UNIT_LENGTH;
inline constexpr int PENDING_DEFENSE_OFFSET =
    DEFENSE_OFFSET + static_cast<int>(defense_type::SIZE) * DEFENSE_UNIT_LENGTH;
inline constexpr int COMMON_ENCODING_LENGTH =
    PENDING_DEFENSE_OFFSET +
    static_cast<int>(defense_type::SIZE) * DELAY_BINS * DEFENSE_UNIT_LENGTH;
inline constexpr int MARK_COUNT = static_cast<int>(mark_id::BLOODSIGIL) + 1;
inline constexpr std::array RESOURCE_TYPES{
    resource_type::IRON, resource_type::GOLD_IRON, resource_type::SPACE,
    resource_type::TIME, resource_type::MAGIC,     resource_type::HP,
    resource_type::MHP};
inline constexpr int MARK_OFFSET = 4;
inline constexpr int PENDING_RESOURCE_OFFSET =
    MARK_OFFSET + MARK_COUNT * DELAY_BINS * CLOCK_LENGTH;
inline constexpr int CALLBACK_OFFSET =
    PENDING_RESOURCE_OFFSET +
    static_cast<int>(RESOURCE_TYPES.size()) * DELAY_BINS * CLOCK_LENGTH;
inline constexpr int EFFECT_OFFSET =
    CALLBACK_OFFSET +
    static_cast<int>(callback_stage::SIZE) * DELAY_BINS * CLOCK_LENGTH;
inline constexpr int PROFESSION_ENCODING_LENGTH =
    EFFECT_OFFSET + 3 * DELAY_BINS * CLOCK_LENGTH;
inline constexpr int GLOBAL_OFFSET =
    2 * (COMMON_ENCODING_LENGTH + PROFESSION_ENCODING_LENGTH);
inline constexpr int STATE_FEATURES = GLOBAL_OFFSET + 2;
inline constexpr int HISTORY_OFFSET = STATE_FEATURES;
inline constexpr int OBSERVATION_LENGTH =
    STATE_FEATURES + HISTORY_LENGTH * HISTORY_TOKEN_LENGTH;

inline bool authorized(const community &player, skill move) {
    const auto &skills = player.focus_.profession_.authorized_skills();
    return std::ranges::find(skills, move) != skills.end();
}
inline int role(const community &player) {
    return static_cast<int>(player.focus_.profession_.profession_);
}
inline int delay_bin(const clap_round_clock &clock) {
    return std::clamp(clock.delayed_rounds_, 0, DELAY_BINS - 1);
}
// Clock channels: count/power, total finite lifetime, infinite count.
inline void add_clock(std::span<float, CLOCK_LENGTH> out,
                      const clap_round_clock &clock, float value) {
    if (clock.is_dead())
        return;
    out[0] += value;
    out[1] += clock.is_infinite_
                  ? 0.0F
                  : static_cast<float>(clock.remaining_rounds_) / 5.0F;
    out[2] += clock.is_infinite_ ? 1.0F : 0.0F;
}
inline void encode_common(const community &player,
                          std::span<float, COMMON_ENCODING_LENGTH> out) {
    const auto &body = player.focus_;
    out[0] = static_cast<float>(body.health_.hp_) / 10.0F;
    out[1] = static_cast<float>(body.health_.mhp_) / 10.0F;
    out[2] = static_cast<float>(body.health_.mhp_ - body.health_.hp_) / 10.0F;
    out[3] = body.resource_.query_common(resource_type::IRON) / 10.0F;
    out[4] = body.resource_.query(resource_type::SPACE) / 10.0F;
    out[5] = body.resource_.query(resource_type::TIME) / 10.0F;
    out[PROFESSION_ONE_HOT_OFFSET + role(player)] = 1.0F;
    for (const auto move : body.profession_.authorized_skills())
        out[AUTHORIZED_OFFSET + static_cast<int>(move)] = 1.0F;
    for (const auto &attack : body.turn_context_.attack_context_.datas_) {
        if (attack.clock_.is_dead() || attack.power_ <= 0)
            continue;
        const int offset =
            ATTACK_OFFSET + (static_cast<int>(attack.type_) * DELAY_BINS +
                             delay_bin(attack.clock_)) *
                                ATTACK_UNIT_LENGTH;
        out[offset] += static_cast<float>(attack.power_) / 10.0F;
        out[offset + 1] +=
            static_cast<float>(attack.power_) * attack.ap_factor_ / 10.0F;
        out[offset + 2] +=
            attack.clock_.is_infinite_
                ? 0.0F
                : static_cast<float>(attack.clock_.remaining_rounds_) / 5.0F;
        out[offset + 3] += attack.clock_.is_infinite_ ? 1.0F : 0.0F;
    }
    for (const auto &defense : body.defense_.defenses_) {
        const int offset = DEFENSE_OFFSET + static_cast<int>(defense.type_) *
                                                DEFENSE_UNIT_LENGTH;
        add_clock(
            std::span<float, CLOCK_LENGTH>{out.data() + offset, CLOCK_LENGTH},
            defense.clock_, static_cast<float>(defense.power_) / 10.0F);
    }
    for (const auto &defense : body.turn_context_.defense_context_.datas_) {
        if (defense.clock_.is_dead() || defense.defense_.clock_.is_dead())
            continue;
        const int offset =
            PENDING_DEFENSE_OFFSET +
            (static_cast<int>(defense.defense_.type_) * DELAY_BINS +
             delay_bin(defense.clock_)) *
                DEFENSE_UNIT_LENGTH;
        add_clock(
            std::span<float, CLOCK_LENGTH>{out.data() + offset, CLOCK_LENGTH},
            defense.defense_.clock_,
            static_cast<float>(defense.defense_.power_) / 10.0F);
    }
}
inline void
encode_profession(const community &player,
                  std::span<float, PROFESSION_ENCODING_LENGTH> out) {
    const auto &body = player.focus_;
    out[0] = body.resource_.query(resource_type::MAGIC) / 10.0F;
    out[1] = body.resource_.query_gold(resource_type::IRON) / 10.0F;
    out[2] = authorized(player, skill::MIDASTOUCH) ? 1.0F : 0.0F;
    out[3] = body.profession_.have_extra_profession_ ? 1.0F : 0.0F;
    const auto clock = [&](int offset, const clap_round_clock &time,
                           float value = .5F) {
        add_clock(
            std::span<float, CLOCK_LENGTH>{out.data() + offset +
                                               delay_bin(time) * CLOCK_LENGTH,
                                           CLOCK_LENGTH},
            time, value);
    };
    for (const auto &mark : body.mark_.marks_)
        clock(MARK_OFFSET +
                  static_cast<int>(mark.id_) * DELAY_BINS * CLOCK_LENGTH,
              mark.clock_);
    for (const auto &resource : body.turn_context_.resource_context_.datas_) {
        const auto found = std::ranges::find(RESOURCE_TYPES, resource.type_);
        if (found != RESOURCE_TYPES.end())
            clock(PENDING_RESOURCE_OFFSET +
                      static_cast<int>(found - RESOURCE_TYPES.begin()) *
                          DELAY_BINS * CLOCK_LENGTH,
                  resource.clock_, resource.power_ / 10.0F);
    }
    for (const auto &callback : body.turn_context_.callback_context_.datas_)
        clock(CALLBACK_OFFSET +
                  static_cast<int>(callback.stage_) * DELAY_BINS * CLOCK_LENGTH,
              callback.clock_);
    for (const auto &effect : body.effect_.effects_)
        clock(EFFECT_OFFSET, effect.clock_);
    for (const auto &effect : body.turn_context_.effect_context_.datas_) {
        const int target = effect.type_ == effect_target_type::SELF ? 1 : 2;
        clock(EFFECT_OFFSET + target * DELAY_BINS * CLOCK_LENGTH,
              effect.clock_);
    }
}
inline std::array<float, OBSERVATION_LENGTH> encode(const state &game,
                                                    int seat) {
    std::array<float, OBSERVATION_LENGTH> out{};
    for (int view = 0; view < 2; ++view) {
        const auto &player =
            game.players_[static_cast<size_t>(view == 0 ? seat : 1 - seat)];
        encode_common(player, std::span<float, COMMON_ENCODING_LENGTH>{
                                  out.data() + view * COMMON_ENCODING_LENGTH,
                                  COMMON_ENCODING_LENGTH});
        encode_profession(player, std::span<float, PROFESSION_ENCODING_LENGTH>{
                                      out.data() + 2 * COMMON_ENCODING_LENGTH +
                                          view * PROFESSION_ENCODING_LENGTH,
                                      PROFESSION_ENCODING_LENGTH});
    }
    out[GLOBAL_OFFSET] = static_cast<float>(game.round_) / ROUND_LIMIT;
    out[GLOBAL_OFFSET + 1] =
        static_cast<float>(std::max(ROUND_LIMIT - game.round_ + 1, 0)) /
        ROUND_LIMIT;
    // Right aligned, oldest to newest. Padding is identified by the first
    // channel.
    for (int i = 0; i < game.history_size_; ++i) {
        const auto &record = game.history_[static_cast<size_t>(i)];
        const int offset =
            HISTORY_OFFSET +
            (HISTORY_LENGTH - game.history_size_ + i) * HISTORY_TOKEN_LENGTH;
        out[offset] = 1.0F;
        out[offset + 1] =
            static_cast<float>(record.actions_[static_cast<size_t>(seat)]);
        out[offset + 2] =
            static_cast<float>(record.actions_[static_cast<size_t>(1 - seat)]);
        out[offset + 3] = static_cast<float>(record.round_) / ROUND_LIMIT;
        for (int view = 0; view < 2; ++view) {
            const auto &values =
                record
                    .players_[static_cast<size_t>(view == 0 ? seat : 1 - seat)];
            std::ranges::copy(values, out.begin() + offset + 4 +
                                          view * HISTORY_PLAYER_LENGTH);
        }
    }
    return out;
}
} // namespace blacksmith::python_bindings::master
