#include "bdsl.hpp"
#include "catalog.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <ios>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace bdsl {
namespace {
enum class Kind { Word, Number, String, Keyword, Tag, Variable, Symbol, Comment, End };
struct Token {
    Kind kind;
    std::string text, raw;
    std::size_t line, column, length;
};
[[noreturn]] void fail(const Token& t, const std::string& message) {
    throw Error({t.line, t.column, std::max(std::size_t{1}, t.length), message});
}
bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool identifier(const std::string& s) {
    return !s.empty() && alpha(s[0]) && std::all_of(s.begin(), s.end(), [](char c) { return alpha(c) || digit(c); });
}
bool skill_identifier(const std::string& s) {
    // Preserve UTF-8 skill identifiers (e.g. Chinese), rejecting ASCII token separators.
    const auto letter = [](unsigned char c) { return alpha(static_cast<char>(c)) || c >= 0x80; };
    return !s.empty() && letter(static_cast<unsigned char>(s[0])) &&
           std::all_of(s.begin(), s.end(), [&](unsigned char c) { return letter(c) || digit(static_cast<char>(c)); });
}

// charconv keeps BDSL numbers independent of the process's C/C++ locale.
std::string decimal(double value, std::chars_format format = std::chars_format::general,
                    int precision = std::numeric_limits<double>::max_digits10) {
    std::array<char, std::numeric_limits<double>::max_exponent10 + 32> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, format, precision);
    if (error != std::errc{}) throw std::runtime_error("Cannot represent number");
    return {buffer.data(), end};
}

class Lexer {
    const std::string& source;
    std::size_t pos = 0, line = 1, column = 1;
    char peek(std::size_t n = 0) const { return pos + n < source.size() ? source[pos + n] : '\0'; }
    bool tag() const {
        if (peek() != '<' || !alpha(peek(1))) return false;
        std::size_t n = 2;
        while (alpha(peek(n)) || digit(peek(n))) ++n;
        return peek(n) == '>';
    }
    void advance() {
        const auto c = static_cast<unsigned char>(source[pos++]);
        if (c == '\n') { ++line; column = 1; }
        else if ((c & 0xc0) != 0x80) { column += c >= 0xf0 ? 2 : 1; }
    }
public:
    explicit Lexer(const std::string& value) : source(value) {}
    std::vector<Token> run(bool comments) {
        std::vector<Token> result;
        if (source.compare(0, 3, "\xef\xbb\xbf") == 0) pos = 3;
        while (pos < source.size()) {
            if (std::isspace(static_cast<unsigned char>(peek()))) { advance(); continue; }
            const auto start = pos, start_line = line, start_column = column;
            Token t{Kind::Symbol, "", "", start_line, start_column, 1};
            auto finish = [&] {
                t.raw = source.substr(start, pos - start);
                t.length = line == start_line ? column - start_column : 1;
                if (t.text.empty()) t.text = t.raw;
                if (comments || t.kind != Kind::Comment) result.push_back(t);
            };
            if (peek() == '/' && (peek(1) == '/' || peek(1) == '*')) {
                const bool block = peek(1) == '*';
                t.kind = Kind::Comment;
                advance(); advance();
                if (!block) { while (pos < source.size() && peek() != '\n' && peek() != '\r') advance(); }
                else {
                    while (pos < source.size() && !(peek() == '*' && peek(1) == '/')) advance();
                    if (pos == source.size()) fail(t, "Unterminated block comment");
                    advance(); advance();
                }
            } else if (peek() == '"') {
                t.kind = Kind::String;
                advance();
                while (pos < source.size() && peek() != '"') {
                    if (peek() == '\n' || peek() == '\r') fail(t, "Newline in string literal");
                    if (peek() == '\\') {
                        advance();
                        switch (peek()) {
                        case 'n': t.text += '\n'; break;
                        case 'r': t.text += '\r'; break;
                        case 't': t.text += '\t'; break;
                        case '"': t.text += '"'; break;
                        case '\\': t.text += '\\'; break;
                        default: fail(t, "Only \\n, \\r, \\t, \\\" and \\\\ escapes are supported");
                        }
                        advance();
                    } else { t.text += peek(); advance(); }
                }
                if (pos == source.size()) fail(t, "Unterminated string literal");
                advance();
                // Empty decoded strings must stay empty.
                t.raw = source.substr(start, pos - start);
                t.length = column - start_column;
                result.push_back(t);
                continue;
            } else if (peek() == '[' || peek() == '@' || tag()) {
                const char open = peek(), close = open == '[' ? ']' : open == '@' ? '@' : '>';
                t.kind = open == '[' ? Kind::Keyword : open == '@' ? Kind::Variable : Kind::Tag;
                advance();
                if (!alpha(peek())) fail(t, "Expected an identifier beginning with a letter or '_'");
                while (alpha(peek()) || digit(peek())) { t.text += peek(); advance(); }
                if (t.text.empty() || peek() != close) fail(t, std::string("Expected '") + close + "' after identifier");
                advance();
            } else if (alpha(peek())) {
                t.kind = Kind::Word;
                while (alpha(peek()) || digit(peek())) advance();
            } else if (digit(peek()) || (peek() == '.' && digit(peek(1)))) {
                t.kind = Kind::Number;
                while (digit(peek())) advance();
                if (peek() == '.') { advance(); while (digit(peek())) advance(); }
                if (peek() == 'e' || peek() == 'E') {
                    advance();
                    if (peek() == '+' || peek() == '-') advance();
                    if (!digit(peek())) fail(t, "Expected exponent digits");
                    while (digit(peek())) advance();
                }
            } else {
                const std::string two = source.substr(pos, 2);
                if (two == "->" || two == ">=" || two == "<=" || two == "==" || two == "!=" || two == "&&" || two == "||") {
                    advance(); advance();
                } else if (std::string("(){}+-*/%^!<>,").find(peek()) != std::string::npos) advance();
                else fail(t, "Unexpected character (BDSL does not use semicolons)");
            }
            finish();
        }
        result.push_back({Kind::End, "<end>", "", line, column, 1});
        return result;
    }
};

