// Control-flow objects: [bang] [float] [int] [symbol] [print] [trigger]
// [route] [select] [pack] [unpack] [spigot] [moses] [change] [swap]
// [value] [send] [receive] [loadbang] [until] [list ...] [inlet] [outlet]
// and the message box.

#include "Common.hpp"

namespace qure::objects {
namespace {

// ---------------------------------------------------------------------------

class Bang final : public Object {
public:
    explicit Bang(const ObjectArgs& a) : Object(a) {
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int) override { outBang(0); }
    void onFloat(int, double) override { outBang(0); }
    void onSymbol(int, Symbol) override { outBang(0); }
    void onList(int, AtomSpan) override { outBang(0); }
    void onAnything(int, Symbol, AtomSpan) override { outBang(0); }
};

// ---------------------------------------------------------------------------

class Float final : public Object {
public:
    Float(const ObjectArgs& a, bool integer) : Object(a), integer_(integer) {
        value_ = conv(argFloat(a.args, 0));
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            outFloat(0, value_);
    }
    void onFloat(int inlet, double f) override {
        value_ = conv(f);
        if (inlet == 0)
            outFloat(0, value_);
    }
    void onSymbol(int, Symbol) override { error("no method for symbol"); }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet == 0 && selector == sym::set())
            value_ = conv(argFloat(args, 0));
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    double conv(double f) const { return integer_ ? std::trunc(f) : f; }
    bool integer_;
    double value_ = 0;
};

// ---------------------------------------------------------------------------

class SymbolObj final : public Object {
public:
    explicit SymbolObj(const ObjectArgs& a) : Object(a), value_(argSymbol(a.args, 0)) {
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            outSymbol(0, value_);
    }
    void onSymbol(int inlet, Symbol s) override {
        value_ = s;
        if (inlet == 0)
            outSymbol(0, value_);
    }
    void onList(int inlet, AtomSpan args) override {
        if (!args.empty() && args[0].isSymbol())
            onSymbol(inlet, args[0].asSymbol());
        else
            Object::onList(inlet, args);
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet == 0 && selector == sym::set())
            value_ = argSymbol(args, 0);
        else
            onSymbol(inlet, selector); // Pd: a selector becomes the symbol
    }

private:
    Symbol value_;
};

// ---------------------------------------------------------------------------

class Print final : public Object {
public:
    explicit Print(const ObjectArgs& a) : Object(a) {
        if (argSymbol(a.args, 0).str() == "-n")
            prefix_ = {};
        else if (!a.args.empty())
            prefix_ = Symbol(a.args[0].toString());
        else
            prefix_ = Symbol("print");
        addInlet();
    }

protected:
    void onBang(int) override { print(sym::bang(), {}); }
    void onFloat(int, double f) override {
        const Atom a(f);
        print(sym::float_(), AtomSpan(&a, 1));
    }
    void onSymbol(int, Symbol s) override {
        const Atom a(s);
        print(sym::symbol(), AtomSpan(&a, 1));
    }
    void onList(int, AtomSpan args) override { print(sym::list(), args); }
    void onAnything(int, Symbol selector, AtomSpan args) override { print(selector, args); }

private:
    void print(Symbol selector, AtomSpan args) {
        TextBuffer t;
        if (!prefix_.empty()) {
            t.append(prefix_.str());
            t.append(": ");
        }
        // Pd prints a list starting with a number without the selector and
        // a plain float as just the number.
        const bool bare = (selector == sym::float_()) ||
                          (selector == sym::list() && !args.empty() && args[0].isFloat());
        if (!bare) {
            t.append(selector.str());
            if (!args.empty())
                t.append(' ');
        }
        t.appendAtoms(args);
        engine().log(LogLevel::Info, t.view());
    }

    Symbol prefix_;
};

// ---------------------------------------------------------------------------

class Trigger final : public Object {
public:
    enum class Kind : char { Bang = 'b', Float = 'f', Symbol = 's', List = 'l', Anything = 'a' };

