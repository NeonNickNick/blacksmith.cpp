#include "battle-platform/standard-platform.hpp"
#include "blacksmith-master/blacksmith-zero.hpp"
#include <fstream>
#include <iostream>
#include <string>
using namespace blacksmith::battle_platform;
using namespace blacksmith::blacksmith_master;
namespace blacksmith::python_bindings {
template <typename T, typename P> class standard_test {
  public:
    bool save_param_to(const std::string &filename) {
        std::ofstream outFile(filename, std::ios::binary);
        if (!outFile) {
            std::cerr << "Failed to open: " << filename << '\n';
            return false;
        }
        outFile.write(reinterpret_cast<const char *>(&param_), sizeof(P));
        if (!outFile) {
            std::cerr << "Failed to write: " << filename << '\n';
            return false;
        }
        outFile.close();
        return true;
    }
    bool load_param_from(const std::string &filename) {
        std::ifstream inFile(filename, std::ios::binary);
        if (!inFile) {
            std::cerr << "Failed to open: " << filename << '\n';
            return false;
        }
        inFile.read(reinterpret_cast<char *>(&param_), sizeof(P));
        if (!inFile) {
            std::cerr << "Failed to read: " << filename << '\n';
            return false;
        }
        inFile.close();
        return true;
    }
    void set_param(const P &param) {
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