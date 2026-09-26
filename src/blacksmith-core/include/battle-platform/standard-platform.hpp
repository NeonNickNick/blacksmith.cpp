#include <domain/models.hpp>
#include <functional>
#include <mutex>
using namespace blacksmith_core::domain;
namespace blacksmith_core::battle_platform {
class standard_pvp {
  public:
    [[nodiscard]] community &player();
    [[nodiscard]] community &enemy();
    void submit_player_context(skill_context &&context);
    void submit_enemy_context(skill_context &&context);
    void set_callback(std::function<void()> &&callback);

  private:
    std::mutex mtx_;
    std::function<void()> callback_;
    community player_{};
    community enemy_{};
    skill_context player_context_{};
    skill_context enemy_context_{};
    bool player_submitted_{false};
    bool enemy_submitted_{false};
    void try_pass_round();
};

} // namespace blacksmith_core::battle_platform