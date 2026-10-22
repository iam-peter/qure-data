// Control-rate math: binary operators [+ - * / pow max min mod div % == !=
// > < >= <= && || & | << >> log], unary functions [abs sqrt exp sin cos tan
// atan wrap mtof ftom dbtorms rmstodb powtodb dbtopow], [atan2] [clip]
// [random] [makefilename] and [expr].

#include "Common.hpp"

#include <charconv>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace qure::objects {
namespace {

// ---------------------------------------------------------------------------
// Helpers

/// C's `(int)f` without the undefined behaviour for NaN and out-of-range
/// values: truncates toward zero and saturates.
int toInt(double f) noexcept {
    if (std::isnan(f))
        return 0;
    if (f >= static_cast<double>(INT_MAX))
        return INT_MAX;
    if (f <= static_cast<double>(INT_MIN))
        return INT_MIN;
    return static_cast<int>(f);
}

/// Shift with x86 semantics for the count (masked to 5 bits), so that
/// out-of-range counts are defined.
int shiftLeft(int a, int n) noexcept {
    return static_cast<int>(static_cast<std::uint32_t>(a) << (static_cast<unsigned>(n) & 31u));
}
int shiftRight(int a, int n) noexcept { return a >> (static_cast<unsigned>(n) & 31u); }

/// Seeds for [random] and expr's random(), like Pd's makeseed(). Only
/// called from constructors (control thread).
unsigned makeSeed() {
    static unsigned next = 1489853723u;
    next = next * 435898247u + 938284287u;
    return next & 0x7fffffffu;
}

constexpr double kLogTen = 2.302585092994046;

double mtof(double f) {
    if (f <= -1500)
        return 0;
    if (f > 1499)
        f = 1499;
    return 8.17579891564 * std::exp(.0577622650 * f);
}
double ftom(double f) { return f > 0 ? 17.3123405046 * std::log(.12231220585 * f) : -1500; }
double powtodb(double f) {
    if (f <= 0)
        return 0;
    const double v = 100 + 10. / kLogTen * std::log(f);
    return v < 0 ? 0 : v;
}
double rmstodb(double f) {
    if (f <= 0)
        return 0;
    const double v = 100 + 20. / kLogTen * std::log(f);
    return v < 0 ? 0 : v;
}
double dbtopow(double f) {
    if (f <= 0)
        return 0;
    return std::exp((kLogTen * 0.1) * (std::min(f, 870.0) - 100.));
}
double dbtorms(double f) {
    if (f <= 0)
        return 0;
    return std::exp((kLogTen * 0.05) * (std::min(f, 485.0) - 100.));
}

// ---------------------------------------------------------------------------
// Binary operators: the left inlet is hot, the right one stores the second
// operand (also settable by the creation argument).

using BinFn = double (*)(double, double);

namespace binops {
double plus(double a, double b) { return a + b; }
double minus(double a, double b) { return a - b; }
double times(double a, double b) { return a * b; }
double over(double a, double b) { return b != 0 ? a / b : 0; }
double power(double a, double b) {
    if ((a == 0 && b < 0) || (a < 0 && b != std::trunc(b)))
        return 0;
    return std::pow(a, b);
}
double max(double a, double b) { return a > b ? a : b; }
double min(double a, double b) { return a < b ? a : b; }
double eq(double a, double b) { return a == b; }
double ne(double a, double b) { return a != b; }
double gt(double a, double b) { return a > b; }
double lt(double a, double b) { return a < b; }
double ge(double a, double b) { return a >= b; }
double le(double a, double b) { return a <= b; }
double bitAnd(double a, double b) { return toInt(a) & toInt(b); }
double bitOr(double a, double b) { return toInt(a) | toInt(b); }
double logAnd(double a, double b) { return toInt(a) && toInt(b); }
double logOr(double a, double b) { return toInt(a) || toInt(b); }
double shl(double a, double b) { return shiftLeft(toInt(a), toInt(b)); }
double shr(double a, double b) { return shiftRight(toInt(a), toInt(b)); }
// [%]: C remainder; a zero divisor counts as 1.
double rem(double a, double b) {
    const long long n2 = toInt(b);
    return static_cast<double>(static_cast<long long>(toInt(a)) % (n2 ? n2 : 1));
}
// [mod]: result is always in [0, |b|).
double mod(double a, double b) {
    long long n2 = toInt(b);
    if (n2 < 0)
        n2 = -n2;
    else if (n2 == 0)
        n2 = 1;
    long long r = static_cast<long long>(toInt(a)) % n2;
    if (r < 0)
        r += n2;
    return static_cast<double>(r);
}
// [div]: integer division rounding toward minus infinity.
double div(double a, double b) {
    long long n1 = toInt(a), n2 = toInt(b);
    if (n2 < 0)
        n2 = -n2;
    else if (n2 == 0)
        n2 = 1;
    if (n1 < 0)
        n1 -= n2 - 1;
    return static_cast<double>(n1 / n2);
}
double log(double a, double b) {
    if (a <= 0)
        return -1000;
    if (b <= 0)
        return std::log(a);
    return std::log(a) / std::log(b);
}
double atan2(double a, double b) { return a == 0 && b == 0 ? 0 : std::atan2(a, b); }
} // namespace binops

class Binop final : public Object {
public:
    Binop(const ObjectArgs& a, BinFn fn) : Object(a), fn_(fn), right_(argFloat(a.args, 0)) {
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            outFloat(0, fn_(left_, right_));
        else
            Object::onBang(inlet);
    }
    void onFloat(int inlet, double f) override {
        if (inlet == 0) {
            left_ = f;
            outFloat(0, fn_(left_, right_));
        } else {
            right_ = f;
        }
    }

private:
    BinFn fn_;
    double left_ = 0, right_;
};

// ---------------------------------------------------------------------------
// Unary functions: one inlet, float in, float out.

using UnFn = double (*)(double);

namespace unops {
double abs(double f) { return std::fabs(f); }
double sqrt(double f) { return f > 0 ? std::sqrt(f) : 0; }
double exp(double f) { return std::exp(std::min(f, 709.78)); }
double sin(double f) { return std::sin(f); }
double cos(double f) { return std::cos(f); }
double tan(double f) {
    const double c = std::cos(f);
    return c == 0 ? 0 : std::sin(f) / c;
}
double atan(double f) { return std::atan(f); }
double wrap(double f) { return f - std::floor(f); }
} // namespace unops

class Unop final : public Object {
public:
    Unop(const ObjectArgs& a, UnFn fn) : Object(a), fn_(fn) {
        addInlet();
        addOutlet();
    }

protected:
    void onFloat(int, double f) override { outFloat(0, fn_(f)); }

private:
    UnFn fn_;
};

// ---------------------------------------------------------------------------

class Clip final : public Object {
public:
    explicit Clip(const ObjectArgs& a) : Object(a), lo_(argFloat(a.args, 0)), hi_(argFloat(a.args, 1)) {
        addInlet();
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            output();
        else
            Object::onBang(inlet);
    }
    void onFloat(int inlet, double f) override {
        switch (inlet) {
        case 0:
            value_ = f;
            output();
            break;
        case 1: lo_ = f; break;
        default: hi_ = f; break;
        }
    }

private:
    // Pd's exact order of tests: with lo > hi the result is lo.
    void output() { outFloat(0, value_ < lo_ ? lo_ : (value_ > hi_ ? hi_ : value_)); }
    double value_ = 0, lo_, hi_;
};

// ---------------------------------------------------------------------------

/// Pd's linear congruential generator, shared by [random] and expr.
class Lcg {
public:
    explicit Lcg(unsigned seed) : state_(seed) {}
    void seed(double f) { state_ = static_cast<unsigned>(static_cast<long long>(f)); }
    /// Next value in [0, 1).
    double next() {
        state_ = state_ * 472940017u + 832416023u;
        return static_cast<double>(state_) * (1. / 4294967296.);
    }

private:
    unsigned state_;
};

class Random final : public Object {
public:
    explicit Random(const ObjectArgs& a)
        : Object(a), range_(argFloat(a.args, 0)), rng_(makeSeed()), seed_(Symbol("seed")) {
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet != 0) {
            Object::onBang(inlet);
            return;
        }
        const int n = toInt(range_);
        const int range = n < 1 ? 1 : n;
        int v = static_cast<int>(range * rng_.next());
        if (v >= range)
            v = range - 1;
        outFloat(0, v);
    }
    void onFloat(int inlet, double f) override {
        range_ = f;
        if (inlet == 0)
            onBang(0);
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet == 0 && selector == seed_)
            rng_.seed(argFloat(args, 0));
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    double range_;
    Lcg rng_;
    Symbol seed_;
};

