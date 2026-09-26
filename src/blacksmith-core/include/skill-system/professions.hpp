#pragma once
#include "domain/models.hpp"
#define DEF_SKILL_SET(name) const profession_skill_set *get_##name();
#define SKILL_SET(name) get_##name()
using namespace blacksmith_core::domain;
using profession_func = const profession_skill_set *();
namespace blacksmith_core::skill_system {
DEF_SKILL_SET(common)
DEF_SKILL_SET(warlock)
DEF_SKILL_SET(cannon)
DEF_SKILL_SET(driver)
DEF_SKILL_SET(lancer)

DEF_SKILL_SET(alchemy)
} // namespace blacksmith_core::skill_system