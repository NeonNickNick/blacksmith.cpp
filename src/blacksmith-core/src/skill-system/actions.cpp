#include "skill-system/skill-sets.hpp"
#include "domain/models.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace blacksmith::skill_system {
skill_action parse_skill_action(std::string_view input) {
    std::istringstream stream{std::string(input)};
    std::vector<std::string> tokens;
    for (std::string token; stream >> token;)
        tokens.push_back(std::move(token));
    // Preserve the existing shorthand for a single zero-parameter skill.
    if (tokens.size() == 1)
        tokens.emplace_back("0");
    if (tokens.empty() || tokens.size() % 2 != 0)
        throw std::invalid_argument("Use skill param [skill param ...].");

    skill_action result{};
    auto *tail = &result;
    const auto &mapping = get_string_skill_mapping();
    for (size_t i = 0; i < tokens.size(); i += 2) {
        auto &name = tokens[i];
        std::ranges::transform(name, name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        const auto found = std::ranges::find_if(mapping, [&name](const auto &entry) {
            return entry.first == name;
        });
        if (found == mapping.end())
            throw std::invalid_argument("Unknown skill name.");
        const auto &param = tokens[i + 1];
        int value = 0;
        const auto [end, error] =
            std::from_chars(param.data(), param.data() + param.size(), value);
        if (error != std::errc{} || end != param.data() + param.size() || value < 0)
            throw std::invalid_argument("Parameters must be nonnegative integers.");
        if (i != 0) {
            tail->next_ = std::make_unique<skill_action>();
            tail = tail->next_.get();
        }
        tail->skill_ = found->second;
        tail->param_ = value;
    }
    return result;
}

std::string format_skill_action(const skill_action &action) {
    std::string result;
    const auto &mapping = get_string_skill_mapping();
    for (auto *node = &action; node; node = node->next_.get()) {
        const auto found = std::ranges::find_if(mapping, [node](const auto &entry) {
            return entry.second == node->skill_;
        });
        if (found == mapping.end())
            throw std::invalid_argument("Unknown skill ID.");
        if (!result.empty())
            result += ' ';
        result += found->first + " " + std::to_string(node->param_);
    }
    return result;
}
} // namespace blacksmith::skill_system