// ---------------------------------------------------------------------------

/// [makefilename]: formats a number or symbol with a printf-style pattern
/// holding at most one conversion. The pattern is validated and rebuilt
/// (without length modifiers) so the call to snprintf is always safe.
/// Numbers always use '.' as the decimal point, whatever the C locale.
class MakeFilename final : public Object {
public:
    explicit MakeFilename(const ObjectArgs& a) : Object(a) {
        addInlet();
        addOutlet();
        const Symbol fmt = argSymbol(a.args, 0);
        if (!setFormat(fmt.empty() ? Symbol("file.%d") : fmt))
            throw ObjectError(std::format("makefilename: invalid format string '{}'", fmt.str()));
    }

protected:
    void onFloat(int, double f) override {
        if (!valid_) {
            error("invalid format string");
            return;
        }
        std::array<char, 1000> buf{};
        int n = 0;
        switch (kind_) {
        case Kind::None: n = std::snprintf(buf.data(), buf.size(), "%s", format_.data()); break;
        case Kind::Int: n = std::snprintf(buf.data(), buf.size(), format_.data(), toInt(f)); break;
        case Kind::Uint: {
            const double c = std::isnan(f) ? 0 : std::clamp(f, 0.0, static_cast<double>(UINT_MAX));
            n = std::snprintf(buf.data(), buf.size(), format_.data(), static_cast<unsigned>(c));
            break;
        }
        case Kind::Float:
            n = std::snprintf(buf.data(), buf.size(), format_.data(), f);
            fixDecimalPoint(buf.data(), n);
            break;
        case Kind::String: {
            std::array<char, 64> num{};
            const auto r = std::format_to_n(num.data(), num.size() - 1, "{:g}", f);
            *r.out = '\0';
            n = std::snprintf(buf.data(), buf.size(), format_.data(), num.data());
            break;
        }
        }
        emit(buf.data(), n);
    }
    void onSymbol(int, Symbol s) override {
        if (!valid_) {
            error("invalid format string");
            return;
        }
        std::array<char, 1000> buf{};
        int n = 0;
        switch (kind_) {
        case Kind::None: n = std::snprintf(buf.data(), buf.size(), "%s", format_.data()); break;
        case Kind::Int: n = std::snprintf(buf.data(), buf.size(), format_.data(), 0); break;
        case Kind::Uint: n = std::snprintf(buf.data(), buf.size(), format_.data(), 0u); break;
        case Kind::Float:
            n = std::snprintf(buf.data(), buf.size(), format_.data(), 0.0);
            fixDecimalPoint(buf.data(), n);
            break;
        case Kind::String: n = std::snprintf(buf.data(), buf.size(), format_.data(), s.c_str()); break;
        }
        emit(buf.data(), n);
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == sym::set()) {
            if (!setFormat(argSymbol(args, 0)))
                error("invalid format string '{}'", argSymbol(args, 0).str());
        } else {
            Object::onAnything(inlet, selector, args);
        }
    }

