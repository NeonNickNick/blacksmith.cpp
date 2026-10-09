#pragma once
// General rule-only tree expansion. No opponent model or skill-specific heuristic.
namespace {
std::vector<zero::State> tree_parents;
void write_tree_observation(zero::State& state, float* out, float* masks) {
    std::vector<float> first, second;
    first.reserve(358);
    second.reserve(358);
    zero::append_player(first, state.players[0], state.previous[0]);
    zero::append_player(second, state.players[1], state.previous[1]);
    if (first.size() != 358 || second.size() != 358)
        throw std::runtime_error("Tree observation schema mismatch");
    for (int seat = 0; seat < 2; ++seat) {
        float* view = out + seat * zero::feature_count;
        view[0] = state.round / 20.0F;
        view[1] = seat == 0 ? 1.0F : 0.0F;
        const auto& own = seat == 0 ? first : second;
        const auto& other = seat == 0 ? second : first;
        std::copy(own.begin(), own.end(), view + 2);
        std::copy(other.begin(), other.end(), view + 360);
        if (masks) for (int action = 0; action < zero::action_count; ++action)
            masks[seat * zero::action_count + action] = state.legal(seat, action) ? 1.0F : 0.0F;
    }
}
}
extern "C" {
int zt_begin() {
    tree_parents = games;
    return static_cast<int>(tree_parents.size());
}
int zt_expand(int width, const int* candidates, int retain,
              float* observations, float* masks, float* outcomes) {
    const int cells = static_cast<int>(tree_parents.size()) * width * width;
    std::vector<zero::State> children;
    if (retain) children.resize(cells);
    int invalid = 0;
    #pragma omp parallel for schedule(dynamic) reduction(+:invalid)
    for (int cell = 0; cell < cells; ++cell) {
        int parent = cell / (width * width);
        int row = cell / width % width, column = cell % width;
        int first = candidates[parent * 2 * width + row];
        int second = candidates[(parent * 2 + 1) * width + column];
        float* observation = observations + cell * 2 * zero::feature_count;
        float* mask = masks + cell * 2 * zero::action_count;
        std::fill(observation, observation + 2 * zero::feature_count, 0);
        std::fill(mask, mask + 2 * zero::action_count, 0);
        outcomes[cell] = -1;
        if (first < 0 || second < 0) continue;
        auto child = tree_parents[parent];
        if (child.outcome() || !child.legal(0, first) || !child.legal(1, second)) {
            ++invalid;
            continue;
        }
        child.step(first, second);
        outcomes[cell] = static_cast<float>(child.outcome());
        // Final leaves need values only, so no legal-action mask is computed.
        if (!child.outcome()) write_tree_observation(child, observation, retain ? mask : nullptr);
        if (retain) children[cell] = std::move(child);
    }
    if (retain) tree_parents = std::move(children);
    return invalid;
}
}
