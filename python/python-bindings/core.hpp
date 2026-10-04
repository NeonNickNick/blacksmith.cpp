#include "battle-platform/standard-platform.hpp"
#include "blacksmith-master/blacksmith-zero.hpp"
using namespace blacksmith::battle_platform;
using namespace blacksmith::blacksmith_master;
namespace blacksmith::python_bindings {
template <typename T, typename P> class standard_test {
  public:
    void set_baseline_param(const P &param) {
        param_ = param;
        test_.set_baseline_initialize([param](T &t) { t.param_ = param; });
    }
    void set_test_param(const P &param) {
        param_ = param;
        test_.set_test_initialize([param](T &t) { t.param_ = param; });
    }
    float win_rate(int battle_times = 100) {
        return test_.win_rate(battle_times);
    }

  private:
    benchmark_test<blacksmith_zero, T> test_;
    P param_;
};
} // namespace blacksmith::python_bindings