enum class Type { Number, Boolean };
struct Expr {
    std::string code;
    Type type = Type::Number;
    std::optional<double> constant;
};
struct Value { Token token; Expr expr; std::string word; };
using Fields = std::map<std::string, Value>;
struct Skill {
    std::string name, id, check, use, body, repeats;
    bool passive = false, parameterized = false, check_uses_player = false;
};

class Parser {
    std::vector<Token> tokens;
    std::size_t at = 0;
    unsigned serial = 0, nesting = 0;
    bool passive = false;
    bool uses_parameter = false, uses_player = false;
    std::string current_skill, repeat_definitions;
    unsigned repeat_serial = 0;
    std::vector<std::string> indices;
    std::map<std::string, std::string> variables;
    std::set<std::string> referenced_variables;
    const Token& peek() const { return tokens.at(at); }
    Token pop() { const auto t = tokens.at(at); if (t.kind != Kind::End) ++at; return t; }
    bool is(const std::string& s) const { return peek().text == s; }
    bool eat(const std::string& s) { if (peek().kind == Kind::Symbol && is(s)) { ++at; return true; } return false; }
    Token expect(const std::string& s) {
        if (peek().kind != Kind::Symbol || !is(s)) fail(peek(), "Expected '" + s + "', found '" + peek().raw + "'");
        return pop();
    }
    Token kind(Kind k, const std::string& description) {
        if (peek().kind != k) fail(peek(), "Expected " + description);
        return pop();
    }
    Token keyword(const std::string& s) {
        if (peek().kind != Kind::Keyword || !is(s)) fail(peek(), "Expected [" + s + "]");
        return pop();
    }
    void block() { expect("->"); expect("{"); }
    void type(const Expr& e, Type expected, const Token& t) const {
        if (e.type != expected) fail(t, expected == Type::Number ? "Expected a numeric expression" : "Expected a boolean expression");
    }
    std::string fresh(const std::string& s) { return "bdsl_" + s + "_" + std::to_string(serial++); }
    static int precedence(const std::string& s) {
        if (s == "||") return 1;
        if (s == "&&") return 2;
        if (s == "==" || s == "!=") return 3;
        if (s == "<" || s == ">" || s == "<=" || s == ">=") return 4;
        if (s == "+" || s == "-") return 5;
        if (s == "*" || s == "/" || s == "%") return 6;
        if (s == "^") return 8;
        return 0;
    }
    Expr expression(int minimum = 1) {
        if (++nesting > 128) fail(peek(), "Expression/block nesting exceeds 128");
        const Token t = pop();
        Expr left;
        if (t.kind == Kind::Number) {
            double n = 0;
            const auto [end, error] = std::from_chars(t.text.data(), t.text.data() + t.text.size(), n);
            if (error != std::errc{} || end != t.text.data() + t.text.size()) fail(t, "Number is out of range");
            if (!std::isfinite(n)) fail(t, "Number must be finite");
            auto literal = decimal(n);
            if (literal.find_first_of(".eE") == std::string::npos) literal += ".0";
            left = {literal, Type::Number, n};
        } else if (t.kind == Kind::Word && (t.text == "true" || t.text == "false")) {
            left = {t.text, Type::Boolean, t.text == "true" ? 1.0 : 0.0};
        } else if (t.kind == Kind::Variable) {
            const auto it = variables.find(t.text);
            if (it == variables.end()) fail(t, "Unknown mark variable @" + t.text + "@");
            left.code = it->second;
            referenced_variables.insert(it->second);
        } else if (t.text == "(" && t.kind == Kind::Symbol) {
            left = expression(); expect(")");
        } else if (t.text == "-" || t.text == "+" || t.text == "!") {
            left = expression(7);
            type(left, t.text == "!" ? Type::Boolean : Type::Number, t);
            left.code = "(" + t.text + left.code + ")";
            if (left.constant) left.constant = t.text == "!" ? !*left.constant : t.text == "-" ? -*left.constant : *left.constant;
        } else if (t.kind == Kind::Word && t.text == "__N__") {
            if (passive) fail(t, "A passive skill has no __N__");
            uses_parameter = true;
            left.code = "static_cast<double>(N)";
        } else if (t.kind == Kind::Word && t.text == "__index__") {
            if (indices.empty()) fail(t, "__index__ is only valid inside <repeat>");
            left.code = "static_cast<double>(" + indices.back() + ")";
        } else if (t.kind == Kind::Word && resources.count(t.text)) {
            uses_player = true;
            if (t.text == "HP" || t.text == "MHP") left.code = "static_cast<double>(player.focus_.health_." + std::string(t.text == "HP" ? "hp_" : "mhp_") + ")";
            else {
                const std::string query = t.text == "GoldIron" ? "query_gold" : t.text == "CommonIron" ? "query_common" : "query";
                left.code = "static_cast<double>(player.focus_.resource_." + query + "(resource_type::" + resources.at(t.text) + "))";
            }
        } else if (t.kind == Kind::Word && (t.text == "count" || t.text == "min" || t.text == "max" || t.text == "abs" || t.text == "floor" || t.text == "ceil")) {
            expect("(");
            if (t.text == "count") {
                const Token id = kind(Kind::Word, "a mark enum name");
                if (!marks.count(id.text)) fail(id, "Unknown mark enum: " + id.text);
                uses_player = true;
                left.code = "static_cast<double>(count_mark<mark_id::" + id.text + ">(player))";
            } else {
                auto a = expression(); type(a, Type::Number, t);
                left = {"std::" + t.text + "(" + a.code, Type::Number, {}};
                if (t.text == "min" || t.text == "max") {
                    expect(","); auto b = expression(); type(b, Type::Number, t);
                    left.code += ", " + b.code;
                    if (a.constant && b.constant) left.constant = t.text == "min" ? std::min(*a.constant, *b.constant) : std::max(*a.constant, *b.constant);
                } else if (a.constant) {
                    left.constant = t.text == "abs" ? std::abs(*a.constant) : t.text == "floor" ? std::floor(*a.constant) : std::ceil(*a.constant);
                }
                left.code += ")";
            }
            expect(")");
        } else fail(t, "Expected expression; unknown name '" + t.text + "'");

        while (peek().kind == Kind::Symbol && precedence(peek().text) >= minimum) {
            const Token op = pop();
            auto right = expression(precedence(op.text) + (op.text == "^" ? 0 : 1));
            const bool logical = op.text == "&&" || op.text == "||";
            const bool equality = op.text == "==" || op.text == "!=";
            const bool compare = precedence(op.text) <= 4;
            if (equality) { if (left.type != right.type) fail(op, "Equality operands must have the same type"); }
            else { type(left, logical ? Type::Boolean : Type::Number, op); type(right, logical ? Type::Boolean : Type::Number, op); }
            if ((op.text == "/" || op.text == "%") && right.constant && *right.constant == 0) fail(op, "Division by zero");
            std::optional<double> c;
            if (left.constant && right.constant) {
                const double a = *left.constant, b = *right.constant;
                if (op.text == "+") c = a + b; else if (op.text == "-") c = a - b;
                else if (op.text == "*") c = a * b; else if (op.text == "/") c = a / b;
                else if (op.text == "%") c = std::fmod(a, b); else if (op.text == "^") c = std::pow(a, b);
                else if (op.text == "==") c = a == b; else if (op.text == "!=") c = a != b;
                else if (op.text == ">=") c = a >= b; else if (op.text == "<=") c = a <= b;
                else if (op.text == ">") c = a > b; else if (op.text == "<") c = a < b;
                else if (op.text == "&&") c = a && b; else if (op.text == "||") c = a || b;
                if (c && !std::isfinite(*c)) fail(op, "Constant expression is out of range");
            }
            left = {op.text == "^" ? "std::pow(" + left.code + ", " + right.code + ")" : op.text == "%" ? "std::fmod(" + left.code + ", " + right.code + ")" : "(" + left.code + " " + op.text + " " + right.code + ")", compare ? Type::Boolean : Type::Number, c};
        }
        --nesting;
        return left;
    }
    using Spec = std::map<std::string, char>;
    Fields fields(const Spec& spec) {
        Fields result;
        expect("(");
        while (!is(")")) {
            const auto t = kind(Kind::Keyword, "a named parameter such as [power]");
            const auto it = spec.find(t.text);
            if (it == spec.end()) fail(t, "Unknown parameter [" + t.text + "]");
            if (result.count(t.text)) fail(t, "Duplicate parameter [" + t.text + "]");
            Value value{t, {}, ""};
            if (it->second == 'w') value.word = kind(Kind::Word, "an enum name").text;
            else if (it->second == 's') value.word = kind(Kind::String, "a string").text;
            else if (it->second == 'c') {
                auto inner = fields({{"delay", 'n'}, {"remain", 'n'}, {"isinfinite", 'b'}});
                value.word = clock(inner);
            } else {
                value.expr = expression();
                type(value.expr, it->second == 'b' ? Type::Boolean : Type::Number, t);
            }
            result.emplace(t.text, std::move(value));
        }
        expect(")");
        return result;
    }
    const Value& required(const Fields& f, const std::string& key, const Token& t) const {
        const auto it = f.find(key);
        if (it == f.end()) fail(t, "Missing parameter [" + key + "]");
        return it->second;
    }
    std::string expr(const Fields& f, const std::string& key, const std::string& fallback) const {
        return f.count(key) ? f.at(key).expr.code : fallback;
    }
    std::string clock(const Fields& f) const {
        return "clap_round_clock{.delayed_rounds_ = static_cast<int>(" + expr(f, "delay", "0") + "), .remaining_rounds_ = static_cast<int>(" + expr(f, "remain", "1") + "), .is_infinite_ = " + expr(f, "isinfinite", "false") + "}";
    }
    std::string resource(const Token& t, bool health = true) const {
        if (!resources.count(t.text) || (!health && (t.text == "HP" || t.text == "MHP" || t.text == "CommonIron"))) fail(t, "Invalid resource type: " + t.text);
        return "resource_type::" + resources.at(t.text);
    }
    std::string condition() {
        const auto t = peek(); auto e = expression(); type(e, Type::Boolean, t); return e.code;
    }
    std::string checks() {
        expect("("); const bool invert = eat("!"); expect(")"); block();
        std::string result;
        while (!is("}")) {
            if (peek().kind != Kind::Tag || !is("require")) fail(peek(), "Only <require> is allowed in [check]");
            pop();
            if (!result.empty()) result += " && ";
            result += "(" + condition() + ")";
        }
        expect("}");
        if (result.empty()) return invert ? "false" : "NOTHING";
        return invert ? "!(" + result + ")" : result;
    }
    static std::string indent(const std::string& body) {
        std::istringstream in(body); std::string line, out;
        while (std::getline(in, line)) out += "    " + line + "\n";
        return out;
    }
    std::string statements(std::string* leading_uses = nullptr) {
        if (++nesting > 128) fail(peek(), "Expression/block nesting exceeds 128");
        std::string out;
        bool in_uses = leading_uses != nullptr;
        while (!is("}")) {
            const Token t = kind(Kind::Tag, "a declaration statement such as <attack>");
            if (t.text != "use") in_uses = false;
            if (t.text == "use" || t.text == "recover") {
                auto amount = expression(); type(amount, Type::Number, t);
                if (t.text == "recover") out += "write_recovery(static_cast<int>(" + amount.code + "), player);\n";
                else {
                    const auto r = kind(Kind::Word, "a resource type");
                    out += "use_resource<" + resource(r) + (r.text == "CommonIron" ? ", true" : "") + ">(static_cast<float>(" + amount.code + "), player);\n";
                }
            } else if (t.text == "takemark" || t.text == "countmark") {
                const auto var = kind(Kind::Variable, "a variable such as @bonus@");
                if (variables.count(var.text)) fail(var, "Duplicate variable @" + var.text + "@");
                const auto mark = kind(Kind::Word, "a mark enum name");
                if (!marks.count(mark.text)) fail(mark, "Unknown mark enum: " + mark.text);
                const auto name = fresh("mark"); variables[var.text] = name;
                out += "[[maybe_unused]] const double " + name + " = " + (t.text == "takemark" ? "take_mark" : "count_mark") + "<mark_id::" + mark.text + ">(player);\n";
            } else if (t.text == "attack") {
                auto f = fields({{"type", 'w'}, {"power", 'n'}, {"delay", 'n'}, {"remain", 'n'}, {"isinfinite", 'b'}, {"ap", 'n'}, {"interrupt", 'b'}});
                const auto& attack = required(f, "type", t);
                if (!attacks.count(attack.word)) fail(attack.token, "Unknown attack type: " + attack.word);
                const auto power = required(f, "power", t).expr.code;
                // dsl-macro.hpp defines PHYSICAL/MAGICAL/REAL as qualified enum aliases.
                const auto call = "write_attack<" + attacks.at(attack.word) + ">(static_cast<int>(" + power + "), player)";
                if (f.size() == 2) { out += call + ";\n"; continue; }
                const auto name = fresh("attack");
                out += "auto& " + name + " = " + call + ";\n";
                // Retain delays already added by context preprocessors.
                if (f.count("delay")) out += name + ".clock_.delayed_rounds_ += static_cast<int>(" + f.at("delay").expr.code + ");\n";
                if (f.count("remain")) out += name + ".clock_.remaining_rounds_ = static_cast<int>(" + f.at("remain").expr.code + ");\n";
                if (f.count("isinfinite")) out += name + ".clock_.is_infinite_ = " + f.at("isinfinite").expr.code + ";\n";
                if (f.count("ap")) out += name + ".ap_factor_ = static_cast<float>(" + f.at("ap").expr.code + ");\n";
                if (f.count("interrupt")) out += name + ".interrupt_ = " + f.at("interrupt").expr.code + ";\n";
            } else if (t.text == "defense") {
                auto f = fields({{"type", 'w'}, {"power", 'n'}, {"delay", 'n'}, {"clock", 'c'}, {"analyzer", 'w'}});
                const auto& defense = required(f, "type", t);
                if (!defenses.count(defense.word)) fail(defense.token, "Unsupported defense type: " + defense.word);
                const auto d = defenses.at(defense.word);
                if (f.count("analyzer") && f.at("analyzer").word != "DefaultReduction") fail(f.at("analyzer").token, "Only DefaultReduction is supported; omit [analyzer] to select by type");
                if (f.count("analyzer") && d.defender != "default_reduction") fail(f.at("analyzer").token, "DefaultReduction does not match this defense type");
                const auto call = "write_defense<>(defense_entity{.type_ = defense_type::" + d.id + ", .power_ = static_cast<int>(" + required(f, "power", t).expr.code + ")" + (f.count("clock") ? ", .clock_ = " + f.at("clock").word : "") + ", .defender_ = " + d.defender + ", .update_ = " + d.update + "}, player)";
                if (!f.count("delay")) { out += call + ";\n"; continue; }
                const auto name = fresh("defense");
                out += "auto& " + name + " = " + call + ";\n";
                out += name + ".clock_.delayed_rounds_ = static_cast<int>(" + f.at("delay").expr.code + ");\n";
            } else if (t.text == "resource") {
                auto f = fields({{"type", 'w'}, {"power", 'n'}, {"delay", 'n'}, {"remain", 'n'}, {"isinfinite", 'b'}});
                const auto& r = required(f, "type", t);
                Token rt = r.token; rt.text = r.word;
                const auto call = "write_resource<" + resource(rt, false) + ">(static_cast<float>(" + required(f, "power", t).expr.code + "), player)";
                if (f.size() == 2) { out += call + ";\n"; continue; }
                const auto name = fresh("resource");
                out += "auto& " + name + " = " + call + ";\n";
                out += name + ".clock_ = " + clock(f) + ";\n";
            } else if (t.text == "addmark") {
                auto f = fields({{"id", 'w'}, {"clock", 'c'}});
                const auto& id = required(f, "id", t);
                if (!marks.count(id.word)) fail(id.token, "Unknown mark enum: " + id.word);
                out += "player.focus_.mark_.add(mark_entity{.clock_ = " + (f.count("clock") ? f.at("clock").word : "clap_round_clock{.is_infinite_ = true}") + ", .id_ = mark_id::" + id.word + "});\n";
            } else if (t.text == "equip") {
                expect("("); const auto name = kind(Kind::String, "a skill set name"); expect(")");
                if (!skill_sets.count(name.text)) fail(name, "Unknown core skill set: " + name.text);
                out += "write_skill_set<&get_" + name.text + ">(player);\n";
            } else if (t.text == "disable") {
                const auto name = kind(Kind::Word, "a skill enum name");
                if (!skills.count(name.text)) fail(name, "Unknown skill enum: " + name.text);
                out += "player.focus_.skills_.disable_skill(skill::" + name.text + ");\n";
            } else if (t.text == "effect") {
                auto f = fields({{"type", 'w'}, {"resource", 'w'}, {"target", 'w'}, {"delay", 'n'}, {"clock", 'c'}});
                const auto& effect = required(f, "type", t);
                if (effect.word != "EraseResource") fail(effect.token, "Only the EraseResource effect is supported");
                const auto& r = required(f, "resource", t); Token rt = r.token; rt.text = r.word;
                const auto resource_name = resource(rt, false);
                const auto& target = required(f, "target", t);
                if (target.word != "Enemy" && target.word != "Self") fail(target.token, "Target must be Enemy or Self");
                // Effect callbacks receive their owner as player, regardless of target.
                const auto call = "write_effect<effect_target_type::" + std::string(target.word == "Enemy" ? "ENEMY" : "SELF") + ", effect_entity{.clock_ = {}, .take_effect_ = [](community& player, community& /*enemy*/, effect_entity&) { std::erase_if(player.focus_.turn_context_.resource_context_.datas_, [](const resource_data& data) { return data.type_ == " + resource_name + "; }); }}>(player)";
                if (f.size() == 3) { out += call + ";\n"; continue; }
                const auto name = fresh("effect");
                out += "auto& " + name + " = " + call + ";\n";
                if (f.count("delay")) out += name + ".clock_.delayed_rounds_ = static_cast<int>(" + f.at("delay").expr.code + ");\n";
                if (f.count("clock")) out += name + ".effect_.clock_ = " + f.at("clock").word + ";\n";
            } else if (t.text == "repeat") {
                expect("(");
                const auto count_token = peek(); auto count = expression(); type(count, Type::Number, count_token);
                if (!count.constant || std::floor(*count.constant) != *count.constant) fail(count_token, "Repeat count must be a constant integer");
                expect(")"); block();
                const auto helper = current_skill + "_repeat_" + std::to_string(repeat_serial++);
                const auto saved_variables = variables;
                const auto saved_references = referenced_variables;
                const bool saved_parameter = uses_parameter;
                referenced_variables.clear(); uses_parameter = false;
                // REPEAT and REPEAT_BATCH expose their compile-time iteration as 'index'.
                indices.push_back("index");
                const auto body = statements();
                indices.pop_back();
                const bool batch = uses_parameter;
                std::vector<std::string> captures;
                for (const auto& [name, symbol] : saved_variables) {
                    (void)name;
                    if (referenced_variables.count(symbol)) captures.push_back(symbol);
                }
                variables = saved_variables;
                referenced_variables.insert(saved_references.begin(), saved_references.end());
                uses_parameter = saved_parameter || batch;
                const auto size = decimal(*count.constant, std::chars_format::fixed, 0);
                const std::string macro = batch ? "REPEAT_BATCH" : "REPEAT";
                const std::string args = batch ? "N, player" : "player";
                if (captures.empty()) {
                    const std::string dsl = body.empty() ? "DSL_()" : std::string(batch ? "DSL_BATCH(\n" : "DSL(\n") + indent(indent(body)) + "    )";
                    repeat_definitions += macro + "(" + helper + ",\n    " + dsl + ")\n\n";
                    out += helper + "<" + size + ">(" + args + ");\n";
                } else {
                    // The fixed macro signatures take only N/player. A per-call value object
                    // keeps outer mark values in scope without global or thread-local state.
                    const auto closure = helper + "_closure";
                    std::string definition = "struct " + closure + " {\n", values;
                    for (const auto& symbol : captures) {
                        definition += "    double " + symbol + ";\n";
                        if (!values.empty()) values += ", ";
                        values += symbol;
                    }
                    definition += "    " + macro + "(run,\n        ([this" + std::string(batch ? ", N" : "") + "](community& player) {\n" + indent(indent(indent(body))) + "        }(p)))\n};\n\n";
                    repeat_definitions += definition;
                    out += closure + "{" + values + "}.run<" + size + ">(" + args + ");\n";
                }
            } else if (t.text == "if") {
                expect("(");
                const auto saved = variables;
                const auto head = "if (" + condition() + ")";
                expect(")"); block();
                out += head + " {\n" + indent(statements()) + "}\n";
                variables = saved;
                if (peek().kind == Kind::Tag && is("else")) {
                    pop(); block(); out += "else {\n" + indent(statements()) + "}\n";
                    variables = saved;
                }
            } else fail(t, "Unknown declaration statement <" + t.text + ">");
            if (in_uses) { *leading_uses += out; out.clear(); }
        }
        expect("}"); --nesting;
        return out;
    }
    Skill skill() {
        keyword("skill");
        Skill result;
        if (tokens.at(std::min(at + 1, tokens.size() - 1)).kind == Kind::String) {
            expect("("); result.name = pop().text; expect(")");
        } else {
            const auto t = peek(); auto f = fields({{"name", 's'}, {"id", 'w'}});
            result.name = required(f, "name", t).word;
            if (f.count("id")) result.id = f.at("id").word;
        }
        result.passive = result.name == "auto";
        if (!skill_identifier(result.name) || result.name[0] == '_' || result.name.find("__") != std::string::npos) fail(peek(), "Skill name must be a C++ identifier without leading '_' or '__'");
        if (result.name == "passive" || result.name == "BEGIN_SKILL_SET" || result.name == "NOTHING" ||
            result.name == "PHYSICAL" || result.name == "MAGICAL" || result.name == "REAL" ||
            (result.name.rfind("R_", 0) == 0 && std::any_of(resources.begin(), resources.end(), [&](const auto& entry) { return result.name == "R_" + entry.second; }))) fail(peek(), "Skill name conflicts with a core macro or the reserved passive name");
        if (result.passive && !result.id.empty()) fail(peek(), "A passive skill has no enum id");
        if (!result.passive && result.id.empty()) {
            std::string normalized;
            for (unsigned char c : result.name) if (c != '_') normalized += static_cast<char>(std::toupper(c));
            for (const auto& name : skills) {
                std::string candidate;
                for (char c : name) if (c != '_') candidate += c;
                if (candidate == normalized) { result.id = name; break; }
            }
        }
        if (!result.passive && !skills.count(result.id)) fail(peek(), "Unknown skill enum; use [skill]([name]\"alias\" [id]MAGIC_ATTACK)");
        passive = result.passive; variables.clear(); indices.clear(); referenced_variables.clear();
        current_skill = passive ? "passive" : result.name;
        repeat_serial = 0; repeat_definitions.clear();
        block();
        bool has_check = false, has_declare = false;
        while (!eat("}")) {
            if (peek().kind != Kind::Keyword) fail(peek(), "Expected [check] or [declare]");
            const auto t = pop();
            uses_parameter = false; uses_player = false;
            if (t.text == "check") {
                if (has_check || passive) fail(t, passive ? "A passive skill cannot contain [check]" : "Duplicate [check] block");
                has_check = true;
                // Check scopes never see declaration variables, independent of block order.
                const auto saved = variables; variables.clear(); result.check = checks(); variables = saved;
                result.check_uses_player = uses_player;
            } else if (t.text == "declare") {
                if (has_declare) fail(t, "Duplicate [declare] block");
                has_declare = true; expect("("); expect(")"); block(); result.body = statements(&result.use);
            } else fail(t, "Expected [check] or [declare]");
            result.parameterized = result.parameterized || uses_parameter;
        }
        if (!has_declare || (!passive && !has_check)) fail(peek(), "Each active skill needs one [check] and one [declare]; auto needs [declare]");
        result.repeats = std::move(repeat_definitions);
        return result;
    }
public:
    explicit Parser(const std::string& source) : tokens(Lexer(source).run(false)) {}
    std::string run() {
        keyword("skillset"); expect("(");
        const auto name = kind(Kind::String, "a C++ skill set identifier string"); expect(")");
        // The set name becomes get_<name>; skill names are passed directly to REGISTER macros.
        if (!identifier(name.text) || name.text[0] == '_' || name.text.find("__") != std::string::npos) fail(name, "Skill set name must be an ASCII identifier without leading '_' or '__'");
        block();
        std::vector<Skill> all;
        std::set<std::string> names, ids;
        while (!eat("}")) {
            const auto t = peek(); auto s = skill();
            if (!names.insert(s.name).second) fail(t, "Duplicate skill name: " + s.name);
            if (!s.passive && !ids.insert(s.id).second) fail(t, "Duplicate skill enum: " + s.id);
            all.push_back(std::move(s));
        }
        kind(Kind::End, "end of file (only one skill package is allowed)");
        std::string out = "// Generated by bdslc 1.0.0. Do not edit.\n#include <algorithm>\n#include <cmath>\n#include <domain/models.hpp>\n#include <domain/transformations.hpp>\n#include <skill-system/skill-sets.hpp>\n#include <skill-system/dsl-macro.hpp>\n\nBEGIN_SKILL_SET\n\n";
        for (const auto& s : all) {
            out += s.repeats;
            const std::string require = s.parameterized ? "REQUIRE_BATCH" : s.check_uses_player ? "REQUIRE" : "REQUIRE_";
            const auto dsl = [&](const std::string& body) {
                return body.empty() ? std::string("DSL_()") : std::string(s.parameterized ? "DSL_BATCH(\n" : "DSL(\n") + indent(indent(body)) + "    )";
            };
            if (s.passive) out += "PASSIVE_(REQUIRE_(NOTHING),\n    " + dsl(s.use + s.body) + ")\n\n";
            else {
                const std::string macro = s.parameterized ? "REGISTER_BATCH" : s.check_uses_player ? "REGISTER" : "REGISTER_";
                out += macro + "(" + s.name + ", skill::" + s.id + ",\n    " + require + "(" + s.check + "),\n    " + dsl(s.use) + ",\n    " + dsl(s.body) + ")\n\n";
            }
        }
        return out + "END(" + name.text + ")\n";
    }
};
} // namespace