    explicit Trigger(const ObjectArgs& a) : Object(a) {
        addInlet();
        if (a.args.empty()) {
            kinds_ = {Kind::Float, Kind::Float};
        } else {
            for (const auto& arg : a.args) {
                if (arg.isFloat()) {
                    kinds_.push_back(Kind::Float);
                    continue;
                }
                switch (arg.asSymbol().str().empty() ? 'f' : arg.asSymbol().str()[0]) {
                case 'b': kinds_.push_back(Kind::Bang); break;
                case 's': kinds_.push_back(Kind::Symbol); break;
                case 'l': kinds_.push_back(Kind::List); break;
                case 'a': kinds_.push_back(Kind::Anything); break;
                default: kinds_.push_back(Kind::Float); break;
                }
            }
        }
        for (std::size_t i = 0; i < kinds_.size(); ++i)
            addOutlet();
    }

protected:
    void onBang(int) override { fire(sym::bang(), {}); }
    void onFloat(int, double f) override {
        const Atom a(f);
        fire(sym::float_(), AtomSpan(&a, 1));
    }
    void onSymbol(int, Symbol s) override {
        const Atom a(s);
        fire(sym::symbol(), AtomSpan(&a, 1));
    }
    void onList(int, AtomSpan args) override { fire(sym::list(), args); }
    void onAnything(int, Symbol selector, AtomSpan args) override { fire(selector, args); }

private:
    void fire(Symbol selector, AtomSpan args) {
        for (int i = static_cast<int>(kinds_.size()) - 1; i >= 0; --i) {
            switch (kinds_[static_cast<std::size_t>(i)]) {
            case Kind::Bang: outBang(i); break;
            case Kind::Float:
                if (selector == sym::symbol() || (selector != sym::float_() && selector != sym::list() &&
                                                  selector != sym::bang()))
                    error("can only convert 's' to 'b' or 'a'");
                else
                    outFloat(i, args.empty() ? 0.0 : args[0].asFloat());
                break;
            case Kind::Symbol:
                if (selector == sym::symbol())
                    outSymbol(i, args.empty() ? Symbol() : args[0].asSymbol());
                else if (selector == sym::list() && !args.empty() && args[0].isSymbol())
                    outSymbol(i, args[0].asSymbol());
                else if (selector != sym::float_() && selector != sym::list() && selector != sym::bang())
                    outSymbol(i, selector);
                else
                    outSymbol(i, Symbol("float"));
                break;
            case Kind::List:
                if (selector == sym::list())
                    outList(i, args);
                else if (selector == sym::bang())
                    outList(i, {});
                else if (selector == sym::float_() || selector == sym::symbol())
                    outList(i, args);
                else {
                    list_.clear();
                    list_.emplace_back(selector);
                    list_.insert(list_.end(), args.begin(), args.end());
                    outList(i, list_);
                }
                break;
            case Kind::Anything: outAnything(i, selector, args); break;
            }
        }
    }

    std::vector<Kind> kinds_;
    AtomVector list_;
};

// ---------------------------------------------------------------------------