private:
    enum class Kind : unsigned char { None, Int, Uint, Float, String };

    /// Undoes a locale's decimal comma inside the converted number, which
    /// sits between the literal prefix and suffix of the output.
    void fixDecimalPoint(char* buf, int n) const {
        const auto len = static_cast<std::size_t>(std::max(n, 0));
        const std::size_t end = std::min(len > suffixLen_ ? len - suffixLen_ : 0, std::size_t{999});
        for (std::size_t i = prefixLen_; i < end; ++i)
            if (buf[i] == ',')
                buf[i] = '.';
    }

    void emit(const char* buf, int n) {
        if (n <= 0 || buf[0] == '\0')
            return;
        // Interning the result is unavoidable here (Pd does the same).
        outSymbol(0, Symbol(std::string_view(buf)));
    }

    /// Validates `fmt` and rebuilds it into format_; no allocation.
    bool setFormat(Symbol fmt) {
        valid_ = false;
        kind_ = Kind::None;
        prefixLen_ = suffixLen_ = 0;
        const std::string_view s = fmt.str();
        std::size_t out = 0;
        std::size_t* literal = &prefixLen_; // output length of literal text
        const auto put = [&](char c) {
            if (out + 1 >= format_.size())
                return false;
            format_[out++] = c;
            return true;
        };
        bool haveSpec = false;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] != '%') {
                if (!put(s[i]))
                    return false;
                ++*literal;
                continue;
            }
            if (i + 1 < s.size() && s[i + 1] == '%') {
                if (!put('%') || !put('%'))
                    return false;
                ++*literal;
                ++i;
                continue;
            }
            if (haveSpec)
                return false; // only one conversion allowed
            haveSpec = true;
            if (!put('%'))
                return false;
            std::size_t j = i + 1;
            for (; j < s.size(); ++j) {
                const char c = s[j];
                if (std::string_view("+- #.0123456789").find(c) != std::string_view::npos) {
                    if (!put(c))
                        return false;
                } else if (c == 'h' || c == 'l' || c == 'L') {
                    // length modifiers are dropped: we pass the matching type
                } else {
                    break;
                }
            }
            if (j == s.size())
                return false;
            const char conv = s[j];
            if (std::string_view("dic").find(conv) != std::string_view::npos)
                kind_ = Kind::Int;
            else if (std::string_view("ouxX").find(conv) != std::string_view::npos)
                kind_ = Kind::Uint;
            else if (std::string_view("fFgGeEaA").find(conv) != std::string_view::npos)
                kind_ = Kind::Float;
            else if (conv == 's')
                kind_ = Kind::String;
            else
                return false;
            if (!put(conv))
                return false;
            i = j;
            literal = &suffixLen_;
        }
        format_[out] = '\0';
        valid_ = true;
        return true;
    }

    std::array<char, 1000> format_{};
    std::size_t prefixLen_ = 0, suffixLen_ = 0;
    Kind kind_ = Kind::None;
    bool valid_ = false;
};

// ---------------------------------------------------------------------------
// [expr]
//
// The box text is turned back into a string, tokenized and compiled into a
// postfix program per expression (separated by `;`). Values carry Pd expr's
// int/float distinction: integer literals and $i inputs are ints, and an
// operation on two ints stays an int (so `1/2` is 0, as in Pd).

