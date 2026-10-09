// Training-only reset API. The rules, observations, search, and legal moves are unchanged.
#include "arena-core.cpp"
#include "tree.hpp"

namespace {
int profession(const zero::community& player) {
    const auto& skills = player.focus_.profession_.authorized_skills();
    auto has = [&](zero::skill s) {
        return std::find(skills.begin(), skills.end(), s) != skills.end();
    };
    if (has(zero::skill::MAGIC_ATTACK)) return has(zero::skill::MIDASTOUCH) ? 2 : 1;
    if (has(zero::skill::STRIKE)) return 3;
    if (has(zero::skill::SPACE_ATTACK)) return 4;
    if (has(zero::skill::SKY_STRIKE)) return 5;
    return 0;
}
}

extern "C" {
// Return action indices rather than depending on hardcoded enum numbers in Python.
int zp_action(int kind) {
    const std::array<zero::skill, 8> types = {
        zero::skill::IRON, zero::skill::WARLOCK, zero::skill::ALCHEMY,
        zero::skill::CANNON, zero::skill::DRIVER, zero::skill::LANCER,
        zero::skill::BLOODSIGIL, zero::skill::STICK
    };
    if (kind < 0 || kind >= static_cast<int>(types.size())) return -1;
    for (int i = 0; i < zero::action_count; ++i)
        if (zero::actions[i].move == types[kind] && zero::actions[i].parameter == 0) return i;
    return -1;
}

// Replay a legal prefix from a normal initial state. Commit only a live valid state.
int zp_reset_prefix(int env, int length, const int* moves) {
    if (env < 0 || env >= static_cast<int>(games.size()) || length < 0 || length >= 100) return -1;
    zero::State candidate;
    for (int i = 0; i < length; ++i) {
        if (candidate.outcome() || !candidate.legal(0, moves[2*i]) ||
            !candidate.legal(1, moves[2*i+1])) return -2;
        candidate.step(moves[2*i], moves[2*i+1]);
    }
    if (candidate.outcome()) return -3;
    games[env] = std::move(candidate);
    return 0;
}

void zp_metadata(int* info) {
    for (int i = 0; i < static_cast<int>(games.size()); ++i) {
        info[3*i] = games[i].round;
        info[3*i+1] = profession(games[i].players[0]);
        info[3*i+2] = profession(games[i].players[1]);
    }
}
}