class Route final : public Object {
public:
    // Like Pd, the first argument decides whether keys are floats or symbols.
    explicit Route(const ObjectArgs& a) : Object(a) {
        const AtomVector def{Atom(0.0)};
        const AtomSpan args = a.args.empty() ? AtomSpan(def) : a.args;
        symbolMode_ = args.front().isSymbol();
        for (const auto& k : args)
            keys_.push_back(symbolMode_ ? Atom(k.asSymbol()) : Atom(k.asFloat()));
        addInlet();
        if (keys_.size() == 1)
            addInlet();
        for (std::size_t i = 0; i <= keys_.size(); ++i)
            addOutlet();
    }

protected:
    // Pd's [route] only has list and anything methods; bang, float and
    // symbol arrive as lists of 0 or 1 element.
    void onBang(int) override { routeList({}); }
    void onFloat(int inlet, double f) override {
        const Atom a(f);
        if (inlet == 1)
            keys_[0] = symbolMode_ ? Atom(Symbol()) : a;
        else
            routeList(AtomSpan(&a, 1));
    }
    void onSymbol(int inlet, Symbol s) override {
        const Atom a(s);
        if (inlet == 1)
            keys_[0] = symbolMode_ ? a : Atom(0.0);
        else
            routeList(AtomSpan(&a, 1));
    }
    void onList(int, AtomSpan args) override { routeList(args); }
    void onAnything(int, Symbol selector, AtomSpan args) override {
        if (symbolMode_)
            if (int i = match(selector); i >= 0) {
                if (!args.empty() && args[0].isSymbol())
                    outAnything(i, args[0].asSymbol(), args.subspan(1));
                else
                    outList(i, args);
                return;
            }
        outAnything(reject(), selector, args);
    }

private:
    int reject() const { return static_cast<int>(keys_.size()); }
    int match(Symbol s) const {
        for (std::size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i].asSymbol() == s)
                return static_cast<int>(i);
        return -1;
    }

    void routeList(AtomSpan args) {
        if (!symbolMode_) {
            if (!args.empty() && args[0].isFloat()) {
                for (std::size_t i = 0; i < keys_.size(); ++i) {
                    if (keys_[i].asFloat() != args[0].asFloat())
                        continue;
                    const int o = static_cast<int>(i);
                    if (args.size() > 1 && args[1].isSymbol())
                        outAnything(o, args[1].asSymbol(), args.subspan(2));
                    else
                        outList(o, args.subspan(1));
                    return;
                }
            }
        } else if (args.size() > 1) {
            if (int i = match(sym::list()); i >= 0) {
                if (args[0].isSymbol())
                    outAnything(i, args[0].asSymbol(), args.subspan(1));
                else
                    outList(i, args);
                return;
            }
        } else if (args.empty()) {
            if (int i = match(sym::bang()); i >= 0) {
                outBang(i);
                return;
            }
        } else if (args[0].isFloat()) {
            if (int i = match(sym::float_()); i >= 0) {
                outFloat(i, args[0].asFloat());
                return;
            }
        } else if (int i = match(sym::symbol()); i >= 0) {
            outSymbol(i, args[0].asSymbol());
            return;
        }
        outList(reject(), args);
    }

    AtomVector keys_;
    bool symbolMode_ = false;
};

// ---------------------------------------------------------------------------

class Select final : public Object {
public:
    explicit Select(const ObjectArgs& a) : Object(a), keys_(a.args.begin(), a.args.end()) {
        if (keys_.empty())
            keys_.emplace_back(0.0);
        addInlet();
        if (keys_.size() == 1)
            addInlet();
        for (std::size_t i = 0; i <= keys_.size(); ++i)
            addOutlet();
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1) {
            keys_[0] = Atom(f);
            return;
        }
        for (std::size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i].isFloat() && keys_[i].asFloat() == f) {
                outBang(static_cast<int>(i));
                return;
            }
        outFloat(static_cast<int>(keys_.size()), f);
    }
    void onSymbol(int inlet, Symbol s) override {
        if (inlet == 1) {
            keys_[0] = Atom(s);
            return;
        }
        for (std::size_t i = 0; i < keys_.size(); ++i)
            if (keys_[i].isSymbol() && keys_[i].asSymbol() == s) {
                outBang(static_cast<int>(i));
                return;
            }
        outSymbol(static_cast<int>(keys_.size()), s);
    }

private:
    AtomVector keys_;
};

// ---------------------------------------------------------------------------

