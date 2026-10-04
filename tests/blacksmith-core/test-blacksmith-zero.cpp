#include "battle-platform/standard-platform.hpp"
#include <blacksmith-master/blacksmith-zero.hpp>
#include <cstddef>
#include <vector>
std::vector<float> scale = {
    1.52641411F,  0.88290204F,  0.35274672F,  0.61542097F, 0.83097131F,
    0.61222351F,  0.3292151F,   1.65260821F,  1.73401696F, 0.77265715F,
    -0.01689368F, 1.39116813F,  1.13587425F,  1.28306686F, -0.28837448F,
    1.69835323F,  -0.93580848F, -0.18005078F, 1.02364501F};
int main() {
    blacksmith::blacksmith_master::blacksmith_zero_param param{};
    auto v = param.to_vector();
    for (size_t i = 0; i < v.size(); ++i) {
        v[i] *= scale[i];
    }
    param.from_vector(v);
    blacksmith::battle_platform::simple_pvp_with_ai<
        blacksmith::blacksmith_master::blacksmith_zero,
        blacksmith::blacksmith_master::blacksmith_zero_param>
        pvp{param};
    pvp.play();
    return 0;
}
