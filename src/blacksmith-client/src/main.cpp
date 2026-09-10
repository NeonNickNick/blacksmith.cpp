#include "domain/models.hpp"
#include "domain/transformations.hpp"
blacksmith_core::domain::community player{};
blacksmith_core::domain::community enemy{};
int main() {
    blacksmith_core::domain::initialize(player, enemy);
    return 0;
}
