#include "Symbol.hpp"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace qure {
namespace {

// Heterogeneous lookup lets us probe with a string_view without allocating.
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

class SymbolTable {
public:
    static SymbolTable& instance() {
        static SymbolTable table;
        return table;
    }

    SymbolData* intern(std::string_view text) {
        std::scoped_lock lock(mutex_);
        if (auto it = table_.find(text); it != table_.end())
            return it->second.get();
        auto data = std::make_unique<SymbolData>();
        data->name = std::string(text);
        auto* raw = data.get();
        table_.emplace(raw->name, std::move(data));
        return raw;
    }

    SymbolData* emptySymbol() {
        static SymbolData* e = intern("");
        return e;
    }

private:
    std::mutex mutex_;
    // Keys view into the owned SymbolData, which never moves.
    std::unordered_map<std::string_view, std::unique_ptr<SymbolData>, StringHash, std::equal_to<>> table_;
};

} // namespace

Symbol::Symbol() noexcept : data_(SymbolTable::instance().emptySymbol()) {}

Symbol::Symbol(std::string_view text) : data_(SymbolTable::instance().intern(text)) {}

void Symbol::bind(Binding& b) const noexcept {
    if (b.bound)
        unbind(b);
    b.bound = data_;
    b.prev = nullptr;
    b.next = data_->bindings;
    if (b.next)
        b.next->prev = &b;
    data_->bindings = &b;
}

void Symbol::unbind(Binding& b) noexcept {
    if (!b.bound)
        return;
    if (b.prev)
        b.prev->next = b.next;
    else
        b.bound->bindings = b.next;
    if (b.next)
        b.next->prev = b.prev;
    b.bound = nullptr;
    b.prev = b.next = nullptr;
}

Binding* Symbol::find(Binding::Kind kind) const noexcept {
    for (auto* b = data_->bindings; b; b = b->next)
        if (b->kind == kind)
            return b;
    return nullptr;
}

namespace sym {
#define QURE_SYMBOL(fn, text)          \
    Symbol fn() {                      \
        static const Symbol s{text};   \
        return s;                      \
    }
QURE_SYMBOL(bang, "bang")
QURE_SYMBOL(float_, "float")
QURE_SYMBOL(symbol, "symbol")
QURE_SYMBOL(list, "list")
QURE_SYMBOL(set, "set")
QURE_SYMBOL(empty, "empty")
QURE_SYMBOL(pd, "pd")
QURE_SYMBOL(dsp, "dsp")
#undef QURE_SYMBOL
} // namespace sym

} // namespace qure
