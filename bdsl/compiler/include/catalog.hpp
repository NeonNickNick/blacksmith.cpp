#pragma once
#include <map>
#include <set>
#include <string>

namespace bdsl {
// Snapshot of the adjacent core's public enum names. SIZE is never a usable value.
inline const std::set<std::string> skills = {
    "IRON", "STICK", "DRILL", "SLASH", "TEAR", "SHIELD", "THORN_SHIELD",
    "RECOVERY", "SPACE", "TIME", "REFLECT", "DELAY_PROTECTION", "ARMOR12",
    "WARLOCK", "ALCHEMY", "CANNON", "DRIVER", "LANCER", "BLOODSIGIL",
    "MAGIC", "MAGIC_ATTACK", "MAGIC_SHIELD", "SACRIFICE", "MUTE", "MIDASTOUCH",
    "STRIKE", "DOUBLE_STRIKE", "TRIPLE_STRIKE", "APSHELL", "CANNON_BARRIER",
    "SPACE_ATTACK", "TIME2SPACE", "SPACE2TIME", "SPACE_BARRIER", "SKY_STRIKE",
    "TYRANT_DESTURCTION", "DRAGON_TOOTH", "TRIPLE_STAB", "CHARGE", "RISING_DRAGON",
    "BLOOD_BLADE", "BLOOD_LUST", "BLOOD_RECOVERY", "BLOOD_RAGE", "MONK", "PROPHET",
    "HOLYBOOK", "PHANTOMBOOK", "CAULDRON", "CROSSBOW", "JADE", "GHOST_STEP",
    "GOLDEN_BELL_COVER", "MAZE_FIST", "MIST", "DISILLUSIONMENT", "CRYSTAL",
    "CRYSTAL_BALL", "FORETOLD", "GREATEST_CAUTION", "REVELATION", "ASSERTION",
    "CRYSTAL_WALL", "REFRACTION", "ULTIMATUM", "CROSS", "PRAYER", "ARK",
    "PROFANATION", "REBIRTH", "ABSOLUTION", "FANTASIA", "ASSOCIATION",
    "HALLUCINATION", "DREAM_WAKE", "ILLUSION", "NIGHTMARE", "DREAM_DIVE",
    "MATERIALIZATION", "HAUNT", "CHANNELING", "FIRE", "WATER", "WOOD", "EARTH",
    "EXPLOSION", "ICE_BLADE", "REGENERATION", "STONE_SHELL", "BURN_LIFE",
    "FIRE_RAIN", "ELEMENTAL_ARMOR", "HAMMER", "GUARD", "MAKE_BOLTS", "ARROW_RAIN",
    "AIM", "CRITICAL", "BLOCK", "MARK_SHOT"
};
inline const std::set<std::string> marks = {
    "CANNON", "DRIVER", "SKY_STRIKE", "TYRANT_DESTRUCTION", "DRAGON_TOOTH",
    "TRIPLE_STAB", "CHARGE", "COUNTER_ATTACK", "BLOODSIGIL", "PROFANATION",
    "CROSSBOW_AIM", "GREATEST_CAUTION", "REVELATION", "CRYSTAL_WALL_HIT",
    "DELAYED_CRYSTAL_WALL_HIT"
};
inline const std::set<std::string> skill_sets = {
    "common", "warlock", "cannon", "driver", "lancer", "bloodsigil", "monk",
    "prophet", "holybook", "phantombook", "crossbow", "cauldron", "alchemy",
    "nightmare", "elementalarmor"
};
inline const std::map<std::string, std::string> resources = {
    {"HP", "HP"}, {"MHP", "MHP"}, {"Iron", "IRON"}, {"CommonIron", "IRON"},
    {"GoldIron", "GOLD_IRON"}, {"Space", "SPACE"}, {"Time", "TIME"},
    {"Magic", "MAGIC"}, {"Cross", "CROSS"}, {"Dream", "DREAM"},
    {"Spirit", "SPIRIT"}, {"Fire", "FIRE"}, {"Water", "WATER"},
    {"Wood", "WOOD"}, {"Earth", "EARTH"}, {"Bolt", "BOLT"}, {"Jade", "JADE"},
    {"Crystal", "CRYSTAL"}, {"CrystalBall", "CRYSTAL_BALL"}
};
inline const std::map<std::string, std::string> attacks = {
    {"Physical", "PHYSICAL"}, {"Magical", "MAGICAL"}, {"Real", "REAL"}
};
// Type -> enum, defender, update. Special defenses requiring custom game hooks are excluded.
struct Defense { std::string id, defender, update; };
inline const std::map<std::string, Defense> defenses = {
    {"CommonReduction", {"COMMON_REDUCTION", "default_reduction", "default_update"}},
    {"RealReduction", {"REAL_REDUCTION", "default_reduction", "default_update"}},
    {"ThornReduction", {"THORN_REDUCTION", "thorn_reduction", "default_update"}},
    {"PercentageReduction", {"PERCENTAGE_REDUCTION", "percentage_reduction", "default_update"}},
    {"CommonArmor", {"COMMON_ARMOR", "default_armor", "armor_update"}},
    {"RealArmor", {"REAL_ARMOR", "default_armor", "armor_update"}},
    {"PhysicalImmunity", {"PHYSICAL_IMMUNITY", "physical_immunity", "default_update"}},
    {"MagicalImmunity", {"MAGICAL_IMMUNITY", "magical_immunity", "default_update"}}
};
} // namespace bdsl
