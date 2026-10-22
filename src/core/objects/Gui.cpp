// Engine side of GUI objects. Everything visual lives in QML front ends;
// these classes implement Pd's message semantics and report value changes
// with notifyUi(). A front end interacts through uiEvent().
//
// Pd's iemgui argument layouts (send/receive name positions, init flag...)
// are honoured so existing patches keep working.

#include "Common.hpp"

namespace qure::objects {
namespace {

/// Shared plumbing: send/receive names, init-on-load, output to both the
/// outlet and the send name.
class GuiObject : public Object {
public:
    struct Layout {
        int send = -1, receive = -1, init = -1;
    };

    GuiObject(const ObjectArgs& a, Layout layout) : Object(a) {
        if (layout.send >= 0)
            send_ = nameOrNone(argSymbol(a.args, static_cast<std::size_t>(layout.send)));
        if (layout.receive >= 0)
            receiveBinding_ = &makeBinding(Binding::Kind::Receiver,
                                           nameOrNone(argSymbol(a.args, static_cast<std::size_t>(layout.receive))));
        if (layout.init >= 0)
            init_ = argFloat(a.args, static_cast<std::size_t>(layout.init)) != 0;
    }

    [[nodiscard]] bool wantsLoadbang() const override { return init_; }

protected:
    /// Pd hides the inlet when a receive name is set and the outlet when a
    /// send name is set; we keep both so connections are never lost.
    void createIolets(int inlets = 1, int outlets = 1) {
        for (int i = 0; i < inlets; ++i)
            addInlet();
        for (int o = 0; o < outlets; ++o)
            addOutlet();
    }

    void emitFloat(double f) {
        outFloat(0, f);
        if (!send_.empty()) {
            const Atom a(f);
            engine().deliverToName(send_, sym::float_(), AtomSpan(&a, 1));
        }
    }
    void emitBang() {
        outBang(0);
        if (!send_.empty())
            engine().deliverToName(send_, sym::bang(), {});
    }
    void emitSymbol(Symbol s) {
        outSymbol(0, s);
        if (!send_.empty()) {
            const Atom a(s);
            engine().deliverToName(send_, sym::symbol(), AtomSpan(&a, 1));
        }
    }
    void emitList(AtomSpan args) {
        outList(0, args);
        if (!send_.empty())
            engine().deliverToName(send_, sym::list(), args);
    }
    void showFloat(double f) {
        const Atom a(f);
        notifyUi(sym::float_(), AtomSpan(&a, 1));
    }

    /// Handles the iemgui messages every class understands ("send",
    /// "receive"). Returns true if consumed.
    bool commonMessage(Symbol selector, AtomSpan args) {
        if (selector.str() == "send") {
            send_ = nameOrNone(argSymbol(args, 0));
            return true;
        }
        if (selector.str() == "receive") {
            if (receiveBinding_)
                receiveBinding_->rebind(nameOrNone(argSymbol(args, 0)));
            return true;
        }
        // Appearance messages (color, label, size, ...) concern the front
        // end only; forward them so a QML component can react.
        notifyUi(selector, args);
        return true;
    }