class Pack final : public Object {
public:
    explicit Pack(const ObjectArgs& a) : Object(a) {
        if (a.args.empty()) {
            values_ = {Atom(0.0), Atom(0.0)};
        } else {
            for (const auto& arg : a.args) {
                if (arg.isFloat())
                    values_.push_back(arg);
                else if (arg.asSymbol().str() == "s")
                    values_.emplace_back(Symbol("symbol"));
                else
                    values_.emplace_back(0.0);
            }
        }
        for (std::size_t i = 0; i < values_.size(); ++i)
            addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            outList(0, values_);
    }
    void onFloat(int inlet, double f) override {
        if (values_[static_cast<std::size_t>(inlet)].isSymbol()) {
            error("inlet {}: expected symbol", inlet + 1);
            return;
        }
        values_[static_cast<std::size_t>(inlet)] = Atom(f);
        if (inlet == 0)
            outList(0, values_);
    }
    void onSymbol(int inlet, Symbol s) override {
        if (values_[static_cast<std::size_t>(inlet)].isFloat()) {
            error("inlet {}: expected float", inlet + 1);
            return;
        }
        values_[static_cast<std::size_t>(inlet)] = Atom(s);
        if (inlet == 0)
            outList(0, values_);
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet != 0)
            return Object::onAnything(inlet, selector, args);
        // [pack] treats "foo 1 2" like the list "foo 1 2".
        list_.clear();
        list_.emplace_back(selector);
        list_.insert(list_.end(), args.begin(), args.end());
        onList(0, list_);
    }

private:
    AtomVector values_;
    AtomVector list_;
};

// ---------------------------------------------------------------------------

class Unpack final : public Object {
public:
    explicit Unpack(const ObjectArgs& a) : Object(a) {
        addInlet();
        const auto n = a.args.empty() ? 2 : a.args.size();
        for (std::size_t i = 0; i < n; ++i)
            addOutlet();
    }

protected:
    void onBang(int) override {}
    void onFloat(int, double f) override { outFloat(0, f); }
    void onSymbol(int, Symbol s) override { outSymbol(0, s); }
    void onList(int, AtomSpan args) override {
        const auto n = std::min<std::size_t>(args.size(), static_cast<std::size_t>(numOutlets()));
        for (auto i = static_cast<int>(n) - 1; i >= 0; --i) {
            const auto& at = args[static_cast<std::size_t>(i)];
            if (at.isFloat())
                outFloat(i, at.asFloat());
            else
                outSymbol(i, at.asSymbol());
        }
    }
    void onAnything(int, Symbol selector, AtomSpan args) override {
        list_.clear();
        list_.emplace_back(selector);
        list_.insert(list_.end(), args.begin(), args.end());
        onList(0, list_);
    }

private:
    AtomVector list_;
};

// ---------------------------------------------------------------------------

class Spigot final : public Object {
public:
    explicit Spigot(const ObjectArgs& a) : Object(a), open_(argFloat(a.args, 0) != 0) {
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int) override {
        if (open_)
            outBang(0);
    }
    void onFloat(int inlet, double f) override {
        if (inlet == 1)
            open_ = f != 0;
        else if (open_)
            outFloat(0, f);
    }
    void onSymbol(int, Symbol s) override {
        if (open_)
            outSymbol(0, s);
    }
    void onList(int inlet, AtomSpan args) override {
        if (inlet == 0 && open_)
            outList(0, args);
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet == 0 && open_)
            outAnything(0, selector, args);
    }

private:
    bool open_;
};

// ---------------------------------------------------------------------------

class Moses final : public Object {
public:
    explicit Moses(const ObjectArgs& a) : Object(a), threshold_(argFloat(a.args, 0)) {
        addInlet();
        addInlet();
        addOutlet();
        addOutlet();
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1)
            threshold_ = f;
        else
            outFloat(f < threshold_ ? 0 : 1, f);
    }

private:
    double threshold_;
};

// ---------------------------------------------------------------------------

class Change final : public Object {
public:
    explicit Change(const ObjectArgs& a) : Object(a), value_(argFloat(a.args, 0)) {
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int) override { outFloat(0, value_); }
    void onFloat(int, double f) override {
        if (f != value_) {
            value_ = f;
            outFloat(0, f);
        }
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == sym::set())
            value_ = argFloat(args, 0);
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    double value_;
};

