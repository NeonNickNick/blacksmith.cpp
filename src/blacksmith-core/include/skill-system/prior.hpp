#pragma once
#define PRIOR_COMBO(skill, ...)                                                \
    static bool skill_combo_collected = []() {                                 \
        auto &combos = get_skill_combo();                                      \
        combos[static_cast<size_t>(skill)] = __VA_ARGS__;                      \
        return true;                                                           \
    }();