std::string compile(const std::string& source) { return Parser(source).run(); }

std::string format(const std::string& source, unsigned width, bool tabs) {
    if (width < 1 || width > 16) throw std::invalid_argument("Indent must be in [1, 16]");
    (void)compile(source);
    const auto tokens = Lexer(source).run(true);
    std::string out, line;
    unsigned depth = 0;
    auto flush = [&] {
        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (!line.empty()) { out += std::string(depth * (tabs ? 1 : width), tabs ? '\t' : ' ') + line + "\n"; line.clear(); }
    };
    auto space = [&] { if (!line.empty() && line.back() != ' ' && line.back() != '(') line += ' '; };
    for (const auto& t : tokens) {
        if (t.kind == Kind::End) break;
        if (t.kind == Kind::Comment) {
            flush();
            for (std::size_t i = 0; i < t.raw.size(); ++i) {
                if (t.raw[i] == '\r') {
                    if (i + 1 < t.raw.size() && t.raw[i + 1] == '\n') ++i;
                    line += '\n';
                } else line += t.raw[i];
            }
            flush();
        }
        else if (t.text == "{" && t.kind == Kind::Symbol) { space(); line += '{'; flush(); ++depth; }
        else if (t.text == "}" && t.kind == Kind::Symbol) { flush(); --depth; line = "}"; flush(); }
        else if (t.kind == Kind::Tag || (t.kind == Kind::Keyword && (t.text == "skillset" || t.text == "skill" || t.text == "check" || t.text == "declare"))) { flush(); line = t.raw; }
        else if (t.text == "(" && t.kind == Kind::Symbol) { line += '('; }
        else if (t.text == ")" && t.kind == Kind::Symbol) { while (!line.empty() && line.back() == ' ') line.pop_back(); line += ')'; }
        else if (t.text == "," && t.kind == Kind::Symbol) { line += ", "; }
        else { space(); line += t.raw; }
    }
    flush(); return out;
}

std::string json_string(const std::string& value) {
    std::ostringstream out; out << '"';
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
            else out << c;
        }
    }
    out << '"'; return out.str();
}

std::string diagnostic_json(const Diagnostic& d) {
    return "{\"line\":" + std::to_string(d.line) + ",\"column\":" + std::to_string(d.column) + ",\"length\":" + std::to_string(d.length) + ",\"severity\":\"error\",\"message\":" + json_string(d.message) + "}";
}
} // namespace bdsl