struct Value {
    double v = 0;
    bool isInt = false;
};

enum class Op : unsigned char {
    Const, Input, Neg, Not, BitNot, Add, Sub, Mul, Div, Mod, Shl, Shr,
    Lt, Le, Gt, Ge, Eq, Ne, BitAnd, BitXor, BitOr, And, Or, Call
};

enum class Fn : unsigned char {
    Min, Max, Abs, Int, Float, Rint, Sqrt, Exp, Log, Log10, Pow, Sin, Cos, Tan,
    Asin, Acos, Atan, Atan2, Sinh, Cosh, Tanh, Floor, Ceil, Fmod, Round, Trunc,
    Cbrt, Hypot, Random, If, Mtof, Ftom, Dbtorms, Rmstodb, Dbtopow, Powtodb
};

struct FnInfo {
    std::string_view name;
    Fn fn;
    int arity;
};

constexpr std::array kFunctions{
    FnInfo{"min", Fn::Min, 2},       FnInfo{"max", Fn::Max, 2},         FnInfo{"abs", Fn::Abs, 1},
    FnInfo{"int", Fn::Int, 1},       FnInfo{"float", Fn::Float, 1},     FnInfo{"rint", Fn::Rint, 1},
    FnInfo{"sqrt", Fn::Sqrt, 1},     FnInfo{"exp", Fn::Exp, 1},         FnInfo{"ln", Fn::Log, 1},
    FnInfo{"log", Fn::Log, 1},       FnInfo{"log10", Fn::Log10, 1},     FnInfo{"pow", Fn::Pow, 2},
    FnInfo{"sin", Fn::Sin, 1},       FnInfo{"cos", Fn::Cos, 1},         FnInfo{"tan", Fn::Tan, 1},
    FnInfo{"asin", Fn::Asin, 1},     FnInfo{"acos", Fn::Acos, 1},       FnInfo{"atan", Fn::Atan, 1},
    FnInfo{"atan2", Fn::Atan2, 2},   FnInfo{"sinh", Fn::Sinh, 1},       FnInfo{"cosh", Fn::Cosh, 1},
    FnInfo{"tanh", Fn::Tanh, 1},     FnInfo{"floor", Fn::Floor, 1},     FnInfo{"ceil", Fn::Ceil, 1},
    FnInfo{"fmod", Fn::Fmod, 2},     FnInfo{"round", Fn::Round, 1},     FnInfo{"trunc", Fn::Trunc, 1},
    FnInfo{"cbrt", Fn::Cbrt, 1},     FnInfo{"hypot", Fn::Hypot, 2},     FnInfo{"random", Fn::Random, 2},
    FnInfo{"if", Fn::If, 3},         FnInfo{"mtof", Fn::Mtof, 1},       FnInfo{"ftom", Fn::Ftom, 1},
    FnInfo{"dbtorms", Fn::Dbtorms, 1}, FnInfo{"rmstodb", Fn::Rmstodb, 1}, FnInfo{"dbtopow", Fn::Dbtopow, 1},
    FnInfo{"powtodb", Fn::Powtodb, 1},
};

struct Instr {
    Op op;
    Fn fn = Fn::Min; // Call
    int index = 0;   // Input: inlet; Call: number of arguments
    Value value{};   // Const
};

enum class InputKind : unsigned char { Unused, Float, Int, Symbol };

/// Tokenizer + recursive-descent compiler. Runs on the control thread.
class ExprCompiler {
public:
    explicit ExprCompiler(std::string_view text) : text_(text) {}

    /// Compiles the expressions; fills `programs`, `inputs` and the
    /// maximum stack depth any program needs.
    void compile(std::vector<std::vector<Instr>>& programs, std::array<InputKind, 9>& inputs, int& maxDepth) {
        inputs_ = &inputs;
        next();
        if (tok_.kind == Tok::End)
            fail("empty expression");
        for (;;) {
            code_ = {};
            depth_ = maxDepth_ = 0;
            parseExpr();
            programs.push_back(std::move(code_));
            maxDepth = std::max(maxDepth, maxDepth_);
            if (tok_.kind == Tok::Semi) {
                next();
                continue;
            }
            if (tok_.kind != Tok::End)
                fail(std::format("syntax error near '{}'", tok_.text));
            break;
        }
    }

private:
    enum class Tok : unsigned char { End, Number, Ident, Dollar, Op, LParen, RParen, Comma, Semi };
    struct Token {
        Tok kind = Tok::End;
        std::string text;
        double number = 0;
        bool isInt = false;
        char dollarType = 0;
        int dollarIndex = 0;
    };

    [[noreturn]] static void fail(const std::string& why) { throw ObjectError("expr: " + why); }

    static bool isDigit(char c) { return c >= '0' && c <= '9'; }
    static bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