    Symbol send_;
    NameBinding* receiveBinding_ = nullptr;
    bool init_ = false;
};

// ---------------------------------------------------------------------------
// [bng] size hold interrupt init send receive ...

class Bng final : public GuiObject {
public:
    explicit Bng(const ObjectArgs& a) : GuiObject(a, {4, 5, 3}) { createIolets(); }
    void loadbang() override { fire(); }
    void uiEvent(Symbol, AtomSpan) override { fire(); }

protected:
    void onBang(int) override { fire(); }
    void onFloat(int, double) override { fire(); }
    void onSymbol(int, Symbol) override { fire(); }
    void onList(int, AtomSpan) override { fire(); }
    void onAnything(int, Symbol selector, AtomSpan args) override {
        if (selector == sym::set())
            return;
        commonMessage(selector, args);
    }

private:
    void fire() {
        notifyUi(sym::bang());
        emitBang();
    }
};

// ---------------------------------------------------------------------------
// [tgl] size init send receive label ... init_value nonzero

class Tgl final : public GuiObject {
public:
    explicit Tgl(const ObjectArgs& a) : GuiObject(a, {2, 3, 1}) {
        nonzero_ = argFloat(a.args, 13, 1);
        if (nonzero_ == 0)
            nonzero_ = 1;
        if (init_)
            value_ = argFloat(a.args, 12);
        createIolets();
    }
    void loadbang() override {
        showFloat(value_);
        emitFloat(value_);
    }
    void uiEvent(Symbol, AtomSpan) override { onBang(0); }

protected:
    void onBang(int) override { onFloat(0, value_ != 0 ? 0 : nonzero_); }
    void onFloat(int, double f) override {
        value_ = f;
        if (f != 0)
            nonzero_ = f;
        showFloat(value_);
        emitFloat(value_);
    }
    void onAnything(int, Symbol selector, AtomSpan args) override {
        if (selector == sym::set()) {
            value_ = argFloat(args, 0);
            showFloat(value_);
        } else if (selector.str() == "nonzero") {
            nonzero_ = argFloat(args, 0, 1);
        } else {
            commonMessage(selector, args);
        }
    }

private:
    double value_ = 0;
    double nonzero_ = 1;
};

// ---------------------------------------------------------------------------
// Ranged number: [hsl]/[vsl]/[nbx]/[knob]. Layout: w h min max lin init send
// receive label ... value(16)

class Ranged final : public GuiObject {
public:
    Ranged(const ObjectArgs& a, Layout layout, int minIdx, int maxIdx, double defMin, double defMax)
        : GuiObject(a, layout) {
        min_ = argFloat(a.args, static_cast<std::size_t>(minIdx), defMin);
        max_ = argFloat(a.args, static_cast<std::size_t>(maxIdx), defMax);
        createIolets();
    }
    void loadbang() override { emitFloat(value_); }
    void uiEvent(Symbol selector, AtomSpan args) override {
        // The front end sends the new value in user units.
        if (selector == sym::float_() || selector == sym::list())
            onFloat(0, argFloat(args, 0));
        else
            receive(0, selector, args);
    }

protected:
    void onBang(int) override { emitFloat(value_); }
    void onFloat(int, double f) override {
        value_ = clip(f);
        showFloat(value_);
        emitFloat(value_);
    }
    void onAnything(int, Symbol selector, AtomSpan args) override {
        if (selector == sym::set()) {
            value_ = clip(argFloat(args, 0));
            showFloat(value_);
        } else if (selector.str() == "range") {
            min_ = argFloat(args, 0);
            max_ = argFloat(args, 1, min_ + 1);
            notifyUi(selector, args);
        } else {
            commonMessage(selector, args);
        }
    }

private:
    double clip(double f) const {
        const double lo = std::min(min_, max_), hi = std::max(min_, max_);
        if (lo == hi)
            return f;
        return std::clamp(f, lo, hi);
    }
    double min_, max_, value_ = 0;
};

// ---------------------------------------------------------------------------
// [hradio]/[vradio] size new_old init number send receive ... value(14)

class Radio final : public GuiObject {
public:
    explicit Radio(const ObjectArgs& a) : GuiObject(a, {4, 5, 2}) {
        cells_ = std::max(1, static_cast<int>(argFloat(a.args, 3, 8)));
        if (init_)
            value_ = argFloat(a.args, 14);
        createIolets();
    }
    void loadbang() override {
        showFloat(value_);
        emitFloat(value_);
    }
    void uiEvent(Symbol, AtomSpan args) override { onFloat(0, argFloat(args, 0)); }

protected:
    void onBang(int) override { emitFloat(value_); }
    void onFloat(int, double f) override {
        value_ = std::clamp(std::trunc(f), 0.0, static_cast<double>(cells_ - 1));
        showFloat(value_);
        emitFloat(value_);
    }
    void onAnything(int, Symbol selector, AtomSpan args) override {
        if (selector == sym::set()) {
            value_ = std::clamp(std::trunc(argFloat(args, 0)), 0.0, static_cast<double>(cells_ - 1));
            showFloat(value_);
        } else if (selector.str() == "number") {
            cells_ = std::max(1, static_cast<int>(argFloat(args, 0, 1)));
            notifyUi(selector, args);
        } else {
            commonMessage(selector, args);
        }
    }

private:
    int cells_;
    double value_ = 0;
};

// ---------------------------------------------------------------------------
// [vu] width height receive ...: left inlet RMS dB, right inlet peak dB.

class Vu final : public GuiObject {
public:
    explicit Vu(const ObjectArgs& a) : GuiObject(a, {-1, 2, -1}), rms_("rms"), peak_("peak") {
        addInlet();
        addInlet();
        addOutlet();
        addOutlet();
    }

protected:
    void onFloat(int inlet, double f) override {
        const Atom v(f);
        notifyUi(inlet == 0 ? rms_ : peak_, AtomSpan(&v, 1));
        outFloat(inlet, f);
    }
    void onAnything(int, Symbol selector, AtomSpan args) override { commonMessage(selector, args); }

private:
    Symbol rms_, peak_;
};

// ---------------------------------------------------------------------------
// [cnv] selsize width height send receive ...: decoration only.

class Cnv final : public GuiObject {
public:
    explicit Cnv(const ObjectArgs& a) : GuiObject(a, {3, 4, -1}) {}

