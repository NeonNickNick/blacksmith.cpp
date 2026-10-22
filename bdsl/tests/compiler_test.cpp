#include "bdsl.hpp"
#include "catalog.hpp"

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <regex>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
void require(bool value, std::string_view message,
             const std::source_location location = std::source_location::current()) {
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(location.line()) + ")");
}
void contains(const std::string& text, std::string_view part,
              const std::source_location location = std::source_location::current()) {
    require(text.find(part) != std::string::npos, "Missing: " + std::string(part), location);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "Cannot read test input");
    return {std::istreambuf_iterator<char>(stream), {}};
}
std::string package(const std::string& body = "", const std::string& check = "",
                    const std::string& name = "stick") {
    return "[skillset](\"sample\")->{[skill](\"" + name + "\")->{[check]()->{" + check +
           "}[declare]()->{" + body + "}}}";
}
std::string replace(std::string text, const std::string& before, const std::string& after) {
    std::size_t at = 0;
    while ((at = text.find(before, at)) != std::string::npos) {
        text.replace(at, before.size(), after);
        at += after.size();
    }
    return text;
}
void invalid(const std::string& source, std::string_view message = "") {
    try {
        (void)bdsl::compile(source);
    } catch (const bdsl::Error& error) {
        require(error.diagnostic.line >= 1 && error.diagnostic.column >= 1 && error.diagnostic.length >= 1,
                "Invalid diagnostic position");
        contains(error.what(), message);
        return;
    }
    throw std::runtime_error("Accepted invalid BDSL: " + source);
}
void roundtrip(const std::string& source) {
    const auto cpp = bdsl::compile(source);
    for (const auto [indent, tabs] : std::array{std::pair{4u, false}, std::pair{2u, false}, std::pair{4u, true}}) {
        const auto formatted = bdsl::format(source, indent, tabs);
        require(bdsl::compile(formatted) == cpp, "Formatting changed semantics");
        require(bdsl::format(formatted, indent, tabs) == formatted, "Formatting is not idempotent");
    }
}
void examples(const std::filesystem::path& root) {
    for (const auto& entry : std::filesystem::directory_iterator(root / "examples"))
        if (entry.path().extension() == ".bdsl") roundtrip(read(entry.path()));
    roundtrip(read(root / "tests/fixtures/repeats.bdsl"));
}
void expressions() {
    (void)bdsl::compile(package("<attack>([type] Physical [power] 2^3^2 - -2^2)",
                               "<require> !(HP < 1) && (Iron >= 0.5 || Magic != 2)"));
    (void)bdsl::compile(package("<use> 0.5 CommonIron <resource>([type] GoldIron [power] .5e+1)"));
    (void)bdsl::compile(package("<repeat>(1 + 2)->{<repeat>(2)->{<attack>([type] Physical [power] __index__)}}"));
    (void)bdsl::compile(package("", "<require> GoldIron <= Iron && CommonIron >= 0"));
    (void)bdsl::compile(package("", "<require> HP<MHP && Iron>=Magic"));
    const auto source = package("<attack>([type] Magical [power] 2 * __N__)", "<require> __N__ > 0");
    contains(bdsl::compile(source), "REGISTER_BATCH(stick, skill::STICK,");
    contains(bdsl::compile(source), "static_cast<double>(N)");
    invalid(replace(source, "[skillset]", "[skillpackagedefinition]"), "Expected [skillset]");
    invalid(replace(source, "__N__", "__skillparam__"), "unknown name");
    invalid(replace(source, "__N__", "__n__"), "unknown name");
}
void rejections() {
    const std::pair<std::string, std::string_view> cases[] = {
        {"", "skillset"},
        {package() + package(), "end of file"},
        {replace(package(), "\"sample\"", "\"../evil\""), "ASCII identifier"},
        {replace(package(), "\"sample\"", "\"_reserved\""), "ASCII identifier"},
        {replace(package(), "\"sample\"", "\"foo__bar\""), "ASCII identifier"},
        {replace(package(), "(\"sample\")", "\"sample\""), "Expected '('"},
        {package("", "", "unknown"), "Unknown skill enum"},
        {package("<require> HP >= 1"), "Unknown declaration"},
        {package("", "<attack>([type] Physical [power] 1)"), "Only <require>"},
        {package("", "<require> HP + 1"), "boolean"},
        {package("", "<require> HP >= true"), "numeric"},
        {package("<attack>([power] 1)"), "Missing parameter [type]"},
        {package("<attack>([type] Physical [power] 1 [power] 2)"), "Duplicate parameter"},
        {package("<attack>([type] Physical [power] 1 [oops] 2)"), "Unknown parameter"},
        {package("<attack>([type] Unknown [power] 1)"), "Unknown attack type"},
        {package("<defense>([type] GreyHP [power] 1)"), "Unsupported defense"},
        {package("<defense>([type] CommonArmor [power] 1 [analyzer] DefaultReduction)"), "does not match"},
        {package("<resource>([type] HP [power] 1)"), "Invalid resource"},
        {package("<use> 1 Unknown"), "Invalid resource"},
        {package("<takemark> @a@ UNKNOWN"), "Unknown mark"},
        {package("<attack>([type] Physical [power] @missing@)"), "Unknown mark variable"},
        {package("<takemark> @a@ CANNON <takemark> @a@ CANNON"), "Duplicate variable"},
        {package("<if>(true)->{<takemark> @a@ CANNON}<use> @a@ Iron"), "Unknown mark variable"},
        {package("<repeat>(3)->{<countmark> @a@ CANNON}<use> @a@ Iron"), "Unknown mark variable"},
        {package("<use> __index__ Iron"), "only valid inside"},
        {package("<repeat>(__N__)->{}"), "constant integer"},
        {package("<repeat>(1.5)->{}"), "constant integer"},
        {package("<equip>(\"unknown\")"), "Unknown core skill set"},
        {package("<disable> SIZE"), "Unknown skill enum"},
        {package("<effect>([type] Bad [resource] Iron [target] Enemy)"), "Only the EraseResource"},
        {package("<effect>([type] EraseResource [resource] Iron [target] Other)"), "Target"},
        {package("<use> 1 / 0 Iron"), "Division by zero"},
        {package("<use> (-1)^0.5 Iron"), "out of range"},
        {package("<use> 1e999 Iron"), "out of range"},
        {package("<use> 1 Iron;"), "Unexpected character"},
        {package("/* unterminated"), "Unterminated block comment"},
        {replace(package(), "\"sample\"", "\"bad\\q\""), "escapes"},
        {replace(package(), "[check]()->{}", ""), "needs one [check]"},
        {replace(package(), "[declare]()->{}", ""), "needs one [check]"},
        {replace(package(), "[check]()->{}", "[check]()->{}[check]()->{}"), "Duplicate [check]"},
        {replace(package(), "[declare]()->{}", "[declare]()->{}[declare]()->{}"), "Duplicate [declare]"},
        {package("", "", "auto"), "passive skill cannot"}
    };
    for (const auto& [source, message] : cases) invalid(source, message);
    const std::string skill = "[skill](\"stick\")->{[check]()->{}[declare]()->{}}";
    invalid("[skillset](\"a\")->{" + skill + skill + "}", "Duplicate skill name");
    invalid("[skillset](\"a\")->{" + skill + replace(skill, "(\"stick\")", "([name]\"alias\" [id]STICK)") + "}",
            "Duplicate skill enum");
    invalid("[skillset](\"a\")->{[skill](\"auto\")->{[declare]()->{<recover> __N__}}}", "passive skill has no");
    for (const auto name : {"two words", "a-b", "passive", "PHYSICAL", "R_IRON", "_reserved", "name__reserved"})
        invalid(replace(package(), "(\"stick\")", "([name]\"" + std::string(name) + "\" [id]STICK)"));
    const auto source = package("<attack>([type] Physical [power] 1)");
    for (std::size_t cut = 0; cut < source.size(); ++cut) invalid(source.substr(0, cut));
}
void registrations() {
    const std::array cases{
        std::pair{package("", "<require> Iron >= 1"), "REGISTER(stick, skill::STICK,"},
        std::pair{package(), "REGISTER_(stick, skill::STICK,"},
        std::pair{package("", "<require> false"), "REQUIRE_((false))"},
        std::pair{package("", "<require> count(CANNON) > 0"), "REGISTER(stick, skill::STICK,"},
        std::pair{package("", "<require> __N__ > 0"), "REQUIRE_BATCH("},
        std::pair{package("<recover> __N__"), "REGISTER_BATCH(stick, skill::STICK,"},
        std::pair{replace(package("<recover> 1"), "(\"stick\")", "([name]\"magic_attack\" [id]MAGIC_ATTACK)"),
                  "REGISTER_(magic_attack, skill::MAGIC_ATTACK,"},
        std::pair{std::string("[skillset](\"a\")->{[skill](\"auto\")->{[declare]()->{<recover> 1}}}"),
                  "PASSIVE_(REQUIRE_(NOTHING),"}
    };
    for (const auto& [source, registration] : cases) {
        const auto cpp = bdsl::compile(source);
        contains(cpp, registration);
        for (const auto manual : {"bdsl_skill_", "get_string_skill_mapping", "get_skill_check_mapping",
                                  "get_skill_declare_mapping", "skills.push_back", "CHECK(", "DECLARE("})
            require(cpp.find(manual) == std::string::npos, "Generated manual registration");
    }
    const auto cpp = bdsl::compile(package("<use> 1 Iron <takemark> @n@ CANNON <use> @n@ Iron <recover> @n@"));
    contains(cpp, "DSL(\n        use_resource");
    require(cpp.find("take_mark<") < cpp.rfind("use_resource<"), "Declaration order changed");
    contains(bdsl::compile("[skillset](\"a\")->{[skill](\"stick\")->{[declare]()->{<recover> 1}"
                           "[check]()->{<require> HP > 0}}}"), "REGISTER(stick, skill::STICK,");
    const auto casts = bdsl::compile(package("<attack>([type] Real [power] __N__ [delay] __N__) "
        "<defense>([type] CommonArmor [power] __N__ [clock]([remain] __N__)) <recover> __N__"));
    contains(casts, "static_cast<int>(static_cast<double>(N))");
    for (const auto helper : {"bdsl_int", "clamp", "isfinite", "numeric_limits"})
        require(casts.find(helper) == std::string::npos, "Generated range check");
    for (const auto body : {"<resource>([type] Iron [power] 1e12)", "<use> -1 Iron",
                            "<attack>([type] Real [power] -1 [ap] 0 [delay] .5 [remain] 0)",
                            "<repeat>(1025)->{}", "<repeat>(-1)->{}"})
        (void)bdsl::compile(package(body));
}
void repeats() {
    struct Case { std::string source; std::string_view macro, call; };
    const Case cases[] = {
        {package("<repeat>(3)->{<attack>([type] Physical [power] __index__)}"),
         "REPEAT(stick_repeat_0,", "stick_repeat_0<3>(player);"},
        {package("<repeat>(3)->{<attack>([type] Magical [power] 2 * __N__ [delay] __index__)}"),
         "REPEAT_BATCH(stick_repeat_0,", "stick_repeat_0<3>(N, player);"},
        {package("<repeat>(1 + 2)->{<recover> 1}", "<require> __N__ > 0"),
         "REPEAT(stick_repeat_0,", "stick_repeat_0<3>(player);"},
        {package("<repeat>(2)->{<repeat>(3)->{<recover> __N__ + __index__}}"),
         "REPEAT_BATCH(stick_repeat_1,", "stick_repeat_1<3>(N, player);"},
        {package("<countmark> @stacks@ CANNON <repeat>(2)->{<recover> @stacks@}"),
         "REPEAT(run,", "stick_repeat_0_closure{bdsl_mark_0}.run<2>(player);"},
        {package("<countmark> @stacks@ CANNON <repeat>(2)->{<recover> @stacks@ + __N__}"),
         "REPEAT_BATCH(run,", "stick_repeat_0_closure{bdsl_mark_0}.run<2>(N, player);"}
    };
    for (const auto& [source, macro, call] : cases) {
        const auto cpp = bdsl::compile(source);
        contains(cpp, macro);
        contains(cpp, call);
        require(cpp.find(macro) < cpp.find("REGISTER"), "Repeat definition after registration");
        require(cpp.find("for (") == std::string::npos, "Generated runtime repeat loop");
    }
    const auto nested = bdsl::compile(cases[3].source);
    require(nested.find("REPEAT_BATCH(stick_repeat_1,") < nested.find("REPEAT_BATCH(stick_repeat_0,"),
            "Nested helper definition order changed");
}
void formatting_and_utf8() {
    const auto source = package("/* { } // \" */ <attack>([type] Physical [power] 1) // } [power]\n");
    const auto formatted = bdsl::format(source);
    contains(formatted, "/* { } // \" */");
    contains(formatted, "// } [power]");
    roundtrip(source);
    const auto crlf = "\xef\xbb\xbf" + package("/* first\r\nsecond */\r\n<recover> 1");
    roundtrip(crlf);
    require(bdsl::format(crlf).find('\r') == std::string::npos, "Formatter retained CR");
    invalid(replace(package(), "(\"stick\")", R"(([name]"a // { \" b" [id]STICK))"), "C++ identifier");
    const std::string unicode = "[skillset](\"a\")->{[skill]([name]\"中文😀\" [id]STICK)->{[check]()->{<require> unknown >= 1}[declare]()->{}}}";
    try {
        (void)bdsl::compile(unicode);
        require(false, "Expected diagnostic");
    } catch (const bdsl::Error& error) {
        // Two Chinese code points occupy six UTF-8 bytes but two UTF-16 units;
        // the emoji occupies four UTF-8 bytes but two UTF-16 units.
        require(error.diagnostic.column == unicode.find("unknown") - 6 + 1, "Diagnostic column is not UTF-16");
    }
    require(bdsl::json_string(std::string("\"\n\t\\\0\x1f", 6)) == R"("\"\n\t\\\u0000\u001f")", "JSON escaping");
}
void locale_independence() {
    struct Comma : std::numpunct<char> {
        char do_decimal_point() const override { return ','; }
        char do_thousands_sep() const override { return '.'; }
        std::string do_grouping() const override { return "\3"; }
    };
    struct Restore {
        std::locale previous = std::locale();
        ~Restore() { std::locale::global(previous); }
    } restore;
    const auto source = package("<recover> 1.5 <repeat>(1000)->{<recover> .5e1}");
    const auto expected = bdsl::compile(source);
    std::locale::global(std::locale(std::locale::classic(), new Comma));
    require(bdsl::compile(source) == expected, "Generated C++ depends on global locale");
    contains(expected, "stick_repeat_0<1000>(player)");
    contains(expected, "1.5");
}
void catalog(const std::filesystem::path& root) {
    const auto core = read(root / "../src/blacksmith-core/include/domain/models.hpp");
    const std::regex entry(R"(\b[A-Z][A-Z_0-9]*\b)");
    for (const auto& [name, expected] : std::array{std::pair{"skill", &bdsl::skills}, std::pair{"mark_id", &bdsl::marks}}) {
        std::smatch match;
        require(std::regex_search(core, match, std::regex("enum class " + std::string(name) + R"(\s*:[^{]+\{([^}]+))")),
                "Core enum missing");
        const std::string body = match[1];
        std::set<std::string> actual;
        for (auto it = std::sregex_iterator(body.begin(), body.end(), entry); it != std::sregex_iterator(); ++it)
            actual.insert(it->str());
        actual.erase("SIZE");
        require(actual == *expected, "BDSL catalog differs from core enum");
    }
}
} // namespace

int main() {
    try {
        const auto root = std::filesystem::current_path();
        examples(root);
        expressions();
        rejections();
        registrations();
        repeats();
        formatting_and_utf8();
        locale_independence();
        catalog(root);
        std::cout << "BDSL compiler regression checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
