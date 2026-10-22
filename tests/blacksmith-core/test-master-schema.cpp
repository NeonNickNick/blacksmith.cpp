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

static_assert(static_cast<int>(profession_type::CROSSBOW) == 8);
static_assert(static_cast<int>(profession_type::SIZE) == 9);
static_assert(SCHEMA_VERSION == 8);
static_assert(COMMON_ENCODING_LENGTH == 266);
static_assert(PROFESSION_ENCODING_LENGTH == 382);
static_assert(OBSERVATION_LENGTH == 1458);
static_assert(SKILL_NUM == 266);

namespace {
void expect(float actual, float expected) {
    if (std::abs(actual - expected) > 1e-6F)
        throw std::runtime_error("Master observation schema mismatch");
}

void expect_authorized(state &game, skill move, bool expected) {
    std::array<float, COMMON_ENCODING_LENGTH> common{};
    encode_common(game.players_[0], common);
    expect(common[AUTHORIZED_OFFSET + static_cast<size_t>(move)],
           expected ? 1.0F : 0.0F);
    if (authorized(game.players_[0], move) != expected ||
        game.legal(0, action_index(move)) != expected)
        throw std::runtime_error("Master skill mask ignored a disable layer");
}

void test_disable_count_encoding() {
    state permissions;
    auto &skills = permissions.players_[0].focus_.skills_;
    expect_authorized(permissions, skill::IRON, true);
    // Enabling an unlocked skill must not underflow.
    skills.enable_skill(skill::IRON);
    skills.disable_skill(skill::IRON);
    skills.disable_skill(skill::IRON);
    expect_authorized(permissions, skill::IRON, false);

    auto branch = permissions;
    auto &branch_skills = branch.players_[0].focus_.skills_;
    branch_skills.enable_skill(skill::IRON);
    expect_authorized(branch, skill::IRON, false);
    branch_skills.enable_skill(skill::IRON);
    expect_authorized(branch, skill::IRON, true);
    expect_authorized(permissions, skill::IRON, false);

    skills.enable_skill(skill::IRON);
    expect_authorized(permissions, skill::IRON, false);
    skills.enable_skill(skill::IRON);
    expect_authorized(permissions, skill::IRON, true);
    branch_skills.disable_skill(skill::IRON);
    expect_authorized(branch, skill::IRON, false);
    expect_authorized(permissions, skill::IRON, true);
}
} // namespace

int main() {
    test_disable_count_encoding();
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
         .clock_ = {.remaining_rounds_ = 3},
         .defender_ = nullptr},
        player);
    write_mark<mark_id::CHARGE, {.delayed_rounds_ = 2, .is_infinite_ = true}>(
        player);
    write_mark<mark_id::CRYSTAL_WALL_HIT, {}>(player);
    write_mark<mark_id::DELAYED_CRYSTAL_WALL_HIT, {.delayed_rounds_ = 1}>(
        player);
    player.focus_.turn_context_.resource_context_.datas_.push_back(
        {.clock_ = {.delayed_rounds_ = 2},
         .type_ = resource_type::MHP,
         .power_ = 4,
         .executor_ = nullptr});
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
    const int wall_hit_mark =
        2 * COMMON_ENCODING_LENGTH + MARK_OFFSET +
        static_cast<int>(mark_id::CRYSTAL_WALL_HIT) * DELAY_BINS * CLOCK_LENGTH;
    expect(observation[wall_hit_mark], .5F);
    const int delayed_wall_hit_mark =
        2 * COMMON_ENCODING_LENGTH + MARK_OFFSET +
        (static_cast<int>(mark_id::DELAYED_CRYSTAL_WALL_HIT) * DELAY_BINS + 1) *
            CLOCK_LENGTH;
    expect(observation[delayed_wall_hit_mark], .5F);
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
    professions.step(action_index(skill::CAULDRON), iron);
    expect(static_cast<float>(role(professions.players_[0])),
           static_cast<float>(profession_type::WARLOCK));
    if (!authorized(professions.players_[0], skill::MAGIC) ||
        !authorized(professions.players_[0], skill::MIDASTOUCH) ||
        !authorized(professions.players_[0], skill::FIRE) ||
        authorized(professions.players_[0], skill::ALCHEMY) ||
        authorized(professions.players_[0], skill::CAULDRON) ||
        authorized(professions.players_[1], skill::ALCHEMY))
        throw std::runtime_error("[equipment_schema] Cauldron changed "
                                 "profession or equipment permissions");
}
