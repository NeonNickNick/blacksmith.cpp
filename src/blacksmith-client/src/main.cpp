#include "domain/models.hpp"
#include "domain/transformations.hpp"
#include <charconv>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
bool to_int(std::string_view sv, int &out) {
    const char *first = sv.data();
    const char *last = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc() && ptr == last;
}

int main() {
    blacksmith_core::domain::community player{};
    blacksmith_core::domain::community enemy{};
    blacksmith_core::domain::skill_context player_context{
        .skill_name_ = "iron", .param_ = 0, .self_ = player};
    blacksmith_core::domain::skill_context enemy_context{
        .skill_name_ = "iron", .param_ = 0, .self_ = enemy};
    std::cout << "Game start." << '\n';
    blacksmith_core::domain::initialize(player, enemy);
    auto get_context = [](community &com, skill_context &context) {
        while (true) {
            std::string skill_name;
            int param = 0;
            std::string input;
            std::getline(std::cin, input);
            std::vector<std::string> tokens;
            for (auto sub : input | std::views::split(' ')) {
                tokens.emplace_back(sub.begin(), sub.end());
            }
            if (tokens.size() != 2) {
                std::cout << "Wrong format." << '\n';
                continue;
            }
            skill_name = tokens[0];
            if (!to_int(tokens[1], param)) {
                std::cout << "Wrong format." << '\n';
                continue;
            }
            context.skill_name_ = skill_name + tokens[1];
            context.param_ = param;
            auto res = check_skill(com, context);
            switch (res) {
            case blacksmith_core::domain::check_result::INVALID:
                std::cout << "Invalid." << '\n';
                continue;
            case blacksmith_core::domain::check_result::REJECTED:
                std::cout << "Rejected." << '\n';
                continue;
            case blacksmith_core::domain::check_result::SUCCESS:
                std::cout << "Succeed." << '\n';
                return;
            }
        }
    };
    while (true) {
        get_context(player, player_context);
        get_context(enemy, enemy_context);
        declare(player, player_context, enemy, enemy_context);
        judge(player, enemy);
        print_info(player, enemy);
    }
    return 0;
}
