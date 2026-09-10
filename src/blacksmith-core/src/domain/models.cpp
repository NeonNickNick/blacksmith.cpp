#include <algorithm>
#include <array>
#include <cassert>
#include <domain/models.hpp>
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
    hp_ = std::min(hp_, mhp_);
}
void health_component::gain_mhp(int gain) { mhp_ += gain; }
bool health_component::is_dead() const { return hp_ <= 0; }
//

// 防御组件
void defense_component::add(const defense_entity &defense) {
    defenses_.push_back(defense);
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
//

// 职业技能组
profession_skill_set::profession_skill_set(
    const std::vector<std::tuple<std::string, skill_check_func,
                                 skill_declare_func>> &skills) {
    int index = 0;
    for (const auto &skill : skills) {
        auto [name, check, declare] = skill;
        authorized_skills_.push_back(name);
        check_funcs_[index] = {name, check};
        declare_funcs_[index] = {name, declare};
        index++;
    }
}
void profession_skill_set::disable_skill(const std::string &skill_name) {
    std::erase(authorized_skills_, skill_name);
}
//

// 职业组件
check_result profession_component::check(const skill_context &context) {
    for (const auto &set : skill_sets_) {
        const auto NAME = context.skill_name_;
        if (std::ranges::find(set->authorized_skills_, NAME) !=
            set->authorized_skills_.end()) {
            return std::ranges::find_if(
                       set->check_funcs_,
                       [&NAME](
                           const std::pair<std::string, skill_check_func> &t) {
                           return t.first == NAME;
                       })->second(context)
                       ? check_result::SUCCESS
                       : check_result::REJECTED;
        }
    }
    return check_result::INVALID;
}
void profession_component::declare(skill_context &context) {
    for (const auto &set : skill_sets_) {
        const auto NAME = context.skill_name_;
        if (std::ranges::find(set->authorized_skills_, NAME) !=
            set->authorized_skills_.end()) {
            std::ranges::find_if(
                set->declare_funcs_,
                [&NAME](const std::pair<std::string, skill_declare_func> &t) {
                    return t.first == NAME;
                })
                ->second(context);
            return;
        }
    }
    assert(false);
}
void profession_component::add_profession(
    const profession_skill_set *skill_set) {
    skill_sets_.push_back(skill_set);
}
//
} // namespace blacksmith_core::domain
