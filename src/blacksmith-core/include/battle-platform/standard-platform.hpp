#pragma once

#include <cstdint>
#include <domain/models.hpp>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

using namespace blacksmith::domain;
namespace blacksmith::battle_platform {
class standard_pvp {
  public:
    enum class battle_result : uint8_t {
        PLAYER_WIN,
        ENEMY_WIN,
        DRAW,
        BATTLING
    };
    standard_pvp(bool enable_info = true);
    void reset();
    [[nodiscard]] community &player();
    [[nodiscard]] community &enemy();
    [[nodiscard]] int round() const;
    [[nodiscard]] skill_action player_action() const;
    [[nodiscard]] skill_action enemy_action() const;
    [[nodiscard]] skill_context collect_context(community &com);
    [[nodiscard]] std::optional<skill_context> to_context(std::string input,
                                                          community &com);
    [[nodiscard]] battle_result result() const;
    void submit_player_context(const skill_context &context);
    void submit_enemy_context(const skill_context &context);
    void set_callback(std::function<void()> &&callback);

  private:
    bool enable_info_{true};
    std::mutex mtx_;
    std::function<void()> callback_{[]() {}};
    community player_{&enemy_};
    community enemy_{&player_};
    int round_{1};
    skill_context player_context_{};
    skill_context enemy_context_{};
    bool player_submitted_{false};
    bool enemy_submitted_{false};
    void try_pass_round();
};
} // namespace blacksmith::battle_platform
