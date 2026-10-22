#include "Object.hpp"
#include "Engine.hpp"

#include <array>
#include <cassert>

namespace qure {

Object::Object(const ObjectArgs& args)
    : engine_(args.engine), id_(args.engine.allocateObjectId()), className_(args.className) {}

Object::~Object() = default;

int Object::addIolet(std::vector<Iolet>& v, bool signal, int& signalCount) {
    v.push_back({signal, signal ? signalCount++ : -1});
    return static_cast<int>(v.size()) - 1;
}

int Object::addTextureInlet() {
    const int index = addIolet(inlets_, false, numSignalIn_);
    inlets_.back().texture = true;
    return index;
}

int Object::addTextureOutlet() {
    const int index = addIolet(outlets_, false, numSignalOut_);
    outlets_.back().texture = true;
    return index;
}

int Object::addSignalInlet(float initialScalar) {
    const int index = addIolet(inlets_, true, numSignalIn_);
    scalars_.push_back(initialScalar);
    return index;
}

// ---------------------------------------------------------------------------
// Dispatch

void Object::receive(int inlet, Symbol selector, AtomSpan args) {
    struct DepthGuard {
        Engine& e;
        ~DepthGuard() { e.leaveMessage(); }
    } guard{engine_};
    if (!engine_.enterMessage()) {
        error("stack overflow (message recursion deeper than 1000)");
        return;
    }

    if (selector == sym::bang())
        onBang(inlet);
    else if (selector == sym::float_())
        onFloat(inlet, args.empty() ? 0.0 : args[0].asFloat());
    else if (selector == sym::symbol())
        onSymbol(inlet, args.empty() ? Symbol() : args[0].asSymbol());
    else if (selector == sym::list())
        onList(inlet, args);
    else
        onAnything(inlet, selector, args);
}

void Object::onBang(int inlet) { onAnything(inlet, sym::bang(), {}); }

void Object::onFloat(int inlet, double f) {
    if (inlet < numInlets() && inlets_[inlet].signal) {
        scalars_[inlets_[inlet].signalIndex] = static_cast<float>(f);
        return;
    }
    const Atom a(f);
    onAnything(inlet, sym::float_(), AtomSpan(&a, 1));
}

void Object::onSymbol(int inlet, Symbol s) {
    const Atom a(s);
    onAnything(inlet, sym::symbol(), AtomSpan(&a, 1));
}

void Object::onList(int inlet, AtomSpan args) {
    // Pd's default list behaviour (pd_defaultlist / obj_list).
    if (args.empty()) {
        onBang(inlet);
        return;
    }
    if (args.size() == 1) {
        if (args[0].isFloat())
            onFloat(inlet, args[0].asFloat());
        else
            onSymbol(inlet, args[0].asSymbol());
        return;
    }
    if (inlet != 0) {
        onAnything(inlet, sym::list(), args);
        return;
    }
    // Spread the tail over the other inlets, then the head into inlet 0.
    const auto n = std::min<std::size_t>(args.size(), static_cast<std::size_t>(numInlets()));
    for (std::size_t i = 1; i < n; ++i) {
        if (args[i].isFloat())
            receive(static_cast<int>(i), sym::float_(), args.subspan(i, 1));
        else
            receive(static_cast<int>(i), sym::symbol(), args.subspan(i, 1));
    }
    if (args[0].isFloat())
        onFloat(0, args[0].asFloat());
    else
        onSymbol(0, args[0].asSymbol());
}

void Object::onAnything(int inlet, Symbol selector, AtomSpan) {
    if (numInlets() == 0)
        error("no inlets: dropped '{}'", selector.str());
    else
        error("inlet {}: no method for '{}'", inlet, selector.str());
}

// ---------------------------------------------------------------------------
// Output

void Object::deliver(int outlet, Symbol selector, AtomSpan args) {
    if (!bindings_)
        return;
    const OutletTargets& t = bindings_[outlet];
    for (std::uint32_t i = 0; i < t.count; ++i)
        t.begin[i].object->receive(t.begin[i].inlet, selector, args);
}

void Object::outBang(int outlet) { deliver(outlet, sym::bang(), {}); }

void Object::outFloat(int outlet, double f) {
    const Atom a(f);
    deliver(outlet, sym::float_(), AtomSpan(&a, 1));
}

void Object::outSymbol(int outlet, Symbol s) {
    const Atom a(s);
    deliver(outlet, sym::symbol(), AtomSpan(&a, 1));
}

void Object::outList(int outlet, AtomSpan args) { deliver(outlet, sym::list(), args); }

void Object::outAnything(int outlet, Symbol selector, AtomSpan args) { deliver(outlet, selector, args); }

void Object::outMessage(int outlet, Symbol selector, AtomSpan args) {
    if (selector == sym::list() && args.size() == 1) {
        if (args[0].isFloat())
            outFloat(outlet, args[0].asFloat());
        else
            outSymbol(outlet, args[0].asSymbol());
        return;
    }
    if (selector == sym::list() && args.empty()) {
        outBang(outlet);
        return;
    }
    deliver(outlet, selector, args);
}

void Object::notifyUi(Symbol selector, AtomSpan args) { engine_.notify(id_, selector, args); }

// ---------------------------------------------------------------------------
// Lifecycle

Clock& Object::makeClock(std::function<void()> callback) {
    clocks_.push_back(std::make_unique<Clock>(engine_.scheduler(), std::move(callback)));
    return *clocks_.back();
}

NameBinding& Object::makeBinding(Binding::Kind kind, Symbol name) {
    names_.push_back(std::make_unique<NameBinding>(kind, this, name));
    return *names_.back();
}

void Object::activate() {
    if (active_)
        return;
    active_ = true;
    for (auto& n : names_)
        n->bind();
    onActivate();
}

void Object::deactivate() {
    if (!active_)
        return;
    active_ = false;
    onDeactivate();
    for (auto& c : clocks_)
        c->unset();
    for (auto& n : names_)
        n->unbind();
    bindings_ = nullptr;
}

// ---------------------------------------------------------------------------
// Utilities

namespace {
/// Output iterator writing into a fixed buffer, silently truncating.
/// Copies share the cursor, so `*it++ = c` works as formatters expect.
struct Cursor {
    char* pos;
    char* end;
    [[nodiscard]] std::size_t used(const char* begin) const { return static_cast<std::size_t>(pos - begin); }
};
struct BoundedWriter {
    using difference_type = std::ptrdiff_t;
    Cursor* cursor;
    BoundedWriter& operator=(char c) {
        if (cursor->pos != cursor->end)
            *cursor->pos++ = c;
        return *this;
    }
    BoundedWriter& operator*() { return *this; }
    BoundedWriter& operator++() { return *this; }
    BoundedWriter operator++(int) { return *this; }
};
} // namespace

void Object::post(LogLevel level, std::string_view text) const {
    std::array<char, 512> buf{};
    Cursor cursor{buf.data(), buf.data() + buf.size()};
    BoundedWriter w{&cursor};
    // Internal classes ("@msg") are named after what the user sees.
    auto name = className_.str();
    if (name == "@msg")
        name = "message";
    else if (name.starts_with('@'))
        name.remove_prefix(1);
    for (char c : name)
        w = c;
    w = ':';
    w = ' ';
    for (char c : text)
        w = c;
    engine_.log(level, std::string_view(buf.data(), cursor.used(buf.data())));
}

void Object::postFormatted(LogLevel level, std::string_view fmt, std::format_args args) const {
    std::array<char, 480> buf{};
    Cursor cursor{buf.data(), buf.data() + buf.size()};
    std::vformat_to(BoundedWriter{&cursor}, fmt, args);
    post(level, std::string_view(buf.data(), cursor.used(buf.data())));
}

double Object::now() const noexcept { return engine_.now(); }
double Object::sampleRate() const noexcept { return engine_.config().sampleRate; }
int Object::blockSize() const noexcept { return engine_.config().blockSize; }

} // namespace qure