    void next() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n'))
            ++pos_;
        tok_ = {};
        if (pos_ >= text_.size())
            return;
        const char c = text_[pos_];
        const std::size_t start = pos_;
        if (isDigit(c) || (c == '.' && pos_ + 1 < text_.size() && isDigit(text_[pos_ + 1]))) {
            bool isFloat = false;
            while (pos_ < text_.size() && isDigit(text_[pos_]))
                ++pos_;
            if (pos_ < text_.size() && text_[pos_] == '.') {
                isFloat = true;
                ++pos_;
                while (pos_ < text_.size() && isDigit(text_[pos_]))
                    ++pos_;
            }
            if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
                std::size_t p = pos_ + 1;
                if (p < text_.size() && (text_[p] == '+' || text_[p] == '-'))
                    ++p;
                if (p < text_.size() && isDigit(text_[p])) {
                    isFloat = true;
                    pos_ = p;
                    while (pos_ < text_.size() && isDigit(text_[pos_]))
                        ++pos_;
                }
            }
            tok_.kind = Tok::Number;
            tok_.text = std::string(text_.substr(start, pos_ - start));
            // from_chars is locale independent (unlike stod).
            std::from_chars(tok_.text.data(), tok_.text.data() + tok_.text.size(), tok_.number);
            tok_.isInt = !isFloat;
            return;
        }
        if (isAlpha(c)) {
            while (pos_ < text_.size() && (isAlpha(text_[pos_]) || isDigit(text_[pos_])))
                ++pos_;
            tok_.kind = Tok::Ident;
            tok_.text = std::string(text_.substr(start, pos_ - start));
            return;
        }
        if (c == '$') {
            ++pos_;
            if (pos_ >= text_.size())
                fail("bad '$'");
            const char t = static_cast<char>(text_[pos_] | 0x20); // $F1 == $f1
            if (t != 'f' && t != 'i' && t != 's')
                fail(std::format("unsupported input type '${}'", text_[pos_]));
            ++pos_;
            int idx = 0;
            const std::size_t digits = pos_;
            while (pos_ < text_.size() && isDigit(text_[pos_]))
                idx = idx * 10 + (text_[pos_++] - '0');
            if (pos_ == digits || idx < 1 || idx > 9)
                fail("inlet number must be 1..9");
            tok_.kind = Tok::Dollar;
            tok_.dollarType = t;
            tok_.dollarIndex = idx;
            tok_.text = std::string(text_.substr(start, pos_ - start));
            return;
        }
        ++pos_;
        tok_.text = std::string(1, c);
        switch (c) {
        case '(': tok_.kind = Tok::LParen; return;
        case ')': tok_.kind = Tok::RParen; return;
        case ',': tok_.kind = Tok::Comma; return;
        case ';': tok_.kind = Tok::Semi; return;
        default: break;
        }
        tok_.kind = Tok::Op;
        // Two-character operators.
        if (pos_ < text_.size()) {
            const char d = text_[pos_];
            const bool two = (c == '|' && d == '|') || (c == '&' && d == '&') || (c == '=' && d == '=') ||
                             (c == '!' && d == '=') || (c == '<' && (d == '=' || d == '<')) ||
                             (c == '>' && (d == '=' || d == '>'));
            if (two) {
                tok_.text += d;
                ++pos_;
                return;
            }
        }
        if (std::string_view("+-*/%<>!~&|^").find(c) == std::string_view::npos)
            fail(std::format("unexpected character '{}'", c));
    }

    bool isOp(std::string_view s) const { return tok_.kind == Tok::Op && tok_.text == s; }

    void emit(Instr i, int stackDelta) {
        code_.push_back(i);
        depth_ += stackDelta;
        maxDepth_ = std::max(maxDepth_, depth_);
    }
    void emitBinary(Op op) { emit({op}, -1); }

    // Precedence climbing over C's binary operator levels (lowest first).
    struct Level {
        std::array<std::pair<std::string_view, Op>, 4> ops;
        int count;
    };
    static constexpr std::array<Level, 10> kLevels{{
        {{{{"||", Op::Or}}}, 1},
        {{{{"&&", Op::And}}}, 1},
        {{{{"|", Op::BitOr}}}, 1},
        {{{{"^", Op::BitXor}}}, 1},
        {{{{"&", Op::BitAnd}}}, 1},
        {{{{"==", Op::Eq}, {"!=", Op::Ne}}}, 2},
        {{{{"<", Op::Lt}, {"<=", Op::Le}, {">", Op::Gt}, {">=", Op::Ge}}}, 4},
        {{{{"<<", Op::Shl}, {">>", Op::Shr}}}, 2},
        {{{{"+", Op::Add}, {"-", Op::Sub}}}, 2},
        {{{{"*", Op::Mul}, {"/", Op::Div}, {"%", Op::Mod}}}, 3},
    }};

    void parseExpr() { parseLevel(0); }

    void parseLevel(std::size_t level) {
        if (level == kLevels.size()) {
            parseUnary();
            return;
        }
        parseLevel(level + 1);
        for (;;) {
            const Level& l = kLevels[level];
            std::optional<Op> op;
            for (int k = 0; k < l.count; ++k)
                if (isOp(l.ops[static_cast<std::size_t>(k)].first))
                    op = l.ops[static_cast<std::size_t>(k)].second;
            if (!op)
                return;
            next();
            parseLevel(level + 1);
            emitBinary(*op);
        }
    }

    void parseUnary() {
        if (isOp("-") || isOp("!") || isOp("~") || isOp("+")) {
            const char c = tok_.text[0];
            next();
            parseUnary();
            if (c == '-')
                emit({Op::Neg}, 0);
            else if (c == '!')
                emit({Op::Not}, 0);
            else if (c == '~')
                emit({Op::BitNot}, 0);
            return;
        }
        parsePrimary();
    }

    void parsePrimary() {
        switch (tok_.kind) {
        case Tok::Number:
            emit({Op::Const, Fn::Min, 0, Value{tok_.number, tok_.isInt}}, 1);
            next();
            return;
        case Tok::Dollar: {
            const int idx = tok_.dollarIndex - 1;
            auto& kind = (*inputs_)[static_cast<std::size_t>(idx)];
            if (kind == InputKind::Unused)
                kind = tok_.dollarType == 'i' ? InputKind::Int
                       : tok_.dollarType == 's' ? InputKind::Symbol
                                                : InputKind::Float;
            emit({Op::Input, Fn::Min, idx}, 1);
            next();
            return;
        }
        case Tok::LParen:
            next();
            parseExpr();
            expect(Tok::RParen, ")");
            return;
        case Tok::Ident: {
            const std::string name = tok_.text;
            const auto it = std::ranges::find(kFunctions, std::string_view(name), &FnInfo::name);
            if (it == kFunctions.end())
                fail(std::format("unknown function '{}'", name));
            next();
            expect(Tok::LParen, "(");
            for (int k = 0; k < it->arity; ++k) {
                if (k > 0)
                    expect(Tok::Comma, ",");
                parseExpr();
            }
            expect(Tok::RParen, ")");
            emit({Op::Call, it->fn, it->arity}, 1 - it->arity);
            return;
        }
        case Tok::End: fail("unexpected end of expression");
        default: fail(std::format("syntax error near '{}'", tok_.text));
        }
    }

    void expect(Tok kind, std::string_view what) {
        if (tok_.kind != kind)
            fail(std::format("expected '{}'", what));
        next();
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    Token tok_;
    std::vector<Instr> code_;
    int depth_ = 0, maxDepth_ = 0;
    std::array<InputKind, 9>* inputs_ = nullptr;
};