// ---------------------------------------------------------------------------

class Swap final : public Object {
public:
    explicit Swap(const ObjectArgs& a) : Object(a), right_(argFloat(a.args, 0)) {
        addInlet();
        addInlet();
        addOutlet();
        addOutlet();
    }

protected:
    void onBang(int) override {
        outFloat(1, left_);
        outFloat(0, right_);
    }
    void onFloat(int inlet, double f) override {
        if (inlet == 1) {
            right_ = f;
            return;
        }
        left_ = f;
        onBang(0);
    }

private:
    double left_ = 0, right_;
};

// ---------------------------------------------------------------------------

class Value final : public Object {
public:
    explicit Value(const ObjectArgs& a) : Object(a), name_(argSymbol(a.args, 0)) {
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int) override { outFloat(0, name_.data()->value); }
    void onFloat(int, double f) override { name_.data()->value = f; }
    void onSymbol(int inlet, Symbol s) override {
        if (inlet == 1)
            name_ = s;
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == sym::set())
            name_ = argSymbol(args, 0);
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    Symbol name_;
};

// ---------------------------------------------------------------------------

class Send final : public Object {
public:
    explicit Send(const ObjectArgs& a) : Object(a), name_(argSymbol(a.args, 0)) {
        addInlet();
        if (a.args.empty())
            addInlet(); // right inlet sets the name
    }

protected:
    void onBang(int inlet) override { send(inlet, sym::bang(), {}); }
    void onFloat(int inlet, double f) override {
        const Atom a(f);
        send(inlet, sym::float_(), AtomSpan(&a, 1));
    }
    void onSymbol(int inlet, Symbol s) override {
        if (inlet == 1) {
            name_ = s;
            return;
        }
        const Atom a(s);
        send(inlet, sym::symbol(), AtomSpan(&a, 1));
    }
    void onList(int inlet, AtomSpan args) override { send(inlet, sym::list(), args); }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override { send(inlet, selector, args); }

private:
    void send(int inlet, Symbol selector, AtomSpan args) {
        if (inlet == 0 && !name_.empty())
            engine().deliverToName(name_, selector, args);
    }
    Symbol name_;
};

// ---------------------------------------------------------------------------

class Receive final : public Object {
public:
    explicit Receive(const ObjectArgs& a) : Object(a) {
        binding_ = &makeBinding(Binding::Kind::Receiver, argSymbol(a.args, 0));
        if (a.args.empty())
            addInlet(); // "set name"
        addOutlet();
    }

    void receiveNamed(Symbol selector, AtomSpan args) override { outAnything(0, selector, args); }

protected:
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == sym::set())
            binding_->rebind(argSymbol(args, 0));
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    NameBinding* binding_;
};

// ---------------------------------------------------------------------------

class Loadbang final : public Object {
public:
    explicit Loadbang(const ObjectArgs& a) : Object(a) { addOutlet(); }
    [[nodiscard]] bool wantsLoadbang() const override { return true; }
    void loadbang() override { outBang(0); }
};

// ---------------------------------------------------------------------------

class Until final : public Object {
public:
    explicit Until(const ObjectArgs& a) : Object(a) {
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 1) {
            running_ = false;
            return;
        }
        run(kSafetyLimit, true);
    }
    void onFloat(int inlet, double f) override {
        if (inlet == 0)
            run(static_cast<long long>(f), false);
    }

private:
    static constexpr long long kSafetyLimit = 10'000'000;

    void run(long long count, bool unbounded) {
        running_ = true;
        for (long long i = 0; i < count && running_; ++i)
            outBang(0);
        if (unbounded && running_)
            error("stopped after {} iterations (missing stop bang?)", kSafetyLimit);
        running_ = false;
    }
    bool running_ = false;
};

// ---------------------------------------------------------------------------
// [list append|prepend|split|trim|length|store]

class List final : public Object {
public:
    enum class Op { Append, Prepend, Split, Trim, Length, Store };

