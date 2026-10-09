#pragma once
namespace zero {
inline void append_player(std::vector<float>& out, community& player, int last_move) {
    const auto& body = player.focus_;
    out.push_back(body.health_.hp_ / 10.0F);
    out.push_back(body.health_.mhp_ / 10.0F);
    for (int i = 0; i < 5; ++i) {
        out.push_back(body.resource_.query(static_cast<resource_type>(i)) / 10.0F);
    }
    // The public query combines ordinary and gold iron; common-only checks
    // expose the ordinary component without accessing private storage.
    float ordinary = 0;
    const float total = body.resource_.query(resource_type::IRON);
    for (float amount = 0.5F; amount <= total; amount += 0.5F) {
        if (!body.resource_.check(resource_type::IRON, amount, true)) break;
        ordinary = amount;
    }
    out.push_back(ordinary / 10.0F);
    out.push_back(body.profession_.have_extra_profession_ ? 1.0F : 0.0F);
    std::array<float, skill_count> authorized{};
    for (skill move : body.profession_.authorized_skills()) authorized[static_cast<int>(move)] = 1;
    out.insert(out.end(), authorized.begin(), authorized.end());
    for (int i = 0; i <= skill_count; ++i) out.push_back(i == last_move ? 1.0F : 0.0F);

    // The checkpoint schema reserves ten mark slots, including COUNTER_READY.
    std::array<float, 10 * delay_bins> marks{};
    for (const auto& mark : body.mark_.marks_) {
        int delay = std::clamp(mark.clock_.delayed_rounds_, 0, delay_bins - 1);
        marks.at(static_cast<int>(mark.id_) * delay_bins + delay) += 0.5F;
    }
    out.insert(out.end(), marks.begin(), marks.end());
    std::array<float, 3 * delay_bins * 3> attacks{};
    for (const auto& attack : body.turn_context_.attack_context_.datas_) {
        int delay = std::clamp(attack.clock_.delayed_rounds_, 0, delay_bins - 1);
        int offset = (static_cast<int>(attack.type_) * delay_bins + delay) * 3;
        attacks[offset] += attack.power_ / 10.0F;
        attacks[offset + 1] += attack.power_ * attack.ap_factor_ / 20.0F;
        attacks[offset + 2] += attack.clock_.is_infinite_ ? 1.0F : attack.clock_.remaining_rounds_ / 5.0F;
    }
    out.insert(out.end(), attacks.begin(), attacks.end());
    std::array<float, 9 * delay_bins> defenses{};
    auto record_defense = [&](const defense_entity& defense, const clap_round_clock& clock) {
        int delay = std::clamp(clock.delayed_rounds_, 0, delay_bins - 1);
        defenses[static_cast<int>(defense.type_) * delay_bins + delay] += defense.power_ / 10.0F;
    };
    for (const auto& defense : body.defense_.defenses_) record_defense(defense, defense.clock_);
    for (const auto& defense : body.turn_context_.defense_context_.datas_) record_defense(defense.defense_, defense.clock_);
    out.insert(out.end(), defenses.begin(), defenses.end());
    std::array<float, 7 * delay_bins> resources{};
    for (const auto& resource : body.turn_context_.resource_context_.datas_) {
        int delay = std::clamp(resource.clock_.delayed_rounds_, 0, delay_bins - 1);
        resources[static_cast<int>(resource.type_) * delay_bins + delay] += resource.power_ / 10.0F;
    }
    out.insert(out.end(), resources.begin(), resources.end());
    // Preserve trained feature positions when the engine has no counter-preparation stage.
    std::array<float, 8 * delay_bins> callbacks{};
    for (const auto& callback : body.turn_context_.callback_context_.datas_) {
        int delay = std::clamp(callback.clock_.delayed_rounds_, 0, delay_bins - 1);
        const int stage = static_cast<int>(callback.stage_);
        callbacks.at(stage * delay_bins + delay) += 0.5F;
    }
    out.insert(out.end(), callbacks.begin(), callbacks.end());
    out.push_back(body.effect_.effects_.size() / 5.0F);
    out.push_back(body.turn_context_.effect_context_.datas_.size() / 5.0F);
}

}