class Expr final : public Object {
public:
    explicit Expr(const ObjectArgs& a) : Object(a), rng_(makeSeed()) {
        // Rebuild the text from the (dollar-expanded) atoms: `$f1` is a plain
        // symbol, `$1` has been replaced by the abstraction argument.
        std::string text;
        for (const Atom& atom : a.args) {
            if (!text.empty())
                text += ' ';
            switch (atom.type()) {
            case Atom::Type::Float: text += formatFloat(atom.asFloat()); break;
            case Atom::Type::Semi: text += ';'; break;
            case Atom::Type::Comma: text += ','; break;
            case Atom::Type::Dollar: text += std::format("${}", atom.dollarIndex()); break;
            default: text += atom.asSymbol().str(); break;
            }
        }
        inputKinds_.fill(InputKind::Unused);
        int depth = 0;
        ExprCompiler(text).compile(programs_, inputKinds_, depth);
        stack_.resize(static_cast<std::size_t>(std::max(depth, 1)));

        int inlets = 1;
        for (int i = 0; i < 9; ++i)
            if (inputKinds_[static_cast<std::size_t>(i)] != InputKind::Unused)
                inlets = i + 1;
        for (int i = 0; i < inlets; ++i)
            addInlet();
        for (std::size_t i = 0; i < programs_.size(); ++i)
            addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            evaluateAll();
        else
            Object::onBang(inlet);
    }
    void onFloat(int inlet, double f) override {
        const auto i = static_cast<std::size_t>(inlet);
        if (inputKinds_[i] == InputKind::Symbol) {
            error("inlet {}: expected a symbol", inlet + 1);
            return;
        }
        inputs_[i] = inputKinds_[i] == InputKind::Int ? std::trunc(f) : f;
        if (inlet == 0)
            evaluateAll();
    }
    void onSymbol(int inlet, Symbol) override {
        const auto i = static_cast<std::size_t>(inlet);
        if (inputKinds_[i] != InputKind::Symbol) {
            error("inlet {}: expected a number", inlet + 1);
            return;
        }
        // Accepted, but symbols have no numeric value in this expr.
        if (inlet == 0)
            evaluateAll();
    }

private:
    void evaluateAll() {
        for (std::size_t i = programs_.size(); i-- > 0;)
            outFloat(static_cast<int>(i), evaluate(programs_[i]).v);
    }