    explicit List(const ObjectArgs& a) : Object(a) {
        const auto opName = argSymbol(a.args, 0, Symbol("append")).str();
        AtomSpan rest = a.args.empty() ? AtomSpan{} : a.args.subspan(1);
        if (!a.args.empty() && a.args[0].isFloat())
            rest = a.args; // [list 1 2] is [list append 1 2]
        if (opName == "append" || opName == "a")
            op_ = Op::Append;
        else if (opName == "prepend" || opName == "p")
            op_ = Op::Prepend;
        else if (opName == "split" || opName == "s")
            op_ = Op::Split;
        else if (opName == "trim" || opName == "t")
            op_ = Op::Trim;
        else if (opName == "length" || opName == "l")
            op_ = Op::Length;
        else if (opName == "store")
            op_ = Op::Store;
        else
            throw ObjectError("unknown list operation '" + std::string(opName) + "'");

        addInlet();
        switch (op_) {
        case Op::Append:
        case Op::Prepend:
        case Op::Store:
            addInlet();
            stored_.assign(rest.begin(), rest.end());
            addOutlet();
            if (op_ == Op::Store)
                addOutlet();
            break;
        case Op::Split:
            addInlet();
            split_ = argFloat(rest, 0);
            addOutlet();
            addOutlet();
            addOutlet();
            break;
        case Op::Trim:
        case Op::Length: addOutlet(); break;
        }
        buf_.reserve(64);
    }

protected:
    void onBang(int inlet) override { message(inlet, sym::bang(), {}); }
    void onFloat(int inlet, double f) override {
        if (op_ == Op::Split && inlet == 1) {
            split_ = f;
            return;
        }
        const Atom a(f);
        message(inlet, sym::float_(), AtomSpan(&a, 1));
    }
    void onSymbol(int inlet, Symbol s) override {
        const Atom a(s);
        message(inlet, sym::symbol(), AtomSpan(&a, 1));
    }
    void onList(int inlet, AtomSpan args) override { message(inlet, sym::list(), args); }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (op_ == Op::Store && inlet == 0) {
            if (selector.str() == "get") {
                const auto from = static_cast<std::size_t>(std::max(0.0, argFloat(args, 0)));
                const auto n = static_cast<std::size_t>(std::max(0.0, argFloat(args, 1, 1)));
                if (from + n > stored_.size()) {
                    outBang(1);
                    return;
                }
                buf_.assign(stored_.begin() + static_cast<std::ptrdiff_t>(from),
                            stored_.begin() + static_cast<std::ptrdiff_t>(from + n));
                outMessage(0, sym::list(), buf_);
                return;
            }
            if (selector.str() == "append") {
                stored_.insert(stored_.end(), args.begin(), args.end());
                return;
            }
            if (selector.str() == "prepend") {
                stored_.insert(stored_.begin(), args.begin(), args.end());
                return;
            }
        }
        message(inlet, selector, args);
    }

private:
    void message(int inlet, Symbol selector, AtomSpan args) {
        messageAsList(selector, args, in_);
        if (inlet == 1) {
            stored_ = in_;
            return;
        }
        switch (op_) {
        case Op::Append:
            buf_ = in_;
            buf_.insert(buf_.end(), stored_.begin(), stored_.end());
            outList(0, buf_);
            break;
        case Op::Prepend:
            buf_ = stored_;
            buf_.insert(buf_.end(), in_.begin(), in_.end());
            outList(0, buf_);
            break;
        case Op::Store:
            buf_ = in_;
            buf_.insert(buf_.end(), stored_.begin(), stored_.end());
            outList(0, buf_);
            break;
        case Op::Split: {
            const auto n = static_cast<std::size_t>(std::max(0.0, split_));
            if (in_.size() < n) {
                outList(2, in_);
                return;
            }
            const AtomSpan all(in_);
            outList(1, all.subspan(n));
            outList(0, all.first(n));
            break;
        }
        case Op::Trim:
            if (in_.empty())
                outBang(0);
            else if (in_[0].isSymbol())
                outAnything(0, in_[0].asSymbol(), AtomSpan(in_).subspan(1));
            else
                outList(0, in_);
            break;
        case Op::Length: outFloat(0, static_cast<double>(in_.size())); break;
        }
    }

    Op op_ = Op::Append;
    AtomVector stored_, in_, buf_;
    double split_ = 0;
};

