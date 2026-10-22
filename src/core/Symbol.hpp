#pragma once

#include <compare>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace qure {

class Object;
struct SymbolData;

/// Intrusive node that attaches something to a symbol: a [receive], a named
/// table, a [catch~] and so on. Pd keeps this in `s_thing`; we keep a typed,
/// doubly linked list per symbol so binding and unbinding are O(1) and never
/// allocate. Only the engine thread mutates bindings.
struct Binding {
    enum class Kind : unsigned char {
        Receiver,   ///< receives messages sent to the name
        Table,      ///< owner implements Table (array storage)
        SignalSend, ///< [send~]
        Catch,      ///< [catch~]
        DelayLine,  ///< [delwrite~]
    };

    explicit Binding(Kind k, Object* o) : kind(k), owner(o) {}
    Binding(const Binding&) = delete;
    Binding& operator=(const Binding&) = delete;

    Kind kind;
    Object* owner;
    SymbolData* bound = nullptr;
    Binding* prev = nullptr;
    Binding* next = nullptr;
};

/// Per-symbol storage. Lives forever once interned.
struct SymbolData {
    std::string name;
    Binding* bindings = nullptr; ///< engine thread only
    double value = 0.0;          ///< storage for [value]; engine thread only
};

/// An interned string. Comparing and hashing are pointer operations, exactly
/// like Pd's `t_symbol*`. A default-constructed Symbol is the empty symbol.
class Symbol {
public:
    Symbol() noexcept;
    explicit Symbol(std::string_view text);

    [[nodiscard]] std::string_view str() const noexcept { return data_->name; }
    [[nodiscard]] const char* c_str() const noexcept { return data_->name.c_str(); }
    [[nodiscard]] bool empty() const noexcept { return data_->name.empty(); }
    [[nodiscard]] SymbolData* data() const noexcept { return data_; }
    /// Wraps already-interned data (no lookup, no lock). Null means "".
    [[nodiscard]] static Symbol fromData(SymbolData* d) noexcept { return d ? Symbol(d) : Symbol(); }

    friend bool operator==(Symbol a, Symbol b) noexcept { return a.data_ == b.data_; }
    friend std::strong_ordering operator<=>(Symbol a, Symbol b) noexcept {
        return std::compare_three_way{}(a.data_, b.data_);
    }

    // ---- bindings (engine thread only) ----
    void bind(Binding& b) const noexcept;
    static void unbind(Binding& b) noexcept;
    [[nodiscard]] Binding* firstBinding() const noexcept { return data_->bindings; }
    /// First binding of the given kind, or nullptr.
    [[nodiscard]] Binding* find(Binding::Kind kind) const noexcept;

private:
    explicit Symbol(SymbolData* d) noexcept : data_(d) {}
    SymbolData* data_;
};

/// Frequently used selectors, interned once.
namespace sym {
Symbol bang();
Symbol float_();
Symbol symbol();
Symbol list();
Symbol set();
Symbol empty();
Symbol pd();
Symbol dsp();
} // namespace sym

} // namespace qure

template <>
struct std::hash<qure::Symbol> {
    std::size_t operator()(qure::Symbol s) const noexcept {
        return std::hash<const void*>{}(s.data());
    }
};
