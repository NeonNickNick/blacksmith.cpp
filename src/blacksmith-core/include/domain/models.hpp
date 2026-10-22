#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// 此头文件定义了游戏领域对象，包括攻击、防御等数据结构、玩家数据结构、玩家组件数据结构

namespace blacksmith::domain {
enum class attack_type : std::uint8_t { PHYSICAL, MAGICAL, REAL, SIZE };
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
    COMMON_ARMOR,
    GREY_HP,
    DAMAGE_TRANSFER,
    SIZE
};
enum class resource_type : std::uint8_t {
    IRON,
    GOLD_IRON,
    SPACE,
    TIME,
    MAGIC,
    CROSS,
    DREAM,
    SPIRIT,
    FIRE,
    WATER,
    WOOD,
    EARTH,
    BOLT,
    JADE,
    CRYSTAL,
    CRYSTAL_BALL,
    SIZE,
    HP,
    MHP
};

// 时钟：包含跨回合行为控制
struct clap_round_clock {
  public:
    int delayed_rounds_{0};
    int remaining_rounds_{1};
    bool is_infinite_{false};
    [[nodiscard]] bool is_ringing() const;
    [[nodiscard]] bool is_dead() const;
    void set_dead();
    void round_pass();
};
static_assert(std::copy_constructible<clap_round_clock>);
//