// ---------------------------------------------------------------------------
// Subpatch inlets/outlets. They never see messages at runtime: the compiler
// routes connections through them.

class Iolet final : public Object {
public:
    Iolet(const ObjectArgs& a, ObjectRole role) : Object(a), role_(role) {
        switch (role) {
        case ObjectRole::Inlet: addOutlet(); break;
        case ObjectRole::SignalInlet: addSignalOutlet(); break;
        case ObjectRole::Outlet: addInlet(); break;
        case ObjectRole::SignalOutlet: addSignalInlet(); break;
        case ObjectRole::Normal: break;
        }
    }
    [[nodiscard]] ObjectRole role() const override { return role_; }
    [[nodiscard]] bool wantsDsp() const override { return false; }

private:
    ObjectRole role_;
};

// ---------------------------------------------------------------------------
// The message box.

class Message final : public Object {
public:
    explicit Message(const ObjectArgs& a)
        : Object(a), content_(a.rawArgs.begin(), a.rawArgs.end()), dollarZero_(a.dollarZero),
          add_("add"), add2_("add2"), addComma_("addcomma"), addSemi_("addsemi"), addDollar_("adddollar"),
          addDollSym_("adddollsym"), click_("click") {
        addInlet();
        addOutlet();
        out_.reserve(content_.size() + 16);
    }

    void uiEvent(Symbol selector, AtomSpan args) override {
        if (selector == click_)
            eval({});
        else
            receive(0, selector, args);
    }

protected:
    void onBang(int) override { eval({}); }
    void onFloat(int, double f) override {
        const Atom a(f);
        eval(AtomSpan(&a, 1));
    }
    void onSymbol(int, Symbol s) override {
        const Atom a(s);
        eval(AtomSpan(&a, 1));
    }
    void onList(int, AtomSpan args) override { eval(args); }
    void onAnything(int, Symbol selector, AtomSpan args) override {
        if (selector == sym::set()) {
            content_.assign(args.begin(), args.end());
        } else if (selector == add2_) {
            content_.insert(content_.end(), args.begin(), args.end());
        } else if (selector == add_) {
            content_.insert(content_.end(), args.begin(), args.end());
            content_.push_back(Atom::semi());
        } else if (selector == addComma_) {
            content_.push_back(Atom::comma());
        } else if (selector == addSemi_) {
            content_.push_back(Atom::semi());
        } else if (selector == addDollar_) {
            content_.push_back(Atom::dollar(static_cast<int>(argFloat(args, 0, 1))));
        } else if (selector == addDollSym_) {
            content_.push_back(Atom::dollarSymbol(argSymbol(args, 0)));
        } else {
            // Like Pd: "foo 1 2" arrives as the list "foo 1 2" for $-expansion.
            in_.clear();
            in_.emplace_back(selector);
            in_.insert(in_.end(), args.begin(), args.end());
            eval(in_);
            return;
        }
        notifyUi(sym::set(), content_);
    }

private:
    Atom expand(const Atom& a, AtomSpan args) const {
        switch (a.type()) {
        case Atom::Type::Dollar: {
            const int idx = a.dollarIndex();
            if (idx == 0)
                return Atom(static_cast<double>(dollarZero_));
            if (idx <= static_cast<int>(args.size()))
                return args[static_cast<std::size_t>(idx - 1)];
            error("${}: argument number out of range", idx);
            return Atom(0.0);
        }
        case Atom::Type::DollarSymbol: return Atom(expandDollarSymbol(a.asSymbol(), dollarZero_, args));
        default: return a;
        }
    }