    void receiveNamed(Symbol selector, AtomSpan args) override { notifyUi(selector, args); }
};

// ---------------------------------------------------------------------------
// Atom boxes: #X floatatom x y width min max label_pos label receive send

class FloatAtom final : public GuiObject {
public:
    explicit FloatAtom(const ObjectArgs& a) : GuiObject(a, {6, 5, -1}) {
        min_ = argFloat(a.args, 1);
        max_ = argFloat(a.args, 2);
        createIolets();
    }
    void uiEvent(Symbol selector, AtomSpan args) override {
        if (selector == sym::float_())
            onFloat(0, argFloat(args, 0));
        else
            receive(0, selector, args);
    }

protected:
    void onBang(int) override { emitFloat(value_); }
    void onFloat(int, double f) override {
        value_ = clip(f);
        showFloat(value_);
        emitFloat(value_);
    }
    void onSymbol(int, Symbol) override { error("no method for symbol"); }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == sym::set()) {
            value_ = clip(argFloat(args, 0));
            showFloat(value_);
        } else {
            Object::onAnything(inlet, selector, args);
        }
    }

private:
    double clip(double f) const { return (min_ == 0 && max_ == 0) ? f : std::clamp(f, min_, max_); }
    double min_, max_, value_ = 0;
};

class SymbolAtom final : public GuiObject {
public:
    explicit SymbolAtom(const ObjectArgs& a) : GuiObject(a, {6, 5, -1}) { createIolets(); }
    void uiEvent(Symbol selector, AtomSpan args) override {
        if (selector == sym::symbol())
            onSymbol(0, argSymbol(args, 0));
        else
            receive(0, selector, args);
    }

protected:
    void onBang(int) override { emitSymbol(value_); }
    void onSymbol(int, Symbol s) override {
        value_ = s;
        show();
        emitSymbol(value_);
    }
    void onAnything(int, Symbol selector, AtomSpan args) override {
        if (selector == sym::set())
            value_ = argSymbol(args, 0);
        else
            value_ = selector; // Pd: an incoming selector is taken as the symbol
        show();
        if (selector != sym::set())
            emitSymbol(value_);
    }

private:
    void show() {
        const Atom a(value_);
        notifyUi(sym::symbol(), AtomSpan(&a, 1));
    }
    Symbol value_;
};

class ListAtom final : public GuiObject {
public:
    explicit ListAtom(const ObjectArgs& a) : GuiObject(a, {6, 5, -1}) { createIolets(); }
    void uiEvent(Symbol selector, AtomSpan args) override { onList(0, selector == sym::list() ? args : args); }

protected:
    void onBang(int) override { emitList(value_); }
    void onFloat(int, double f) override {
        const Atom a(f);
        onList(0, AtomSpan(&a, 1));
    }
    void onSymbol(int, Symbol s) override {
        const Atom a(s);
        onList(0, AtomSpan(&a, 1));
    }
    void onList(int, AtomSpan args) override {
        value_.assign(args.begin(), args.end());
        notifyUi(sym::list(), value_);
        emitList(value_);
    }
    void onAnything(int, Symbol selector, AtomSpan args) override {
        if (selector == sym::set()) {
            value_.assign(args.begin(), args.end());
            notifyUi(sym::list(), value_);
            return;
        }
        messageAsList(selector, args, value_);
        notifyUi(sym::list(), value_);
        emitList(value_);
    }

private:
    AtomVector value_;
};