    static long long asInt(double f) noexcept {
        if (std::isnan(f))
            return 0;
        return static_cast<long long>(std::clamp(f, -9.2e18, 9.2e18));
    }

    static Value binary(Op op, Value l, Value r) noexcept {
        const bool ints = l.isInt && r.isInt;
        const auto wrap = [&](double v) { return Value{v, ints}; };
        const long long li = asInt(l.v), ri = asInt(r.v);
        switch (op) {
        case Op::Add: return wrap(l.v + r.v);
        case Op::Sub: return wrap(l.v - r.v);
        case Op::Mul: return wrap(ints ? static_cast<double>(li * ri) : l.v * r.v);
        case Op::Div:
            if (r.v == 0)
                return wrap(0);
            if (ints)
                return wrap(ri == -1 ? static_cast<double>(-li) : static_cast<double>(li / ri));
            return wrap(l.v / r.v);
        case Op::Mod: return wrap(ri == 0 || ri == -1 ? 0.0 : static_cast<double>(li % ri));
        case Op::Shl:
            return wrap(static_cast<double>(static_cast<long long>(static_cast<unsigned long long>(li)
                                                                   << (static_cast<unsigned long long>(ri) & 63u))));
        case Op::Shr: return wrap(static_cast<double>(li >> (static_cast<unsigned long long>(ri) & 63u)));
        case Op::Lt: return wrap(l.v < r.v);
        case Op::Le: return wrap(l.v <= r.v);
        case Op::Gt: return wrap(l.v > r.v);
        case Op::Ge: return wrap(l.v >= r.v);
        case Op::Eq: return wrap(l.v == r.v);
        case Op::Ne: return wrap(l.v != r.v);
        case Op::BitAnd: return wrap(static_cast<double>(li & ri));
        case Op::BitXor: return wrap(static_cast<double>(li ^ ri));
        case Op::BitOr: return wrap(static_cast<double>(li | ri));
        // Like Pd, the logical operators look at the integer parts.
        case Op::And: return wrap(li != 0 && ri != 0);
        case Op::Or: return wrap(li != 0 || ri != 0);
        default: return {};
        }
    }

    Value call(Fn fn, const Value* a) noexcept {
        const auto flt = [](double v) { return Value{v, false}; };
        switch (fn) {
        case Fn::Min: return a[0].v < a[1].v ? Value{a[0].v, a[0].isInt && a[1].isInt} : Value{a[1].v, a[0].isInt && a[1].isInt};
        case Fn::Max: return a[0].v > a[1].v ? Value{a[0].v, a[0].isInt && a[1].isInt} : Value{a[1].v, a[0].isInt && a[1].isInt};
        case Fn::Abs: return {std::fabs(a[0].v), a[0].isInt};
        case Fn::Int: return {std::trunc(a[0].v), a[0].isInt};
        case Fn::Float: return flt(a[0].v);
        case Fn::Rint: return flt(std::rint(a[0].v));
        case Fn::Sqrt: return flt(std::sqrt(a[0].v));
        case Fn::Exp: return flt(std::exp(a[0].v));
        case Fn::Log: return flt(std::log(a[0].v));
        case Fn::Log10: return flt(std::log10(a[0].v));
        case Fn::Pow: return flt(std::pow(a[0].v, a[1].v));
        case Fn::Sin: return flt(std::sin(a[0].v));
        case Fn::Cos: return flt(std::cos(a[0].v));
        case Fn::Tan: return flt(std::tan(a[0].v));
        case Fn::Asin: return flt(std::asin(a[0].v));
        case Fn::Acos: return flt(std::acos(a[0].v));
        case Fn::Atan: return flt(std::atan(a[0].v));
        case Fn::Atan2: return flt(std::atan2(a[0].v, a[1].v));
        case Fn::Sinh: return flt(std::sinh(a[0].v));
        case Fn::Cosh: return flt(std::cosh(a[0].v));
        case Fn::Tanh: return flt(std::tanh(a[0].v));
        case Fn::Floor: return flt(std::floor(a[0].v));
        case Fn::Ceil: return flt(std::ceil(a[0].v));
        case Fn::Fmod: return flt(a[1].v == 0 ? 0 : std::fmod(a[0].v, a[1].v));
        case Fn::Round: return flt(std::round(a[0].v));
        case Fn::Trunc: return flt(std::trunc(a[0].v));
        case Fn::Cbrt: return flt(std::cbrt(a[0].v));
        case Fn::Hypot: return flt(std::hypot(a[0].v, a[1].v));
        case Fn::Random: {
            // random(lo, hi): an integer in [lo, hi).
            const long long lo = asInt(a[0].v), hi = asInt(a[1].v);
            return {static_cast<double>(lo + static_cast<long long>(static_cast<double>(hi - lo) * rng_.next())), true};
        }
        case Fn::If: return a[0].v != 0 ? a[1] : a[2];
        case Fn::Mtof: return flt(mtof(a[0].v));
        case Fn::Ftom: return flt(ftom(a[0].v));
        case Fn::Dbtorms: return flt(dbtorms(a[0].v));
        case Fn::Rmstodb: return flt(rmstodb(a[0].v));
        case Fn::Dbtopow: return flt(dbtopow(a[0].v));
        case Fn::Powtodb: return flt(powtodb(a[0].v));
        }
        return {};
    }