    void flush(Symbol target, bool toOutlet) {
        if (out_.empty())
            return;
        const AtomSpan msg(out_);
        Symbol selector = sym::list();
        AtomSpan rest = msg;
        if (msg[0].isSymbol()) {
            selector = msg[0].asSymbol();
            rest = msg.subspan(1);
        } else if (msg.size() == 1) {
            selector = sym::float_();
        }
        if (toOutlet)
            outMessage(0, selector, rest);
        else if (!target.empty())
            engine().deliverToName(target, selector, rest);
        out_.clear();
    }

    void eval(AtomSpan args) {
        // Content can change while we send (a message to ourselves); work on
        // a stable copy only if needed – Pd has the same hazard.
        bool toOutlet = true;
        Symbol target;
        bool expectTarget = false;
        out_.clear();
        for (std::size_t i = 0; i < content_.size(); ++i) {
            const Atom& a = content_[i];
            if (a.type() == Atom::Type::Semi) {
                flush(target, toOutlet);
                toOutlet = false;
                expectTarget = true;
                continue;
            }
            if (a.type() == Atom::Type::Comma) {
                flush(target, toOutlet);
                continue;
            }
            const Atom e = expand(a, args);
            if (expectTarget) {
                target = e.isSymbol() ? e.asSymbol() : Symbol();
                expectTarget = false;
                continue;
            }
            out_.push_back(e);
        }
        flush(target, toOutlet);
    }

    AtomVector content_;
    AtomVector out_;
    AtomVector in_;
    int dollarZero_;
    Symbol add_, add2_, addComma_, addSemi_, addDollar_, addDollSym_, click_;
};

} // namespace
} // namespace qure::objects

namespace qure {
using namespace objects;

void registerControlObjects(ObjectRegistry& r) {
    r.add<Bang>("bang", "output a bang for any input", {"b"});
    r.add("float", [](const ObjectArgs& a) { return std::make_unique<Float>(a, false); }, "store a number", {"f"});
    r.add("int", [](const ObjectArgs& a) { return std::make_unique<Float>(a, true); }, "store an integer", {"i"});
    r.add<SymbolObj>("symbol", "store a symbol");
    r.add<Print>("print", "print messages to the console");
    r.add<Trigger>("trigger", "sequence messages right to left, converting", {"t"});
    r.add<Route>("route", "route messages by their first element");
    r.add<Select>("select", "bang on matching numbers or symbols", {"sel"});
    r.add<Pack>("pack", "combine atoms into a list");
    r.add<Unpack>("unpack", "split a list into atoms");
    r.add<Spigot>("spigot", "pass or block messages");
    r.add<Moses>("moses", "split numbers by a threshold");
    r.add<Change>("change", "output only changed numbers");
    r.add<Swap>("swap", "swap two numbers");
    r.add<Value>("value", "shared named number", {"v"});
    r.add<Send>("send", "send messages to a name", {"s"});
    r.add<Receive>("receive", "receive messages sent to a name", {"r"});
    r.add<Loadbang>("loadbang", "bang when the patch has loaded");
    r.add<Until>("until", "loop");
    r.add<List>("list", "list operations: append prepend split trim length store");
    r.add("inlet", [](const ObjectArgs& a) { return std::make_unique<Iolet>(a, ObjectRole::Inlet); },
          "subpatch control inlet");
    r.add("outlet", [](const ObjectArgs& a) { return std::make_unique<Iolet>(a, ObjectRole::Outlet); },
          "subpatch control outlet");
    r.add("inlet~", [](const ObjectArgs& a) { return std::make_unique<Iolet>(a, ObjectRole::SignalInlet); },
          "subpatch signal inlet");
    r.add("outlet~", [](const ObjectArgs& a) { return std::make_unique<Iolet>(a, ObjectRole::SignalOutlet); },
          "subpatch signal outlet");
    r.add<Message>("@msg");
}

} // namespace qure
