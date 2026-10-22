#include "bdsl.hpp"
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <corecrt_io.h>
#include <fcntl.h>
#endif

namespace {
std::filesystem::path from_utf8(const std::string &value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
std::string to_utf8(const std::filesystem::path &value) {
    const auto encoded = value.u8string();
    return {reinterpret_cast<const char *>(encoded.data()), encoded.size()};
}
const char *help = R"(bdslc 1.0.0 - Blacksmith DSL compiler
Usage:
  bdslc input.bdsl [-o output.cpp]
  bdslc --check [--json] input.bdsl
  bdslc --format [--indent 4 | --tabs] input.bdsl [-o formatted.bdsl]
Use '-' as input to read UTF-8 stdin. With stdin, output defaults to stdout.
--check --json outputs {"diagnostics":[...]}; exit: 0 valid, 1 BDSL error, 2 I/O or usage error.
Formatting preserves comments and always writes to stdout unless -o is given.
)";
std::string read(const std::string &path) {
    if (path == "-")
        return {std::istreambuf_iterator<char>(std::cin), {}};
    std::ifstream in(from_utf8(path), std::ios::binary);
    if (!in)
        throw std::runtime_error("Cannot read input: " + path);
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::string &path, const std::string &value) {
    if (path.empty() || path == "-") {
        std::cout << value;
        return;
    }
    std::ofstream out(from_utf8(path), std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("Cannot write output: " + path);
    out << value;
    if (!out)
        throw std::runtime_error("Failed writing output: " + path);
}
int run(const std::vector<std::string> &args) {
    bool check = false, format = false, json = false, tabs = false;
    unsigned indent = 4;
    std::string input, output;
    try {
        for (std::size_t i = 1; i < args.size(); ++i) {
            const auto &arg = args[i];
            if (arg == "--help" || arg == "-h") {
                std::cout << help;
                return 0;
            }
            if (arg == "--version") {
                std::cout << "bdslc 1.0.0\n";
                return 0;
            }
            if (arg == "--check")
                check = true;
            else if (arg == "--format")
                format = true;
            else if (arg == "--json")
                json = true;
            else if (arg == "--tabs")
                tabs = true;
            else if (arg == "-o" || arg == "--output" || arg == "--indent") {
                if (++i == args.size())
                    throw std::runtime_error("Missing value after " + arg);
                if (arg == "--indent") {
                    const auto &n = args[i];
                    unsigned value = 0;
                    const auto [end, error] = std::from_chars(n.data(), n.data() + n.size(), value);
                    if (error != std::errc{} || end != n.data() + n.size() || value < 1 || value > 16)
                        throw std::runtime_error("Indent must be in [1, 16]");
                    indent = value;
                } else {
                    if (!output.empty())
                        throw std::runtime_error("Only one output is allowed");
                    output = args[i];
                }
            } else if (arg != "-" && !arg.empty() && arg[0] == '-')
                throw std::runtime_error("Unknown option: " + arg);
            else {
                if (!input.empty())
                    throw std::runtime_error(
                        "Exactly one input file is required");
                input = arg;
            }
        }
        if (input.empty())
            throw std::runtime_error("Input file is required; see --help");
        if ((check && format) || (json && !check) || (check && !output.empty()))
            throw std::runtime_error(
                "Invalid combination of --check, --format, --json or -o");
        const auto source = read(input);
        // Never truncate the source, even via a symlink or a hard link.
        if (!check && !format && output.empty() && input != "-") {
            auto p = from_utf8(input);
            p.replace_extension(".cpp");
            output = to_utf8(p);
        }
        if (!format && !check && input != "-" && !output.empty() &&
            output != "-") {
            const auto in = from_utf8(input), out = from_utf8(output);
            if (std::filesystem::absolute(in).lexically_normal() ==
                    std::filesystem::absolute(out).lexically_normal() ||
                (std::filesystem::exists(out) &&
                 std::filesystem::equivalent(in, out)))
                throw std::runtime_error(
                    "C++ output must not overwrite the BDSL input");
        }
        if (format)
            write(output, bdsl::format(source, indent, tabs));
        else {
            const auto cpp = bdsl::compile(source);
            if (check) {
                if (json)
                    std::cout << "{\"diagnostics\":[]}\n";
            } else
                write(output, cpp);
        }
        return 0;
    } catch (const bdsl::Error &e) {
        if (json)
            std::cout << "{\"diagnostics\":["
                      << bdsl::diagnostic_json(e.diagnostic) << "]}\n";
        else
            std::cerr << input << ':' << e.diagnostic.line << ':'
                      << e.diagnostic.column << ": error: " << e.what() << '\n';
        return 1;
    } catch (const std::exception &e) {
        if (json)
            std::cout << "{\"error\":" << bdsl::json_string(e.what()) << "}\n";
        else
            std::cerr << "bdslc: " << e.what() << '\n';
        return 2;
    }
}
} // namespace

#ifdef _WIN32
// C++20 has no portable API for Windows Unicode argv or binary stdin/stdout.
// Keep this adapter at the boundary; parsing, formatting and file I/O use std::.
int wmain(int argc, wchar_t **argv) {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
        args.push_back(to_utf8(std::filesystem::path(argv[i])));
    return run(args);
}
#else
int main(int argc, char **argv) { return run({argv, argv + argc}); }
#endif
