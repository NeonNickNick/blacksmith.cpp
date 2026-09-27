#include <algorithm>
#include <array>
#include <cassert>
#include <domain/models.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace blacksmith_core::domain {
// 时钟：包含跨回合行为控制
bool clap_round_clock::is_ringing() const {
    return remaining_rounds_ > 0 && delayed_rounds_ == 0;
}
bool clap_round_clock::is_dead() const { return remaining_rounds_ <= 0; }
void clap_round_clock::round_pass() {
    if (is_infinite_) {
        return;
    }
    if (delayed_rounds_ > 0) {
        delayed_rounds_--;
        return;
    }
    if (remaining_rounds_ > 0) {
        remaining_rounds_--;
        return;
    }
}
//

// 生命组件
void health_component::lose_hp(int loss) { hp_ -= loss; }
void health_component::gain_hp(int gain) {
    hp_ += gain;
    hp_ = std::min(hp_, mhp_);
}
void health_component::lose_mhp(int loss) {
    hp_ -= loss;
    mhp_ -= loss;
    hp_ = std::min(hp_, mhp_);
}
void health_component::gain_mhp(int gain) { mhp_ += gain; }
bool health_component::is_dead() const { return hp_ <= 0; }
//

// 防御组件
void defense_component::add(const defense_entity &defense) {
    defenses_.push_back(defense);
}
void defense_component::round_pass() {
    for (auto &defense : defenses_) {
        defense.update_(defense);
    }
    std::erase_if(defenses_, [](auto &d) { return d.clock_.is_dead(); });
}
//

// 效果组件
void effect_component::add(const effect_entity &effect) {
    effects_.push_back(effect);
}
void effect_component::round_pass() {
    for (auto &effect : effects_) {
        effect.clock_.round_pass();
    }
    std::erase_if(effects_, [](auto &d) { return d.clock_.is_dead(); });
}
//

// 标记组件
void mark_component::add(const mark_entity &mark) { marks_.push_back(mark); }
void mark_component::round_pass() {
    for (auto &mark : marks_) {
        mark.clock_.round_pass();
    }
    std::erase_if(marks_, [](auto &d) { return d.clock_.is_dead(); });
}
//

// 回合上下文组件
void turn_context_component::round_pass() {
    attack_context_.round_pass();
    defense_context_.round_pass();
    resource_context_.round_pass();
    effect_context_.round_pass();
}
//

// 资源组件
resource_component::resource_template::resource_template(
    resource_type common_type, resource_type gold_type) {
    common_type_ = common_type;
    gold_type_ = gold_type;
}
bool resource_component::resource_template::check(float need,
                                                  bool if_common_only) const {
    float temp = common_;
    if (!if_common_only) {
        temp += gold_;
    }
    return temp >= need;
}
void resource_component::resource_template::use(float need,
                                                bool if_common_only) {
    assert(check(need, if_common_only));
    if (!if_common_only) {
        if (need <= gold_) {
            gold_ -= need;
        } else {
            common_ -= need - gold_;
            gold_ = 0;
        }
    } else {
        common_ -= need;
    }
}
void resource_component::resource_template::gain(resource_type type,
                                                 float add) {
    if (type == common_type_) {
        common_ += add;
    } else if (type == gold_type_) {
        gold_ += add;
    } else {
        assert(false);
    }
}
resource_component::resource_component() {
    auto iron = std::make_shared<resource_template>(resource_type::IRON,
                                                    resource_type::GOLD_IRON);
    resources_.emplace(resource_type::IRON, iron);
    resources_.emplace(resource_type::GOLD_IRON, iron);
    resources_.emplace(resource_type::SPACE,
                       std::make_shared<resource_template>(
                           resource_type::SPACE, resource_type::SPACE));
    resources_.emplace(resource_type::TIME,
                       std::make_shared<resource_template>(
                           resource_type::TIME, resource_type::TIME));
    resources_.emplace(resource_type::MAGIC,
                       std::make_shared<resource_template>(
                           resource_type::MAGIC, resource_type::MAGIC));
}
resource_component::resource_component(const resource_component &other)
    : resource_component() {
    for (const auto TYPE :
         {resource_type::IRON, resource_type::GOLD_IRON, resource_type::SPACE,
          resource_type::TIME, resource_type::MAGIC}) {
        const auto SOURCE = other.resources_.at(TYPE);
        auto destination = resources_.at(TYPE);
        destination->common_ = SOURCE->common_;
        destination->gold_ = SOURCE->gold_;
    }
}
resource_component &
resource_component::operator=(const resource_component &other) {
    if (this != &other) {
        resource_component copy(other);
        resources_ = std::move(copy.resources_);
    }
    return *this;
}
bool resource_component::check(resource_type type, float need,
                               bool if_common_only) const {
    auto it = resources_.find(type);
    if (it != resources_.end()) {
        return it->second->check(need, if_common_only);
    }
    assert(false);
    return false;
}
void resource_component::use(resource_type type, float need,
                             bool if_common_only) {
    auto it = resources_.find(type);
    if (it != resources_.end()) {
        it->second->use(need, if_common_only);
        return;
    }
    assert(false);
}
void resource_component::gain(resource_type type, float gain) {
    auto it = resources_.find(type);
    if (it != resources_.end()) {
        it->second->gain(type, gain);
        return;
    }
    assert(false);
}
float resource_component::query(resource_type type) const {
    return resources_.at(type)->common_;
}
//