    /// Runs a program on the preallocated stack; never allocates.
    Value evaluate(const std::vector<Instr>& code) noexcept {
        std::size_t sp = 0;
        Value* st = stack_.data();
        for (const Instr& in : code) {
            switch (in.op) {
            case Op::Const: st[sp++] = in.value; break;
            case Op::Input: {
                const auto i = static_cast<std::size_t>(in.index);
                // Symbols have no numeric value.
                st[sp++] = inputKinds_[i] == InputKind::Symbol ? Value{0, false}
                                                              : Value{inputs_[i], inputKinds_[i] == InputKind::Int};
                break;
            }
            case Op::Neg: st[sp - 1].v = -st[sp - 1].v; break;
            case Op::Not: st[sp - 1].v = st[sp - 1].v == 0 ? 1 : 0; break;
            case Op::BitNot: st[sp - 1].v = static_cast<double>(~asInt(st[sp - 1].v)); break;
            case Op::Call: {
                const auto n = static_cast<std::size_t>(in.index);
                sp -= n;
                st[sp] = call(in.fn, st + sp);
                ++sp;
                break;
            }
            default:
                --sp;
                st[sp - 1] = binary(in.op, st[sp - 1], st[sp]);
                break;
            }
        }
        return sp ? st[sp - 1] : Value{};
    }

    std::vector<std::vector<Instr>> programs_;
    std::array<InputKind, 9> inputKinds_{};
    std::array<double, 9> inputs_{};
    std::vector<Value> stack_;
    Lcg rng_;
};

} // namespace
} // namespace qure::objects

namespace qure {
using namespace objects;

void registerMathObjects(ObjectRegistry& r) {
    const auto binop = [&r](std::string_view name, BinFn fn, std::string_view help,
                            std::initializer_list<std::string_view> aliases = {}) {
        r.add(name, [fn](const ObjectArgs& a) { return std::make_unique<Binop>(a, fn); }, help, aliases);
    };
    binop("+", binops::plus, "add");
    binop("-", binops::minus, "subtract");
    binop("*", binops::times, "multiply");
    binop("/", binops::over, "divide (0 when dividing by 0)");
    binop("pow", binops::power, "raise to a power");
    binop("max", binops::max, "the larger of two numbers");
    binop("min", binops::min, "the smaller of two numbers");
    binop("mod", binops::mod, "integer modulo, always positive");
    binop("div", binops::div, "integer division rounding down");
    binop("%", binops::rem, "integer remainder (C %)");
    binop("==", binops::eq, "equal");
    binop("!=", binops::ne, "not equal");
    binop(">", binops::gt, "greater than");
    binop("<", binops::lt, "less than");
    binop(">=", binops::ge, "greater than or equal");
    binop("<=", binops::le, "less than or equal");
    binop("&&", binops::logAnd, "logical and");
    binop("||", binops::logOr, "logical or");
    binop("&", binops::bitAnd, "bitwise and");
    binop("|", binops::bitOr, "bitwise or");
    binop("<<", binops::shl, "shift left");
    binop(">>", binops::shr, "shift right");
    binop("log", binops::log, "logarithm (natural, or to the base on the right)");
    binop("atan2", binops::atan2, "arc tangent of y/x");

    const auto unop = [&r](std::string_view name, UnFn fn, std::string_view help) {
        r.add(name, [fn](const ObjectArgs& a) { return std::make_unique<Unop>(a, fn); }, help);
    };
    unop("abs", unops::abs, "absolute value");
    unop("sqrt", unops::sqrt, "square root");
    unop("exp", unops::exp, "e to the power of x");
    unop("sin", unops::sin, "sine");
    unop("cos", unops::cos, "cosine");
    unop("tan", unops::tan, "tangent");
    unop("atan", unops::atan, "arc tangent");
    unop("wrap", unops::wrap, "fractional part (wrap to 0..1)");
    unop("mtof", mtof, "MIDI pitch to frequency");
    unop("ftom", ftom, "frequency to MIDI pitch");
    unop("dbtorms", dbtorms, "decibels to RMS amplitude");
    unop("rmstodb", rmstodb, "RMS amplitude to decibels");
    unop("dbtopow", dbtopow, "decibels to power");
    unop("powtodb", powtodb, "power to decibels");

    r.add<Clip>("clip", "restrict a number to a range");
    r.add<Random>("random", "pseudo-random integer in 0..n-1");
    r.add<MakeFilename>("makefilename", "format a number or symbol into a symbol");
    r.add<Expr>("expr", "evaluate C-like expressions");
}

} // namespace qure
