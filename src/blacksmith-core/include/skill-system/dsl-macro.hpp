#pragma once
#define CHECK_NAME(name) name##_check0
#define DECLARE_NAME(name) name##_declare0
#define CHECK(name) bool CHECK_NAME(name)(const skill_context &context)
#define DECLARE(name) void DECLARE_NAME(name)(skill_context & context)
#define CHECK_(name) bool CHECK_NAME(name)(const skill_context & /*context*/)
#define DECLARE_(name) void DECLARE_NAME(name)(skill_context & /*context*/)

#define TMP_CHECK_NAME(name, N) name##_check<N>
#define TMP_DECLARE_NAME(name, N) name##_declare<N>
#define TMP_CHECK(name, N)                                                     \
    template <int N> bool name##_check(const skill_context &context)
#define TMP_DECLARE(name, N)                                                   \
    template <int N> void name##_declare(skill_context &context)

#define BEGIN_PROFESSION                                                       \
    static std::vector<                                                        \
        std::tuple<std::string, skill_check_func, skill_declare_func>>         \
        skills{};
#define END(profession_name)                                                   \
    static profession_skill_set profession_name##_SKILL_SET{skills};

#define LOGIC(...) [&context]() { __VA_ARGS__ }();
#define LOGIC_(...) []() { __VA_ARGS__ }();

#define SELF context.self_
#define TRUE return true;
#define RESOURCE(type, need)                                                   \
    return context.self_.focus_.resource_.check(type, need);

#define REGIST(name, check, declare)                                           \
    CHECK(name){return check} DECLARE(name) { declare }                        \
    static bool name##_registed = []() {                                       \
        skills.emplace_back(#name "0", CHECK_NAME(name), DECLARE_NAME(name));  \
        return true;                                                           \
    }();
#define REGIST_C(name, check, declare)                                         \
    CHECK_(name){return check} DECLARE(name) { declare }                       \
    static bool name##_registed = []() {                                       \
        skills.emplace_back(#name "0", CHECK_NAME(name), DECLARE_NAME(name));  \
        return true;                                                           \
    }();
#define REGIST_D(name, check, declare)                                         \
    CHECK(name){return check} DECLARE_(name) { declare }                       \
    static bool name##_registed = []() {                                       \
        skills.emplace_back(#name "0", CHECK_NAME(name), DECLARE_NAME(name));  \
        return true;                                                           \
    }();
#define REGIST_CD(name, check, declare)                                        \
    CHECK_(name){return check} DECLARE_(name) { declare }                      \
    static bool name##_registed = []() {                                       \
        skills.emplace_back(#name "0", CHECK_NAME(name), DECLARE_NAME(name));  \
        return true;                                                           \
    }();

inline constexpr int BATCH_SIZE = 5;

// NOLINTBEGIN
#define REGIST_BATCH(name, check, declare, N)                                  \
    TMP_CHECK(name, N){return check} TMP_DECLARE(name, N) { declare }          \
    template <int N> bool name##_regist() {                                    \
        if constexpr (N < 0) {                                                 \
            return false;                                                      \
        } else {                                                               \
            name##_regist<N - 1>();                                            \
            skills.emplace_back(#name + std::to_string(N),                     \
                                TMP_CHECK_NAME(name, N),                       \
                                TMP_DECLARE_NAME(name, N));                    \
            return true;                                                       \
        }                                                                      \
    }                                                                          \
    static bool name##_registed = name##_regist<BATCH_SIZE>();
// NOLINTEND