#pragma once
#include "domain/models.hpp"
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#define DEF_SKILL_SET(name) const skill_set *get_##name();
#define SKILL_SET(name) get_##name()
using namespace blacksmith::domain;
using skill_set_func = const skill_set *();
namespace blacksmith::skill_system {
// Text input and display share the registered skill names.
[[nodiscard]] skill_action parse_skill_action(std::string_view input);
[[nodiscard]] std::string format_skill_action(const skill_action &action);

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
inline std::array<skill_declare_func, static_cast<size_t>(skill::SIZE)> &
get_none_cost_skill_declare_mapping() {
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

// Mutually exclusive professions at the root node.
DEF_SKILL_SET(warlock)
DEF_SKILL_SET(cannon)
DEF_SKILL_SET(driver)
DEF_SKILL_SET(lancer)
DEF_SKILL_SET(bloodsigil)
// DEF_SKILL_SET(monk)
// DEF_SKILL_SET(prophet)
DEF_SKILL_SET(holybook)
DEF_SKILL_SET(phantombook)
DEF_SKILL_SET(crossbow)

// Secondary equipment: Cauldron belongs to common, Alchemy to Warlock.
DEF_SKILL_SET(cauldron)
DEF_SKILL_SET(alchemy)

// Temporary equipment modes.
DEF_SKILL_SET(nightmare)
DEF_SKILL_SET(elementalarmor)
} // namespace blacksmith::skill_system