// 要求领域对象具有一个时钟
template <typename T>
concept context_data = requires(T &t) {
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
struct effect_entity;
struct effect_data;
//

// 攻击数据模型
using attack_execute_func = void (*)(community &, community &, attack_data &);
struct attack_data {
  public:
    clap_round_clock clock_;
    attack_type type_;
    int power_;
    float ap_factor_;
    bool interrupt_;
    attack_execute_func executor_;
    int total_damage_;
    int body_damage_factor_ = 1;
};
static_assert(context_data<attack_data>);
//

// 防御实体数据模型，存放于玩家defense_component
using defend_attack_func = void (*)(community &, community &, defense_entity &,
                                    attack_data &);
using merge_func = void (*)(defense_entity &, defense_entity &);
using update_func = void (*)(community &, defense_entity &);
enum class defense_id : uint8_t {
    DEFAULT,
    ARMOR12,
    MONK_CLONES,
    NIGHTMARE_ARMOR,
    ELEMENTAL_ARMOR,
    CRYSTAL_WALL,
    STONE_SHELL,
};
inline extern void default_update(community &player, defense_entity &defense);
struct defense_entity {
  public:
    defense_id id_{defense_id::DEFAULT};
    defense_type type_;
    int power_;
    clap_round_clock clock_{};
    defend_attack_func defender_;
    update_func update_{default_update};
    bool can_merge_ = false;
    merge_func merge_ = nullptr;
};
static_assert(context_data<defense_entity>);
//

// 防御数据模型
using defense_execute_func = void (*)(community &, community &, defense_data &);
struct defense_data {
  public:
    clap_round_clock clock_;
    defense_entity defense_;
    defense_execute_func executor_;
};
static_assert(context_data<defense_data>);
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
static_assert(context_data<resource_data>);
//

// 效果实体数据模型，存放于玩家defense_component
using take_effect_func = void (*)(community &, community &, effect_entity &);
struct effect_entity {
  public:
    clap_round_clock clock_;
    take_effect_func take_effect_{};
};
static_assert(context_data<effect_entity>);
//

// 效果数据模型
enum class effect_target_type : uint8_t { SELF, ENEMY };
using effect_execute_func = void (*)(community &, community &, effect_data &);
struct effect_data {
  public:
    clap_round_clock clock_;
    effect_target_type type_;
    effect_entity effect_;
    effect_execute_func executor_;
};
static_assert(context_data<effect_data>);
//

// 标记实体
enum class mark_id : uint8_t {
    CANNON,

    DRIVER,

    SKY_STRIKE,
    TYRANT_DESTRUCTION,
    DRAGON_TOOTH,
    TRIPLE_STAB,
    CHARGE,
    COUNTER_ATTACK,

    BLOODSIGIL,
    PROFANATION,
    CROSSBOW_AIM,
    GREATEST_CAUTION,
    REVELATION,
    CRYSTAL_WALL_HIT,
    DELAYED_CRYSTAL_WALL_HIT,
    SIZE,
};
struct mark_entity {
  public:
    clap_round_clock clock_;
    mark_id id_;
};
static_assert(context_data<mark_entity>);
//

// 回调
enum class callback_stage : uint8_t {

    BEFORE_APPLY_EFFECT,
    BEFORE_TAKE_EFFECT,
    BEFORE_APPLY_DEFENSE,
    BEFORE_CANCEL_ATTACK,
    BEFORE_APPLY_ATTACK,
    BEFORE_APPLY_RESOURCE,
    BEFORE_ROUND_PASS,
    BEFORE_END,
    SIZE
};
struct callback_data {
  public:
    clap_round_clock clock_;
    callback_stage stage_;
    void (*callback_)(community &player, community &enemy);
};
static_assert(context_data<callback_data>);
//

// 生命组件
class health_component {
  public:
    int hp_ = 10;
    int mhp_ = 10;
    void lose_hp(int loss);
    void gain_hp(int gain);
    void lose_mhp(int loss);
    void gain_mhp(int gain);
    [[nodiscard]] bool is_dead() const;
    void print_info() const;
};
//

// 防御组件
class defense_component {
  public:
    std::vector<defense_entity> defenses_;
    void add(defense_entity &defense);
    void round_pass(community &player);

  private:
    bool try_merge(defense_entity &defense);
};
//

// 效果组件
class effect_component {
  public:
    std::vector<effect_entity> effects_;
    void add(const effect_entity &effect);
    void round_pass();
};
//

// 标记组件
class mark_component {
  public:
    std::vector<mark_entity> marks_;
    void add(const mark_entity &mark);
    void round_pass();
};
//

// 资源组件
class resource_component {
    class resource_template {
      public:
        std::string name_;
        resource_type common_type_;
        resource_type gold_type_;
        float common_{0};
        float gold_{0};

        resource_template(std::string &&name, resource_type common_type,
                          resource_type gold_type);
        [[nodiscard]] bool check(float need, bool if_common_only = false) const;
        void use(float need, bool if_common_only = false);
        void gain(resource_type type, float add);
    };

  private:
    static constexpr std::size_t RESOURCE_NUMS = 15;
    std::array<resource_template, RESOURCE_NUMS> templates_;
    [[nodiscard]] static std::size_t template_index(resource_type type);

  public:
    resource_component();
    [[nodiscard]] bool check(resource_type type, float need,
                             bool if_common_only = false) const;
    void use(resource_type type, float need, bool if_common_only = false);
    void gain(resource_type type, float gain);
    [[nodiscard]] float query(resource_type type) const;
    [[nodiscard]] float query_common(resource_type type) const;
    [[nodiscard]] float query_gold(resource_type type) const;
    void print_info() const;
};
//

// 回合上下文组件
class turn_context_component {
    template <context_data T> class context_unit {
      public:
        std::vector<T> datas_;
        template <typename... Args> [[nodiscard]] T &write(Args &&...args) {
            auto data = T(std::forward<Args>(args)...);
            for (const auto PREPROCESS : preprocessors_) {
                PREPROCESS(data);
            }
            return datas_.emplace_back(data);
        }
        void register_preprocess(void (*preprocess)(T &)) {
            preprocessors_.push_back(preprocess);
        }
        void round_pass() {
            for (auto &data : datas_) {
                data.clock_.round_pass();
            }
            std::erase_if(datas_, [](T &t) { return t.clock_.is_dead(); });
        }

      private:
        std::vector<void (*)(T &)> preprocessors_;
    };

  public:
    context_unit<attack_data> attack_context_;
    context_unit<defense_data> defense_context_;
    context_unit<resource_data> resource_context_;
    context_unit<effect_data> effect_context_;
    context_unit<callback_data> callback_context_;
    void round_pass();
};
//

// 可复制的技能动作；搜索树节点与技能上下文共用这一值对象。
enum class skill : uint8_t {
    IRON,
    STICK,
    DRILL,
    SLASH,
    TEAR,
    SHIELD,
    THORN_SHIELD,
    RECOVERY,
    SPACE,
    TIME,
    REFLECT,
    DELAY_PROTECTION,
    ARMOR12,

    WARLOCK,
    ALCHEMY,
    CANNON,
    DRIVER,
    LANCER,
    BLOODSIGIL,

    MAGIC,
    MAGIC_ATTACK,
    MAGIC_SHIELD,
    SACRIFICE,
    MUTE,

    MIDASTOUCH,

    STRIKE,
    DOUBLE_STRIKE,
    TRIPLE_STRIKE,
    APSHELL,
    CANNON_BARRIER,

    SPACE_ATTACK,
    TIME2SPACE,
    SPACE2TIME,
    SPACE_BARRIER,

    SKY_STRIKE,
    TYRANT_DESTURCTION,
    DRAGON_TOOTH,
    TRIPLE_STAB,
    CHARGE,
    RISING_DRAGON,

    BLOOD_BLADE,
    BLOOD_LUST,
    BLOOD_RECOVERY,
    BLOOD_RAGE,

    // MONK,
    // PROPHET,
    HOLYBOOK,
    PHANTOMBOOK,
    CAULDRON,
    CROSSBOW,

    // JADE,
    // GHOST_STEP,
    // GOLDEN_BELL_COVER,
    // MAZE_FIST,
    // MIST,
    // DISILLUSIONMENT,

    // CRYSTAL,
    // CRYSTAL_BALL,
    // FORETOLD,
    // GREATEST_CAUTION,
    // REVELATION,
    // ASSERTION,
    // CRYSTAL_WALL,
    // REFRACTION,
    // ULTIMATUM,

    CROSS,
    PRAYER,
    ARK,
    PROFANATION,
    REBIRTH,
    ABSOLUTION,

    FANTASIA,
    ASSOCIATION,
    HALLUCINATION,
    DREAM_WAKE,
    ILLUSION,
    NIGHTMARE,
    DREAM_DIVE,
    MATERIALIZATION,
    HAUNT,
    CHANNELING,

    FIRE,
    WATER,
    WOOD,
    EARTH,
    EXPLOSION,
    ICE_BLADE,
    REGENERATION,
    STONE_SHELL,
    BURN_LIFE,
    FIRE_RAIN,
    ELEMENTAL_ARMOR,
    HAMMER,
    GUARD,

    MAKE_BOLTS,
    ARROW_RAIN,
    AIM,
    CRITICAL,
    BLOCK,
    MARK_SHOT,

    SIZE,
};
struct skill_action {
  public:
    skill skill_;
    int param_ = 0;
    std::unique_ptr<skill_action> next_{nullptr};
    [[nodiscard]] skill_action copy() const;
};

// 技能上下文

struct skill_context {
  public:
    skill_action action_;
    community *self_ = nullptr;
};
//

// 通用技能包：公共技能、职业、副装备和临时装备模式共用。
using skill_check_func = bool (*)(const skill_context &);
using skill_declare_func = void (*)(skill_context &);
class skill_set {
  public:
    skill_set(const std::vector<skill> &skills, skill_check_func passive_check,
              skill_declare_func passive_declare);
    std::vector<skill> authorized_skills_;
    std::pair<skill_check_func, skill_declare_func> passive_func_{nullptr,
                                                                  nullptr};
};
//
enum class check_result : std::uint8_t { SUCCESS, REJECTED, INVALID };
// 根节点的互斥职业身份；副装备不占用职业身份。
enum class profession_type : uint8_t {
    NONE,
    WARLOCK,
    CANNON,
    DRIVER,
    LANCER,
    BLOODSIGIL,
    // MONK,
    // PROPHET,
    HOLYBOOK,
    PHANTOMBOOK,
    CROSSBOW,
    SIZE,
};
// 技能权限组件；具体节点的获取入口和互斥规则由技能包声明。
class skill_component {
  public:
    void invoke_passive(skill_context &context);
    [[nodiscard]] check_result check(const skill_context &context) const;
    void declare(skill_context &context);

    void disable_skill(skill skill_name);
    void disable_skills(const std::span<const skill> &skill_names);
    void enable_skill(skill skill_name);
    void enable_skills(const std::span<const skill> &skill_names);

    void remove_skill_set(const skill_set *skill_set);
    void add_skill_set(const skill_set *skill_set);

    void lock_all_skills(const std::span<const skill> &except_skills = {});
    void release_all_skills(const std::span<const skill> &except_skills = {});
    [[nodiscard]] const std::vector<skill> &authorized_skills() const;
    bool has_profession_{false};
    profession_type profession_{profession_type::NONE};

  private:
    std::vector<skill> authorized_skills_;
    // An unacquired skill starts with one disable; acquiring it releases it.
    std::array<int, static_cast<size_t>(skill::SIZE)> skill_disable_counts_ =
        [] {
            std::array<int, static_cast<size_t>(skill::SIZE)> counts{};
            counts.fill(1);
            return counts;
        }();
    std::vector<std::pair<skill_check_func, skill_declare_func>> passive_funcs_;
};
//

/* Temporarily disabled profession state.
struct monk_clone {
    health_component health_;
    int golden_bell_uses_ = 0;
    int mist_rounds_ = 0;
};
struct monk_component {
    std::vector<monk_clone> clones_;
    int golden_bell_uses_ = 0;
    int mist_cooldown_ = 0;
    int transfer_rounds_ = 0;
};
struct prophet_component {
    bool evaded_ = false;
    int caution_ = 0;
    int revelation_ = 0;
};
*/

// 玩家
class body {
  public:
    health_component health_;
    defense_component defense_;
    resource_component resource_;
    effect_component effect_;
    mark_component mark_;
    turn_context_component turn_context_;
    skill_component skills_;
    // monk_component monk_;
    // prophet_component prophet_;
    void print_info() const;
};
//

// 社区：包装玩家和召唤物
class community {
  public:
    explicit community(community *opponent);
    community(const community &other);
    community &operator=(const community &other);
    community(community &&) = default;
    community &operator=(community &&other) noexcept;

    // 非拥有指针；赋值只替换玩家状态，保留当前对局的对手。
    community *opponent_;
    body focus_;
    skill current_skill_{skill::IRON};
    std::vector<skill_action> history_;
};
//
} // namespace blacksmith::domain
