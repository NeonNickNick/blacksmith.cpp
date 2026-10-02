#include "skill-system/professions.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <domain/models.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <tuple>
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
    mhp_ -= loss;
    hp_ = std::min(hp_, mhp_);
}
void health_component::gain_mhp(int gain) { mhp_ += gain; }
bool health_component::is_dead() const { return hp_ <= 0; }
//

// 防御组件
void defense_component::add(defense_entity &defense) {
    if (try_merge(defense)) {
        return;
    }
    defenses_.push_back(defense);
}
void defense_component::round_pass() {
    for (auto &defense : defenses_) {
        defense.update_(defense);
    }
    std::erase_if(defenses_, [](auto &d) { return d.clock_.is_dead(); });
}
bool defense_component::try_merge(defense_entity &defense) {
    if (!defense.can_merge_) {
        return false;
    }
    auto it = std::ranges::find_if(
        defenses_, [&defense](const auto &d) { return d.id_ == defense.id_; });
    if (it == defenses_.end()) {
        return false;
    }
    it->merge_(*it, defense);
    return true;
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
    callback_context_.round_pass();
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
resource_component::resource_component()
    : templates_{
          resource_template(resource_type::IRON, resource_type::GOLD_IRON),
          resource_template(resource_type::SPACE, resource_type::SPACE),
          resource_template(resource_type::TIME, resource_type::TIME),
          resource_template(resource_type::MAGIC, resource_type::MAGIC)} {}

std::size_t resource_component::template_index(resource_type type) {
    switch (type) {
    case resource_type::IRON:
    case resource_type::GOLD_IRON:
        return 0;
    case resource_type::SPACE:
        return 1;
    case resource_type::TIME:
        return 2;
    case resource_type::MAGIC:
        return 3;
    default:
        assert(false);
        return 0;
    }
}

bool resource_component::check(resource_type type, float need,
                               bool if_common_only) const {
    return templates_[template_index(type)].check(need, if_common_only);
}
void resource_component::use(resource_type type, float need,
                             bool if_common_only) {
    templates_[template_index(type)].use(need, if_common_only);
}
void resource_component::gain(resource_type type, float gain) {
    templates_[template_index(type)].gain(type, gain);
}
float resource_component::query(resource_type type) const {
    return templates_[template_index(type)].common_;
}
//

skill_action skill_action::copy() const {
    skill_action copy{.skill_ = this->skill_, .param_ = this->param_};
    if (this->next_ != nullptr) {
        copy.next_ = std::make_unique<skill_action>(this->next_->copy());
    }
    return copy;
}

// 职业技能组
profession_skill_set::profession_skill_set(
    const std::vector<
        std::tuple<skill, int, skill_check_func, skill_declare_func>> &skills,
    skill_check_func passive_check, skill_declare_func passive_declare) {
    for (const auto &skill : skills) {
        auto [name, param, check, declare] = skill;
        authorized_skills_.push_back(name);
        check_funcs_.emplace_back(name, param, check);
        declare_funcs_.emplace_back(name, param, declare);
    }

    auto proj = [](const auto &t) {
        return std::tie(std::get<0>(t), std::get<1>(t));
    };

    std::ranges::sort(check_funcs_, std::less{}, proj);
    std::ranges::sort(declare_funcs_, std::less{}, proj);

    std::ranges::sort(authorized_skills_);
    auto ret = std::ranges::unique(authorized_skills_);
    authorized_skills_.erase(ret.begin(), ret.end());

    passive_func_ = {passive_check, passive_declare};
}
//

// 职业组件
void profession_component::invoke_passive(skill_context &context) {
    for (const auto [check, declare] : passive_funcs_) {
        if (check(context)) {
            declare(context);
        }
    }
}
check_result profession_component::check(const skill_context &context) const {
    const auto NAME = context.action_.skill_;
    const auto PARAM = context.action_.param_;
    const auto *SET =
        skill_system::get_skill_profession_mapping()[static_cast<size_t>(NAME)];
    if (!std::ranges::binary_search(authorized_skills_, NAME)) {
        return check_result::INVALID;
    }

    auto it = std::ranges::lower_bound(
        SET->check_funcs_, std::tie(NAME, PARAM), std::less{},
        [](const auto &t) { return std::tie(std::get<0>(t), std::get<1>(t)); });

    if (it != SET->check_funcs_.end() && std::get<0>(*it) == NAME &&
        std::get<1>(*it) == PARAM) {
        return std::get<2>(*it)(context) ? check_result::SUCCESS
                                         : check_result::REJECTED;
    }
    return check_result::INVALID;
}
void profession_component::declare(skill_context &context) {
    const auto NAME = context.action_.skill_;
    const auto PARAM = context.action_.param_;
    const auto *SET =
        skill_system::get_skill_profession_mapping()[static_cast<size_t>(NAME)];
    auto it = std::ranges::lower_bound(
        SET->declare_funcs_, std::tie(NAME, PARAM), std::less{},
        [](const auto &t) { return std::tie(std::get<0>(t), std::get<1>(t)); });

    if (it != SET->declare_funcs_.end() && std::get<0>(*it) == NAME &&
        std::get<1>(*it) == PARAM) {
        std::get<2> (*it)(context);
        return;
    }
    assert(false);
}
void profession_component::add_profession(
    const profession_skill_set *skill_set) {
    for (const auto &skill_name : skill_set->authorized_skills_) {
        authorized_skills_.push_back(skill_name);
    }
    const auto [check, declare] = skill_set->passive_func_;
    if (check != nullptr && declare != nullptr) {
        passive_funcs_.emplace_back(check, declare);
    }
}
void profession_component::disable_skill(skill skill_name) {
    std::erase_if(authorized_skills_, [&skill_name](const auto &name) {
        return name == skill_name;
    });
}
const std::vector<skill> &profession_component::authorized_skills() const {
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
