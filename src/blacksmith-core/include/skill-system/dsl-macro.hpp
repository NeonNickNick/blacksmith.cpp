#pragma once
#define CHECK_NAME(name) name##_check
#define DECLARE_NAME(name) name##_declare
#define CHECK(name) static bool CHECK_NAME(name)(const skill_context &context)
#define DECLARE(name) static void DECLARE_NAME(name)(skill_context & context)
#define CHECK_(name)                                                           \
    static bool CHECK_NAME(name)(const skill_context & /*context*/)
#define DECLARE_(name)                                                         \
    static void DECLARE_NAME(name)(skill_context & /*context*/)

#define TMP_CHECK_NAME(name, N) CHECK_NAME(name)<N>
#define TMP_DECLARE_NAME(name, N) DECLARE_NAME(name)<N>
#define TMP_CHECK(name, N)                                                     \
    template <int N> bool CHECK_NAME(name)(const skill_context &context)
#define TMP_DECLARE(name, N)                                                   \
    template <int N> void DECLARE_NAME(name)(skill_context & context)

#define BEGIN_SKILL_SET                                                        \
    using namespace blacksmith::domain;                                        \
    namespace blacksmith::skill_system {                                       \
    static std::vector<skill> skills{};                                        \
    static skill_check_func passive_check_func = nullptr;                      \
    static skill_declare_func passive_declare_func = nullptr;

#define END(skill_set_name)                                                    \
    static skill_set skill_set_name##_skill_set{skills, passive_check_func,    \
                                                passive_declare_func};         \
                                                                               \
    const blacksmith::domain::skill_set *get_##skill_set_name() {              \
        return &skill_set_name##_skill_set;                                    \
    }                                                                          \
    }

#define REQUIRE(...) [](const community &player) { return __VA_ARGS__; }(p);
#define REQUIRE_CHAIN(...)                                                     \
    [&next](const community &player) { return __VA_ARGS__; }(p);
#define REQUIRE_BATCH(...)                                                     \
    [N](const community &player) { return __VA_ARGS__; }(p);
#define REQUIRE_(...) []() { return __VA_ARGS__; }();
#define DSL(...) [](community &player) { __VA_ARGS__; }(p);
#define DSL_BATCH(...) [N](community &player) { __VA_ARGS__; }(p);
#define DSL_CHAIN(...) [&next](community &player) { __VA_ARGS__; }(p);
#define DSL_CAPTURE(...) [&]() { __VA_ARGS__; }();
#define DSL_(...) []() { __VA_ARGS__; }();

#define PASSIVE(check, declare)                                                \
    CHECK(passive) {                                                           \
        auto &p = *context.self_;                                              \
        return check                                                           \
    }                                                                          \
    DECLARE(passive) {                                                         \
        auto &p = *context.self_;                                              \
        declare                                                                \
    }                                                                          \
    static bool passive_check_registed = []() {                                \
        passive_check_func = &CHECK_NAME(passive);                             \
        return true;                                                           \
    }();                                                                       \
    static bool passive_declare_registed = []() {                              \
        passive_declare_func = &DECLARE_NAME(passive);                         \
        return true;                                                           \
    }();
#define PASSIVE_(check, declare)                                               \
    CHECK_(passive){return check} DECLARE(passive) {                           \
        auto &p = *context.self_;                                              \
        declare                                                                \
    }                                                                          \
    static bool passive_check_registed = []() {                                \
        passive_check_func = &CHECK_NAME(passive);                             \
        return true;                                                           \
    }();                                                                       \
    static bool passive_declare_registed = []() {                              \
        passive_declare_func = &DECLARE_NAME(passive);                         \
        return true;                                                           \
    }();

