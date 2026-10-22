#include "Atom.hpp"
#include "PdText.hpp"

#include <charconv>
#include <format>
#include <optional>

namespace qure {

Symbol Atom::asSymbol() const noexcept {
    if (type_ == Type::Symbol || type_ == Type::DollarSymbol)
        return Symbol::fromData(s_);
    return {};
}

bool operator==(const Atom& a, const Atom& b) noexcept {
    if (a.type_ != b.type_)
        return false;
    switch (a.type_) {
    case Atom::Type::Float:
    case Atom::Type::Dollar: return a.f_ == b.f_;
    case Atom::Type::Symbol:
    case Atom::Type::DollarSymbol: return a.s_ == b.s_;
    case Atom::Type::Semi:
    case Atom::Type::Comma: return true;
    }
    return false;
}

std::string formatFloat(double f) {
    // Pd prints with "%g"; integers up to 2^53 are printed in full so that
    // double-precision counters stay readable.
    if (f == static_cast<double>(static_cast<long long>(f)) && f > -9.007199254740992e15 && f < 9.007199254740992e15)
        return std::format("{}", static_cast<long long>(f));
    return std::format("{:g}", f);
}

std::string Atom::toString() const {
    return atomsToString(AtomSpan(this, 1));
}

namespace {

bool isNumber(std::string_view s) {
    std::size_t i = 0;
    const auto n = s.size();
    if (i < n && (s[i] == '+' || s[i] == '-'))
        ++i;
    std::size_t digits = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9')
        ++i, ++digits;
    if (i < n && s[i] == '.') {
        ++i;
        while (i < n && s[i] >= '0' && s[i] <= '9')
            ++i, ++digits;
    }
    if (digits == 0)
        return false;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < n && (s[i] == '+' || s[i] == '-'))
            ++i;
        std::size_t expDigits = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9')
            ++i, ++expDigits;
        if (expDigits == 0)
            return false;
    }
    return i == n;
}

double toDouble(std::string_view s) {
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    double v = 0.0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/// Returns the index n if `s` is exactly "$" followed by digits.
std::optional<int> plainDollar(std::string_view s) {
    if (s.size() < 2 || s[0] != '$')
        return std::nullopt;
    int value = 0;
    for (char c : s.substr(1)) {
        if (c < '0' || c > '9')
            return std::nullopt;
        value = value * 10 + (c - '0');
    }
    return value;
}

bool containsDollarDigit(std::string_view s) {
    for (std::size_t i = 0; i + 1 < s.size(); ++i)
        if (s[i] == '$' && s[i + 1] >= '0' && s[i + 1] <= '9')
            return true;
    return false;
}

void appendEscaped(std::string& out, std::string_view s, bool escapeDollar) {
    for (char c : s) {
        if (c == ' ' || c == ';' || c == ',' || c == '\\' || (escapeDollar && c == '$') || c == '\t' || c == '\n')
            out += '\\';
        out += c;
    }
}

} // namespace

void tokenize(std::string_view text, const std::function<void(const Token&)>& emit) {
    std::size_t i = 0;
    const auto n = text.size();
    std::string buf;
    while (i < n) {
        const char c = text[i];
        if (isWhitespace(c)) {
            ++i;
            continue;
        }
        if (c == ';' || c == ',') {
            emit(Token{c == ';' ? Atom::semi() : Atom::comma(), true});
            ++i;
            continue;
        }
        buf.clear();
        bool escaped = false;      // some character other than '$' was escaped
        bool onlyEscapedSep = true; // token is exactly "\;" or "\,"
        while (i < n) {
            char d = text[i];
            if (d == '\\' && i + 1 < n) {
                const char e = text[i + 1];
                if (e != '$')
                    escaped = true;
                if (!(e == ';' || e == ','))
                    onlyEscapedSep = false;
                buf += e;
                i += 2;
                continue;
            }
            if (isWhitespace(d) || d == ';' || d == ',')
                break;
            onlyEscapedSep = false;
            buf += d;
            ++i;
        }
        if (onlyEscapedSep && buf.size() == 1) {
            emit(Token{buf[0] == ';' ? Atom::semi() : Atom::comma(), false});
        } else if (auto idx = plainDollar(buf)) {
            emit(Token{Atom::dollar(*idx), false});
        } else if (containsDollarDigit(buf)) {
            emit(Token{Atom::dollarSymbol(Symbol(buf)), false});
        } else if (!escaped && isNumber(buf)) {
            emit(Token{Atom(toDouble(buf)), false});
        } else {
            emit(Token{Atom(Symbol(buf)), false});
        }
    }
}

