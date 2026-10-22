#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace bdsl {
struct Diagnostic {
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t length = 1;
    std::string message;
};

class Error : public std::runtime_error {
public:
    Diagnostic diagnostic;
    explicit Error(Diagnostic value)
        : std::runtime_error(value.message), diagnostic(std::move(value)) {}
};

// Both functions perform the same lexical, syntax and semantic checks.
std::string compile(const std::string& source);
std::string format(const std::string& source, unsigned indent = 4, bool tabs = false);
std::string json_string(const std::string& value);
std::string diagnostic_json(const Diagnostic& value);
} // namespace bdsl