// ---------------------------------------------------------------------------
// Objects defined purely by a QML front end (see makeFrontendObject).

class FrontendObject final : public Object {
public:
    FrontendObject(const ObjectArgs& a, int inlets, int outlets) : Object(a), out_("out"), in_("in") {
        for (int i = 0; i < inlets; ++i)
            addInlet();
        for (int o = 0; o < outlets; ++o)
            addOutlet();
        buf_.reserve(32);
    }

    // The front end calls node.output(outlet, atoms) → "out outlet atoms...".
    void uiEvent(Symbol selector, AtomSpan args) override {
        if (selector != out_ || args.empty() || !args[0].isFloat()) {
            receive(0, selector, args);
            return;
        }
        const int outlet = static_cast<int>(args[0].asFloat());
        if (outlet < 0 || outlet >= numOutlets())
            return;
        const auto rest = args.subspan(1);
        if (rest.empty())
            outBang(outlet);
        else if (rest[0].isSymbol())
            outAnything(outlet, rest[0].asSymbol(), rest.subspan(1));
        else
            outMessage(outlet, sym::list(), rest);
    }

protected:
    void onBang(int inlet) override { forward(inlet, sym::bang(), {}); }
    void onFloat(int inlet, double f) override {
        const Atom a(f);
        forward(inlet, sym::float_(), AtomSpan(&a, 1));
    }
    void onSymbol(int inlet, Symbol s) override {
        const Atom a(s);
        forward(inlet, sym::symbol(), AtomSpan(&a, 1));
    }
    void onList(int inlet, AtomSpan args) override { forward(inlet, sym::list(), args); }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override { forward(inlet, selector, args); }

private:
    // Delivered to QML as node.received(inlet, selector, args).
    void forward(int inlet, Symbol selector, AtomSpan args) {
        buf_.clear();
        buf_.emplace_back(inlet);
        buf_.emplace_back(selector);
        buf_.insert(buf_.end(), args.begin(), args.end());
        notifyUi(in_, buf_);
    }

    Symbol out_, in_;
    AtomVector buf_;
};

} // namespace
} // namespace qure::objects

namespace qure {
using namespace objects;

std::unique_ptr<Object> makeFrontendObject(const ObjectArgs& args, int inlets, int outlets) {
    return std::make_unique<FrontendObject>(args, inlets, outlets);
}

void registerGuiObjects(ObjectRegistry& r) {
    r.add<Bng>("bng", "bang button");
    r.add<Tgl>("tgl", "toggle", {"toggle"});
    // hsl/vsl: w h min max lin init send(6) receive(7)
    auto slider = [](const ObjectArgs& a) {
        return std::make_unique<Ranged>(a, GuiObject::Layout{6, 7, 5}, 2, 3, 0.0, 127.0);
    };
    r.add("hsl", slider, "horizontal slider", {"hslider"});
    r.add("vsl", slider, "vertical slider", {"vslider"});
    // nbx: digits h min max lin init send(6) receive(7)
    r.add("nbx", [](const ObjectArgs& a) {
        return std::make_unique<Ranged>(a, GuiObject::Layout{6, 7, 5}, 2, 3, -1e37, 1e37);
    }, "number box (iemgui)", {"numbox"});
    // knob: size min max init send receive
    r.add("knob", [](const ObjectArgs& a) {
        return std::make_unique<Ranged>(a, GuiObject::Layout{4, 5, 3}, 1, 2, 0.0, 127.0);
    }, "rotary knob");
    r.add<Radio>("hradio", "horizontal radio buttons", {"hdl", "rdb", "radiobut", "radiobutton"});
    r.add<Radio>("vradio", "vertical radio buttons", {"vdl"});
    r.add<Vu>("vu", "VU meter");
    r.add<Cnv>("cnv", "canvas (decoration)", {"my_canvas"});
    r.add<FloatAtom>("@floatatom");
    r.add<SymbolAtom>("@symbolatom");
    r.add<ListAtom>("@listbox");
}

} // namespace qure