AtomVector parseAtoms(std::string_view text) {
    AtomVector out;
    tokenize(text, [&](const Token& t) { out.push_back(t.atom); });
    return out;
}

std::string atomsToString(AtomSpan atoms, bool semicolonNewlines) {
    return formatAtoms(atoms, semicolonNewlines ? TextStyle::DisplayMultiline : TextStyle::Display);
}

std::string formatAtoms(AtomSpan atoms, TextStyle style) {
    std::string out;
    const bool file = style == TextStyle::File;
    bool newline = false;
    for (std::size_t i = 0; i < atoms.size(); ++i) {
        const Atom& a = atoms[i];
        const bool sep = a.type() == Atom::Type::Semi || a.type() == Atom::Type::Comma;
        if (i > 0 && !newline && (file || !sep))
            out += ' ';
        newline = false;
        switch (a.type()) {
        case Atom::Type::Float: out += formatFloat(a.asFloat()); break;
        case Atom::Type::Symbol: appendEscaped(out, a.asSymbol().str(), true); break;
        case Atom::Type::DollarSymbol: appendEscaped(out, a.asSymbol().str(), file); break;
        case Atom::Type::Dollar:
            out += file ? "\\$" : "$";
            out += std::to_string(a.dollarIndex());
            break;
        case Atom::Type::Semi:
            out += file ? "\\;" : ";";
            if (style == TextStyle::DisplayMultiline && i + 1 < atoms.size()) {
                out += '\n';
                newline = true;
            }
            break;
        case Atom::Type::Comma: out += file ? "\\," : ","; break;
        }
    }
    return out;
}

Symbol expandDollarSymbol(Symbol pattern, int dollarZero, AtomSpan args) {
    const auto s = pattern.str();
    std::string out;
    out.reserve(s.size() + 8);
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '$' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') {
            int idx = 0;
            std::size_t j = i + 1;
            while (j < s.size() && s[j] >= '0' && s[j] <= '9')
                idx = idx * 10 + (s[j++] - '0');
            if (idx == 0)
                out += std::to_string(dollarZero);
            else if (idx <= static_cast<int>(args.size()))
                out += args[idx - 1].isFloat() ? formatFloat(args[idx - 1].asFloat())
                                               : std::string(args[idx - 1].asSymbol().str());
            else
                out += '0';
            i = j - 1;
        } else {
            out += s[i];
        }
    }
    return Symbol(out);
}

AtomVector expandDollars(AtomSpan atoms, int dollarZero, AtomSpan args) {
    AtomVector out;
    out.reserve(atoms.size());
    for (const auto& a : atoms) {
        switch (a.type()) {
        case Atom::Type::Dollar: {
            const int idx = a.dollarIndex();
            if (idx == 0)
                out.emplace_back(static_cast<double>(dollarZero));
            else if (idx <= static_cast<int>(args.size()))
                out.push_back(args[idx - 1]);
            else
                out.emplace_back(0.0);
            break;
        }
        case Atom::Type::DollarSymbol: out.emplace_back(expandDollarSymbol(a.asSymbol(), dollarZero, args)); break;
        default: out.push_back(a); break;
        }
    }
    return out;
}

} // namespace qure
