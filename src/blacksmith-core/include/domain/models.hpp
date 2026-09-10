#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

// 此头文件定义了游戏领域对象，包括攻击、防御等数据结构、玩家数据结构、玩家组件数据结构

namespace blacksmith_core::domain {
enum class attack_type : std::uint8_t { PHYSICAL, MAGICAL, REAL };
enum class attack_stage : std::uint8_t { FIRST_TIME_HIT_ARMOR, HIT_BODY, END };
enum class defense_type : std::uint8_t {
    PHYSICAL_IMMUNITY,
    MAGICAL_IMMUNITY,
    PERCENTAGE_REDUCTION,
    REAL_REDUCTION,
    THORN_REDUCTION,
    COMMON_REDUCTION,
    STONE_SHELL,
    REAL_ARMOR,
    COMMON_ARMOR
};
enum class resource_type : std::uint8_t { IRON, GOLD_IRON, SPACE, TIME, MAGIC };

// 时钟：包含跨回合行为控制
struct clap_round_clock {
  public:
    int delayed_rounds_{0};
    int remaining_rounds_{1};
    bool is_infinite_{false};
    [[nodiscard]] bool is_ringing() const;
    [[nodiscard]] bool is_dead() const;
    void round_pass();
};
static_assert(std::copy_constructible<clap_round_clock>);
//

// 要求领域对象具有一个时钟
template <typename T>
concept analyzable_data = requires(T &t) {
    { t.clock_ } -> std::same_as<clap_round_clock &>;
} && std::is_copy_constructible_v<T>;
//

//
class body;
class community;
struct attack_data;
struct defense_entity;
struct defense_data;
struct resource_data;
//

// 攻击数据模型
using attack_execute_func = void (*)(community &, community &, attack_data &);
struct attack_data {
  public:
    clap_round_clock clock_;
    attack_type type_;
    int power_;
    float ap_factor_;
    attack_execute_func executor_;
    int total_damage_;
};
static_assert(analyzable_data<attack_data>);
//

// 防御实体数据模型，存放于玩家defense_component
using defend_attack_func = void (*)(community &, community &, defense_entity &,
                                    attack_data &);
using merge_func = void (*)(defense_entity &);
using update_func = void (*)(defense_entity &);
struct defense_entity {
  public:
    defense_type type_;
    int power_;
    clap_round_clock clock_;
    defend_attack_func defender_;
    update_func update_;
    bool can_merge_ = false;
    merge_func merge_ = nullptr;
};
static_assert(analyzable_data<defense_entity>);
//

// 防御数据模型
using defense_execute_func = void (*)(community &, community &, defense_data &);
struct defense_data {
  public:
    clap_round_clock clock_;
    defense_entity defense_;
    defense_execute_func executor_;
};
static_assert(analyzable_data<defense_data>);
//

// 资源数据模型
using resource_execute_func = void (*)(community &, community &,
                                       resource_data &);
struct resource_data {
  public:
    clap_round_clock clock_;
    resource_type type_;
    float power_;
    resource_execute_func executor_;
};
static_assert(analyzable_data<resource_data>);
//

// 生命组件
class health_component {
  private:
    int hp_ = 10;
    int mhp_ = 10;

  public:
    health_component() = default;
    void lose_hp(int loss);
    void gain_hp(int gain);
    void lose_mhp(int loss);
    void gain_mhp(int gain);
    [[nodiscard]] bool is_dead() const;
};
//

// 防御组件
class defense_component {
  public:
    std::vector<defense_entity> defenses_;
    void add(const defense_entity &defense);
};
//

// 资源组件
class resource_component {
    class resource_template {
      private:
        resource_type common_type_;
        resource_type gold_type_;
        float common_{0};
        float gold_{0};

      public:
        resource_template() = delete;
        resource_template(resource_template &&other) = default;
        resource_template(resource_type common_type, resource_type gold_type);
        [[nodiscard]] bool check(float need, bool if_common_only = false) const;
        void use(float need, bool if_common_only = false);
        void gain(resource_type type, float add);
    };

  private:
    std::unordered_map<resource_type, std::shared_ptr<resource_template>>
        resources_;

  public:
    resource_component();
    [[nodiscard]] bool check(resource_type type, float need,
                             bool if_common_only = false) const;
    void use(resource_type type, float need, bool if_common_only = false);
    void gain(resource_type type, float gain);
};
//

// 回合上下文组件
class turn_context_component {
    template <analyzable_data T> class context_unit {
      private:
        std::vector<T> datas_;

      public:
        template <typename... Args> void write(Args &&...args) {
            datas_.emplace_back(std::forward<Args>(args)...);
        }
    };

  public:
    context_unit<attack_data> attack_context_;
    context_unit<defense_data> defense_context_;
    context_unit<resource_data> resource_context_;
};
//

// 技能上下文
struct skill_context {
  public:
    std::string skill_name_;
    int param_;
    community &self_;
    std::unique_ptr<skill_context> next_;
};
//

// 职业技能组
using skill_check_func = bool (*)(const skill_context &);
using skill_declare_func = void (*)(skill_context &);
class profession_skill_set {
  public:
    static constexpr int MAX_SKILL_COUNT = 10;
    profession_skill_set(
        const std::vector<std::tuple<std::string, skill_check_func,
                                     skill_declare_func>> &skills);
    void disable_skill(const std::string &skill_name);
    std::vector<std::string> authorized_skills_;
    std::array<std::pair<std::string, skill_check_func>, MAX_SKILL_COUNT>
        check_funcs_{};
    std::array<std::pair<std::string, skill_declare_func>, MAX_SKILL_COUNT>
        declare_funcs_{};
};
//
enum class check_result : std::uint8_t { SUCCESS, REJECTED, INVALID };
// 职业组件
class profession_component {
  public:
    check_result check(const skill_context &context);
    void declare(skill_context &context);
    void add_profession(const profession_skill_set *skill_set);

  private:
    std::vector<const profession_skill_set *> skill_sets_;
};
//

// 玩家
class body {
  public:
    health_component health_;
    defense_component defense_;
    resource_component resource_;
    turn_context_component turn_context_;
    profession_component profession_;
};
//

// 社区：包装玩家和召唤物
class community {
  public:
    body focus_;
    bool is_player_;
};
//
} // namespace blacksmith_core::domain