skill_action skill_action::copy() const {
    skill_action copy{.skill_name_ = this->skill_name_, .param_ = this->param_};
    if (this->next_ != nullptr) {
        copy.next_ = std::make_unique<skill_action>(this->next_->copy());
    }
    return copy;
}

// 职业技能组
profession_skill_set::profession_skill_set(
    const std::vector<
        std::tuple<std::string, skill_check_func, skill_declare_func>> &skills,
    skill_check_func passive_check, skill_declare_func passive_declare) {
    int index = 0;
    for (const auto &skill : skills) {
        assert(index < MAX_SKILL_COUNT);
        auto [name, check, declare] = skill;
        authorized_skills_.push_back(name);
        check_funcs_[index] = {name, check};
        declare_funcs_[index] = {name, declare};
        index++;
    }
    passive_func_ = {passive_check, passive_declare};
}
//

// 职业组件
void profession_component::invoke_passive(skill_context &context) {
    for (const auto *set : skill_sets_) {
        const auto [check, declare] = set->passive_func_;
        if (check == nullptr || declare == nullptr) {
            continue;
        }
        if (check(context)) {
            declare(context);
        }
    }
}
check_result profession_component::check(const skill_context &context) const {
    const auto &NAME = context.action_.skill_name_;
    if (std::ranges::find(authorized_skills_, NAME) ==
        authorized_skills_.end()) {
        return check_result::INVALID;
    }
    for (const auto *const SET : skill_sets_) {
        const auto IT = std::ranges::find_if(
            SET->check_funcs_,
            [&NAME](const std::pair<std::string, skill_check_func> &t) {
                return t.first == NAME;
            });
        if (IT != SET->check_funcs_.end()) {
            return IT->second(context) ? check_result::SUCCESS
                                       : check_result::REJECTED;
        }
    }
    return check_result::INVALID;
}
void profession_component::declare(skill_context &context) {
    const auto &NAME = context.action_.skill_name_;
    for (const auto *const SET : skill_sets_) {
        const auto IT = std::ranges::find_if(
            SET->declare_funcs_,
            [&NAME](const std::pair<std::string, skill_declare_func> &t) {
                return t.first == NAME;
            });
        if (IT != SET->declare_funcs_.end()) {
            IT->second(context);
            return;
        }
    }
    assert(false);
}
void profession_component::add_profession(
    const profession_skill_set *skill_set) {
    skill_sets_.push_back(skill_set);
    for (const auto &skill_name : skill_set->authorized_skills_) {
        authorized_skills_.push_back(skill_name);
    }
}
void profession_component::disable_skill(const std::string &skill_name) {
    std::erase_if(authorized_skills_, [&skill_name](const auto &name) {
        return name == skill_name;
    });
}
const std::vector<std::string> &profession_component::available_skills() const {
    return authorized_skills_;
}
//

// 玩家
void body::print_info() const {
    std::cout << '\n';
    std::cout << "HP:" << health_.hp_ << "/" << health_.mhp_ << '\n';
    std::cout << "IRON:" << resource_.query(resource_type::IRON)
              << "   SPACE:" << resource_.query(resource_type::SPACE) << '\n';
}
//
} // namespace blacksmith_core::domain
