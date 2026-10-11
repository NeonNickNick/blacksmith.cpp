#include "actions.hpp"
#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include "encoding.hpp"
#include "state.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

using namespace blacksmith::domain;
using namespace blacksmith::python_bindings::master;

namespace {
void expect(float actual, float expected) {
    if (std::abs(actual - expected) > 1e-6F)
        throw std::runtime_error("Master observation schema mismatch");
}
} // namespace

int main() {
    state game;
    auto &player = game.players_[0];
    auto &enemy = game.players_[1];
    player.focus_.health_.hp_ = 7;
    enemy.focus_.health_.hp_ = 9;
    player.focus_.resource_.gain(resource_type::MAGIC, 2.0F);
    enemy.focus_.resource_.gain(resource_type::GOLD_IRON, 3.0F);
    write_attack<6, attack_type::MAGICAL,
                 {.delayed_rounds_ = 1, .remaining_rounds_ = 3}>(player);
    write_defense<{.delayed_rounds_ = 1}>(
        {.type_ = defense_type::REAL_REDUCTION,
         .power_ = 9,
         .clock_ = {.remaining_rounds_ = 3}},
        player);
    write_mark<mark_id::CHARGE, {.delayed_rounds_ = 2, .is_infinite_ = true}>(
        player);
    player.focus_.turn_context_.resource_context_.datas_.push_back(
        {.clock_ = {.delayed_rounds_ = 2},
         .type_ = resource_type::MHP,
         .power_ = 4});
    const auto observation = encode(game, 0);
    expect(observation[0], .7F);
    expect(observation[COMMON_ENCODING_LENGTH], .9F);
    expect(observation[2 * COMMON_ENCODING_LENGTH], .2F);
    expect(observation[2 * COMMON_ENCODING_LENGTH + PROFESSION_ENCODING_LENGTH +
                       1],
           .3F);
    const int attack =
        ATTACK_OFFSET +
        (static_cast<int>(attack_type::MAGICAL) * DELAY_BINS + 1) *
            ATTACK_UNIT_LENGTH;
    expect(observation[attack], .6F);
    expect(observation[attack + 2], .6F);
    const int defense =
        PENDING_DEFENSE_OFFSET +
        (static_cast<int>(defense_type::REAL_REDUCTION) * DELAY_BINS + 1) *
            DEFENSE_UNIT_LENGTH;
    expect(observation[defense], .9F);
    expect(observation[defense + 1], .6F);
    const int mark =
        2 * COMMON_ENCODING_LENGTH + MARK_OFFSET +
        (static_cast<int>(mark_id::CHARGE) * DELAY_BINS + 2) * CLOCK_LENGTH;
    expect(observation[mark], .5F);
    expect(observation[mark + 1], 0);
    expect(observation[mark + 2], 1);
    expect(observation[2 * COMMON_ENCODING_LENGTH + PENDING_RESOURCE_OFFSET +
                       (6 * DELAY_BINS + 2) * CLOCK_LENGTH],
           .4F);
    const auto reverse = encode(game, 1);
    for (int i = 0; i < COMMON_ENCODING_LENGTH; ++i)
        expect(observation[static_cast<size_t>(i)],
               reverse[static_cast<size_t>(COMMON_ENCODING_LENGTH + i)]);
    for (int i = 0; i < PROFESSION_ENCODING_LENGTH; ++i)
        expect(observation[static_cast<size_t>(2 * COMMON_ENCODING_LENGTH + i)],
               reverse[static_cast<size_t>(2 * COMMON_ENCODING_LENGTH +
                                           PROFESSION_ENCODING_LENGTH + i)]);

    state history;
    const int iron = action_index(skill::IRON);
    for (int turn = 0; turn < HISTORY_LENGTH + 3; ++turn)
        history.step(iron, iron);
    const auto past = encode(history, 0);
    expect(static_cast<float>(history.history_size_), HISTORY_LENGTH);
    expect(past[HISTORY_OFFSET + 3], .04F);
    expect(past[HISTORY_OFFSET + 7], .1F); // Ordinary iron delta.
    expect(
        past[HISTORY_OFFSET + (HISTORY_LENGTH - 1) * HISTORY_TOKEN_LENGTH + 3],
        .11F);
    auto clone = history;
    clone.step(iron, iron);
    expect(static_cast<float>(history.round_), HISTORY_LENGTH + 4);
    expect(encode(history, 0)[HISTORY_OFFSET + 3], .04F);
    expect(encode(clone, 0)[HISTORY_OFFSET + 3], .05F);

    state professions;
    for (int turn = 0; turn < 7; ++turn)
        professions.step(iron, iron);
    professions.step(action_index(skill::WARLOCK),
                     action_index(skill::BLOODSIGIL));
    expect(static_cast<float>(role(professions.players_[0])),
           static_cast<float>(profession_type::WARLOCK));
    expect(static_cast<float>(role(professions.players_[1])),
           static_cast<float>(profession_type::BLOODSIGIL));
    if (!authorized(professions.players_[1], skill::BLOOD_BLADE) ||
        authorized(professions.players_[1], skill::SKY_STRIKE))
        throw std::runtime_error(
            "Bloodsigil registration uses the wrong skill set");
    professions.step(action_index(skill::ALCHEMY),
                     action_index(skill::BLOOD_RECOVERY));
    expect(static_cast<float>(role(professions.players_[0])),
           static_cast<float>(profession_type::WARLOCK));
    if (!authorized(professions.players_[0], skill::MIDASTOUCH))
        throw std::runtime_error("Alchemy skill was not acquired");
}
