#include <domain/models.hpp>
#include <domain/transformations.hpp>
#include <skill-system/dsl-macro.hpp>
#include <skill-system/dsl.hpp>

using namespace blacksmith_core::domain;
namespace blacksmith_core::skill_system {
BEGIN_PROFESSION

REGIST_C(iron, LOGIC_(TRUE),
         LOGIC(write_resource<1.0F, resource_type::IRON>(SELF);))

REGIST(space, LOGIC(RESOURCE(resource_type::IRON, 3.0F)),
       LOGIC(write_resource<1.0F, resource_type::SPACE>(SELF);))

REGIST(time, LOGIC(RESOURCE(resource_type::IRON, 3.0F)),
       LOGIC(write_resource<1.0F, resource_type::TIME>(SELF);))

REGIST(stick, LOGIC(RESOURCE(resource_type::IRON, 0.5F);),
       LOGIC(write_attack<1, attack_type::PHYSICAL>(SELF);));

REGIST(drill, LOGIC(RESOURCE(resource_type::IRON, 1.5F);),
       LOGIC(write_attack<3, attack_type::PHYSICAL>(SELF);));

REGIST(slash, LOGIC(RESOURCE(resource_type::IRON, 2.5F);),
       LOGIC(write_attack<5, attack_type::PHYSICAL>(SELF);));

REGIST_BATCH(shield, LOGIC(RESOURCE(resource_type::IRON, 0.0F + (0.5F * N));),
             LOGIC(write_defense<{defense_type::COMMON_REDUCTION,
                                  2 + N,
                                  {},
                                  common_reduction,
                                  default_update}>(SELF);),
             N)

END(COMMON)

blacksmith_core::domain::profession_skill_set *common_skill_set() {
    return &COMMON_SKILL_SET;
}
} // namespace blacksmith_core::skill_system