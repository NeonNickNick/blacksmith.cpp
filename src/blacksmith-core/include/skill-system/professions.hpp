#pragma once
#include "domain/models.hpp"
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#define DEF_SKILL_SET(name) const profession_skill_set *get_##name();
#define SKILL_SET(name) get_##name()
using namespace blacksmith::domain;
using profession_func = const profession_skill_set *();
namespace blacksmith::skill_system {
inline std::vector<std::pair<std::string, skill>> &get_string_skill_mapping() {
    static std::vector<std::pair<std::string, skill>> mapping{};
    return mapping;
}

inline std::array<skill_check_func, static_cast<size_t>(skill::SIZE)> &
get_skill_check_mapping() {
    static std::array<skill_check_func, static_cast<size_t>(skill::SIZE)>
        mapping{};
    return mapping;
}
inline std::array<skill_declare_func, static_cast<size_t>(skill::SIZE)> &
get_skill_declare_mapping() {
    static std::array<skill_declare_func, static_cast<size_t>(skill::SIZE)>
        mapping{};
    return mapping;
}
inline std::array<std::vector<skill>, static_cast<size_t>(skill::SIZE)> &
get_skill_combo() {
    static std::array<std::vector<skill>, static_cast<size_t>(skill::SIZE)>
        combos;
    return combos;
}
DEF_SKILL_SET(common)
DEF_SKILL_SET(warlock)
DEF_SKILL_SET(cannon)
DEF_SKILL_SET(driver)
DEF_SKILL_SET(lancer)

DEF_SKILL_SET(alchemy)
} // namespace blacksmith::skill_system