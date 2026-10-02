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

#define BEGIN_PROFESSION                                                       \
    using namespace blacksmith_core::domain;                                   \
    namespace blacksmith_core::skill_system {                                  \
    static std::vector<                                                        \
        std::tuple<skill, int, skill_check_func, skill_declare_func>>          \
        skills{};                                                              \
    static std::vector<std::pair<std::string, skill>>                          \
        partial_string_skill_mapping{};                                        \
    static skill_check_func passive_check_func = nullptr;                      \
    static skill_declare_func passive_declare_func = nullptr;

#define END(profession_name)                                                   \
    static profession_skill_set profession_name##_skill_set{                   \
        skills, passive_check_func, passive_declare_func};                     \
    static bool mapping_merged = []() {                                        \
        auto &s_s_mapping = get_string_skill_mapping();                        \
        auto &s_p_mapping = get_skill_profession_mapping();                    \
        for (const auto &p : partial_string_skill_mapping) {                   \
            s_p_mapping[static_cast<size_t>(p.second)] =                       \
                &profession_name##_skill_set;                                  \
        }                                                                      \
        for (auto &&p : partial_string_skill_mapping) {                        \
            s_s_mapping.emplace_back(std::move(p));                            \
        }                                                                      \
        return true;                                                           \
    }();                                                                       \
                                                                               \
    const blacksmith_core::domain::profession_skill_set *                      \
    get_##profession_name() {                                                  \
        return &profession_name##_skill_set;                                   \
    }                                                                          \
    }

#define REQUIRE(...) [](const community &player) { return __VA_ARGS__; }(p);
#define REQUIRE_(...) []() { return __VA_ARGS__; }();
#define DSL(...) [](community &player) { __VA_ARGS__; }(p);
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

#define REGISTER(name, skill_enum, check, declare_use, declare_write)          \
    CHECK(name) {                                                              \
        auto &p = *context.self_;                                              \
        return check                                                           \
    }                                                                          \
    DECLARE(name) {                                                            \
        auto &p = *context.self_;                                              \
        declare_use declare_write                                              \
    }                                                                          \
    static bool name##_registed = []() {                                       \
        skills.emplace_back(skill_enum, 0, CHECK_NAME(name),                   \
                            DECLARE_NAME(name));                               \
        partial_string_skill_mapping.emplace_back(#name, skill_enum);          \
        return true;                                                           \
    }();
#define REGISTER_(name, skill_enum, check, declare_use, declare_write)         \
    CHECK_(name){return check} DECLARE(name) {                                 \
        auto &p = *context.self_;                                              \
        declare_use declare_write                                              \
    }                                                                          \
    static bool name##_registed = []() {                                       \
        skills.emplace_back(skill_enum, 0, CHECK_NAME(name),                   \
                            DECLARE_NAME(name));                               \
        partial_string_skill_mapping.emplace_back(#name, skill_enum);          \
        return true;                                                           \
    }();

inline constexpr int BATCH_SIZE = 5;

// NOLINTBEGIN
#define REGISTER_BATCH(name, skill_enum, check, declare_use, declare_write)    \
    TMP_CHECK(name, N) {                                                       \
        auto &p = *context.self_;                                              \
        return check                                                           \
    }                                                                          \
    TMP_DECLARE(name, N) {                                                     \
        auto &p = *context.self_;                                              \
        declare_use declare_write                                              \
    }                                                                          \
    template <int N> bool name##_regist() {                                    \
        if constexpr (N == 0) {                                                \
            skills.emplace_back(skill_enum, 0, TMP_CHECK_NAME(name, 0),        \
                                TMP_DECLARE_NAME(name, 0));                    \
            partial_string_skill_mapping.emplace_back(#name, skill_enum);      \
            return true;                                                       \
        } else {                                                               \
            name##_regist<N - 1>();                                            \
            skills.emplace_back(skill_enum, N, TMP_CHECK_NAME(name, N),        \
                                TMP_DECLARE_NAME(name, N));                    \
            return true;                                                       \
        }                                                                      \
    }                                                                          \
    static bool name##_registed = name##_regist<BATCH_SIZE>();
// NOLINTEND

// NOLINTBEGIN
#define REPEAT_BATCH(name, dsl_func)                                           \
    template <int N, int index, int repeat_times>                              \
    void name##_impl(community &player) {                                      \
        if constexpr (index >= repeat_times) {                                 \
            return;                                                            \
        } else {                                                               \
            auto &p = player;                                                  \
            dsl_func;                                                          \
            name##_impl<N, index + 1, repeat_times>(player);                   \
        }                                                                      \
    }                                                                          \
    template <int N, int repeat_times> void name(community &player) {          \
        name##_impl<N, 0, repeat_times>(player);                               \
    }
// NOLINTEND

#define NOTHING true
#define RESOURCE(type, need) player.focus_.resource_.check(type, need)
#define RESOURCE_COMMON_ONLY(type, need)                                       \
    player.focus_.resource_.check(type, need, true)
#define HP(need) (player.focus_.health_.hp_ > (need))
#define MHP(need) (player.focus_.health_.mhp_ > (need))
#define R_HP resource_type::HP
#define R_MHP resource_type::MHP
#define R_IRON resource_type::IRON
#define R_GOLD_IRON resource_type::GOLD_IRON
#define R_SPACE resource_type::SPACE
#define R_TIME resource_type::TIME
#define R_MAGIC resource_type::MAGIC

#define PHYSICAL attack_type::PHYSICAL
#define MAGICAL attack_type::MAGICAL
#define REAL attack_type::REAL