#define REGISTER_SKILL(name, skill_enum)                                       \
    static bool name##_registed = []() {                                       \
        skills.push_back(skill_enum);                                          \
        get_string_skill_mapping().emplace_back(#name, skill_enum);            \
        get_skill_check_mapping()[static_cast<size_t>(skill_enum)] =           \
            &CHECK_NAME(name);                                                 \
        get_skill_declare_mapping()[static_cast<size_t>(skill_enum)] =         \
            &DECLARE_NAME(name);                                               \
        get_none_cost_skill_declare_mapping()[static_cast<size_t>(             \
            skill_enum)] = &DECLARE_NAME(name##_none_cost);                    \
        return true;                                                           \
    }();

#define REGISTER(name, skill_enum, check, declare_use, declare_write)          \
    CHECK(name) {                                                              \
        auto &p = *context.self_;                                              \
        return check                                                           \
    }                                                                          \
    DECLARE(name) {                                                            \
        auto &p = *context.self_;                                              \
        declare_use declare_write                                              \
    }                                                                          \
    DECLARE(name##_none_cost) {                                                \
        auto &p = *context.self_;                                              \
        declare_write                                                          \
    }                                                                          \
    REGISTER_SKILL(name, skill_enum)
#define REGISTER_CHAIN(name, skill_enum, check, declare_use, declare_write)    \
    CHECK(name) {                                                              \
        auto &p = *context.self_;                                              \
        const auto &next = context.action_.next_;                              \
        return check                                                           \
    }                                                                          \
    DECLARE(name) {                                                            \
        auto &p = *context.self_;                                              \
        const auto &next = context.action_.next_;                              \
        declare_use declare_write                                              \
    }                                                                          \
    DECLARE(name##_none_cost) {                                                \
        auto &p = *context.self_;                                              \
        const auto &next = context.action_.next_;                              \
        declare_write                                                          \
    }                                                                          \
    REGISTER_SKILL(name, skill_enum)
#define REGISTER_BATCH(name, skill_enum, check, declare_use, declare_write)    \
    CHECK(name) {                                                              \
        auto &p = *context.self_;                                              \
        const auto N = context.action_.param_;                                 \
        return check                                                           \
    }                                                                          \
    DECLARE(name) {                                                            \
        auto &p = *context.self_;                                              \
        const auto N = context.action_.param_;                                 \
        declare_use declare_write                                              \
    }                                                                          \
    DECLARE(name##_none_cost) {                                                \
        auto &p = *context.self_;                                              \
        const auto N = context.action_.param_;                                 \
        declare_write                                                          \
    }                                                                          \
    REGISTER_SKILL(name, skill_enum)
#define REGISTER_(name, skill_enum, check, declare_use, declare_write)         \
    CHECK_(name){return check} DECLARE(name) {                                 \
        auto &p = *context.self_;                                              \
        declare_use declare_write                                              \
    }                                                                          \
    DECLARE(name##_none_cost) {                                                \
        auto &p = *context.self_;                                              \
        declare_write                                                          \
    }                                                                          \
    REGISTER_SKILL(name, skill_enum)
#define REPEAT(name, dsl_func)                                                 \
    template <int index, int len> void name##_impl(community &player) {        \
        if constexpr (index >= len) {                                          \
            return;                                                            \
        } else {                                                               \
            auto &p = player;                                                  \
            dsl_func;                                                          \
            name##_impl<index + 1, len>(player);                               \
        }                                                                      \
    }                                                                          \
    template <int len> void name(community &player) {                          \
        name##_impl<0, len>(player);                                           \
    }
#define REPEAT_BATCH(name, dsl_func)                                           \
    template <int index, int len> void name##_impl(int N, community &player) { \
        if constexpr (index >= len) {                                          \
            return;                                                            \
        } else {                                                               \
            auto &p = player;                                                  \
            dsl_func;                                                          \
            name##_impl<index + 1, len>(N, player);                            \
        }                                                                      \
    }                                                                          \
    template <int len> void name(int N, community &player) {                   \
        name##_impl<0, len>(N, player);                                        \
    }
#define NOTHING true
#define RESOURCE(type, need) player.focus_.resource_.check(type, need)
#define RESOURCE_COMMON_ONLY(type, need)                                       \
    player.focus_.resource_.check(type, need, true)
#define HP(need) (player.focus_.health_.hp_ >= (need))
#define MHP(need) (player.focus_.health_.mhp_ >= (need))
#define R_HP resource_type::HP
#define R_MHP resource_type::MHP
#define R_IRON resource_type::IRON
#define R_GOLD_IRON resource_type::GOLD_IRON
#define R_SPACE resource_type::SPACE
#define R_TIME resource_type::TIME
#define R_MAGIC resource_type::MAGIC
#define R_CROSS resource_type::CROSS
#define R_DREAM resource_type::DREAM
#define R_SPIRIT resource_type::SPIRIT
#define R_FIRE resource_type::FIRE
#define R_WATER resource_type::WATER
#define R_WOOD resource_type::WOOD
#define R_EARTH resource_type::EARTH
#define R_BOLT resource_type::BOLT
#define R_JADE resource_type::JADE
#define R_CRYSTAL resource_type::CRYSTAL
#define R_CRYSTAL_BALL resource_type::CRYSTAL_BALL

#define PHYSICAL attack_type::PHYSICAL
#define MAGICAL attack_type::MAGICAL
#define REAL attack_type::REAL
