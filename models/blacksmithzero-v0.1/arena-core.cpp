#include "game.hpp"
#include <omp.h>

namespace {
std::vector<zero::State> games;
}
extern "C" {
int zs_init(int count, int threads) {
    if (count <= 0 || threads <= 0) return -1;
    omp_set_num_threads(threads);
    games.clear();
    games.resize(count);
    return zero::feature_count;
}
int zs_actions() { return zero::action_count; }
void zs_observe(float* observations, float* masks) {
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < static_cast<int>(games.size()); ++i)
        zero::write_observation(games[i], observations + i * 2 * zero::feature_count, masks + i * 2 * zero::action_count);
}
// Expand every joint action in each sampled root matrix. Padding is never stepped.
int zs_expand(int width, const int* candidates, float* observations, float* outcomes) {
    int invalid = 0;
    #pragma omp parallel for schedule(dynamic) reduction(+:invalid)
    for (int cell = 0; cell < static_cast<int>(games.size()) * width * width; ++cell) {
        int game = cell / (width * width);
        int row = cell / width % width, column = cell % width;
        int a = candidates[(game * 2) * width + row];
        int b = candidates[(game * 2 + 1) * width + column];
        float* out = observations + cell * 2 * zero::feature_count;
        if (a < 0 || b < 0) {
            outcomes[cell] = -1;
            std::fill(out, out + 2 * zero::feature_count, 0);
            continue;
        }
        auto next = games[game];
        if (!next.legal(0, a) || !next.legal(1, b)) { ++invalid; continue; }
        next.step(a, b);
        outcomes[cell] = static_cast<float>(next.outcome());
        zero::write_observation(next, out);
    }
    return invalid;
}
int zs_step(const int* actions, float* observations, float* masks, float* results) {
    int invalid = 0;
    #pragma omp parallel for schedule(dynamic) reduction(+:invalid)
    for (int i = 0; i < static_cast<int>(games.size()); ++i) {
        auto& state = games[i];
        if (!state.legal(0, actions[2 * i]) || !state.legal(1, actions[2 * i + 1])) { ++invalid; continue; }
        state.step(actions[2 * i], actions[2 * i + 1]);
        const int outcome = state.outcome();
        results[4 * i] = static_cast<float>(outcome);
        results[4 * i + 1] = static_cast<float>(state.round - 1);
        results[4 * i + 2] = static_cast<float>(state.players[0].focus_.health_.hp_);
        results[4 * i + 3] = static_cast<float>(state.players[1].focus_.health_.hp_);
        if (outcome) state = zero::State{};
        zero::write_observation(state, observations + i * 2 * zero::feature_count, masks + i * 2 * zero::action_count);
    }
    return invalid;
}
}
