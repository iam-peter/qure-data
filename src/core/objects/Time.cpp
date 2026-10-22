// Scheduling objects: [metro] [delay] [timer] [pipe] [line] [cputime]
// [realtime]. Everything runs in logical time through the object's clocks.

#include "Common.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>

namespace qure::objects {
namespace {

// ---------------------------------------------------------------------------
// Tempo units

/// Pd's parsetimeunits(): the length of one time unit in milliseconds for
/// `tempo <amount> <unit>`, e.g. `1 sec`, `120 permin`, `1 samp`. Compares
/// string views only, so it is safe on the engine thread.
std::optional<double> parseTimeUnit(double amount, std::string_view unit, double sampleRate) {
    if (amount <= 0)
        amount = 1;
    const bool per = unit.starts_with("per");
    if (per)
        unit.remove_prefix(3);
    double ms = 0;
    if (unit == "millisecond" || unit == "msec")
        ms = 1;
    else if (unit.starts_with("sec"))
        ms = 1000;
    else if (unit.starts_with("min"))
        ms = 60000;
    else if (unit.starts_with("sam"))
        ms = 1000.0 / sampleRate;
    else
        return std::nullopt;
    return per ? ms / amount : ms * amount;
}

/// Shared by [metro] and [delay]: a clock whose delay is counted in tempo
/// units. Changing the unit while the clock is set rescales the time left,
/// like Pd's clock_setunit().
class UnitClock {
public:
    void init(Clock& clock) { clock_ = &clock; }
    void delay(double units) { clock_->delay(units * unitMs_); }
    void unset() { clock_->unset(); }
    void setUnit(double unitMs, double now) {
        if (clock_->isSet()) {
            const double unitsLeft = (clock_->dueTime() - now) / unitMs_;
            clock_->setAt(now + unitsLeft * unitMs);
        }
        unitMs_ = unitMs;
    }
    [[nodiscard]] double unitMs() const { return unitMs_; }

private:
    Clock* clock_ = nullptr;
    double unitMs_ = 1;
};

// ---------------------------------------------------------------------------

class Metro final : public Object {
public:
    explicit Metro(const ObjectArgs& a) : Object(a), stop_(Symbol("stop")), tempo_(Symbol("tempo")) {
        setInterval(argFloat(a.args, 0));
        clock_.init(makeClock([this] { tick(); }));
        if (const double t = argFloat(a.args, 1); t != 0)
            clock_.setUnit(parseTimeUnit(t, argSymbol(a.args, 2).str(), sampleRate()).value_or(1), 0);
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            onFloat(0, 1);
        else
            Object::onBang(inlet);
    }
    void onFloat(int inlet, double f) override {
        if (inlet == 1) {
            setInterval(f);
            return;
        }
        if (f != 0)
            tick();
        else
            clock_.unset();
        hit_ = true;
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet == 0 && selector == stop_) {
            onFloat(0, 0);
        } else if (inlet == 0 && selector == tempo_) {
            const auto unit = parseTimeUnit(argFloat(args, 0), argSymbol(args, 1).str(), sampleRate());
            if (!unit)
                error("{}: unknown time unit", argSymbol(args, 1).str());
            clock_.setUnit(unit.value_or(1), now());
        } else {
            Object::onAnything(inlet, selector, args);
        }
    }

private:
    void setInterval(double g) { interval_ = g <= 0 ? 1 : g; }

    void tick() {
        // A [stop] or new start triggered by our own output sets hit_, in
        // which case the clock must not be re-armed here.
        hit_ = false;
        outBang(0);
        if (!hit_)
            clock_.delay(interval_); // now() is the due time: drift free
    }

    UnitClock clock_;
    double interval_ = 1;
    bool hit_ = false;
    Symbol stop_, tempo_;
};

// ---------------------------------------------------------------------------

class Delay final : public Object {
public:
    explicit Delay(const ObjectArgs& a) : Object(a), stop_(Symbol("stop")), tempo_(Symbol("tempo")) {
        setTime(argFloat(a.args, 0));
        clock_.init(makeClock([this] { outBang(0); }));
        if (const double t = argFloat(a.args, 1); t != 0)
            clock_.setUnit(parseTimeUnit(t, argSymbol(a.args, 2).str(), sampleRate()).value_or(1), 0);
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            clock_.delay(time_);
        else
            Object::onBang(inlet);
    }
    void onFloat(int inlet, double f) override {
        setTime(f);
        if (inlet == 0)
            clock_.delay(time_);
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet == 0 && selector == stop_) {
            clock_.unset();
        } else if (inlet == 0 && selector == tempo_) {
            const auto unit = parseTimeUnit(argFloat(args, 0), argSymbol(args, 1).str(), sampleRate());
            if (!unit)
                error("{}: unknown time unit", argSymbol(args, 1).str());
            clock_.setUnit(unit.value_or(1), now());
        } else {
            Object::onAnything(inlet, selector, args);
        }
    }

private:
    void setTime(double g) { time_ = g < 0 ? 0 : g; }

    UnitClock clock_;
    double time_ = 0;
    Symbol stop_, tempo_;
};

// ---------------------------------------------------------------------------

class Timer final : public Object {
public:
    explicit Timer(const ObjectArgs& a) : Object(a), tempo_(Symbol("tempo")) {
        if (const double t = argFloat(a.args, 0); t != 0)
            unitMs_ = parseTimeUnit(t, argSymbol(a.args, 1).str(), sampleRate()).value_or(1);
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onActivate() override { reset(); }
    void onBang(int inlet) override {
        if (inlet == 0)
            reset();
        else
            outFloat(0, elapsed() + moreElapsed_);
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet == 0 && selector == tempo_) {
            moreElapsed_ += elapsed();
            setTime_ = now();
            const auto unit = parseTimeUnit(argFloat(args, 0), argSymbol(args, 1).str(), sampleRate());
            if (!unit)
                error("{}: unknown time unit", argSymbol(args, 1).str());
            unitMs_ = unit.value_or(1);
        } else {
            Object::onAnything(inlet, selector, args);
        }
    }

private:
    void reset() {
        setTime_ = now();
        moreElapsed_ = 0;
    }
    [[nodiscard]] double elapsed() const { return (now() - setTime_) / unitMs_; }

    double setTime_ = 0, moreElapsed_ = 0, unitMs_ = 1;
    Symbol tempo_;
};

// ---------------------------------------------------------------------------

/// [pipe]: delays numbers, symbols and lists. Pending events live in a
/// pre-reserved pool and a binary heap ordered by (due time, arrival), so
/// scheduling only allocates when more events are pending than ever before.
/// One clock is armed for the earliest event.
class Pipe final : public Object {
public:
    explicit Pipe(const ObjectArgs& a) : Object(a), flush_(Symbol("flush")), clear_(Symbol("clear")) {
        AtomSpan args = a.args;
        if (!args.empty()) {
            if (!args.back().isFloat())
                throw ObjectError(std::format("pipe: {}: bad time delay value", args.back().toString()));
            delay_ = args.back().asFloat();
            args = args.first(args.size() - 1);
        }
        if (args.empty()) {
            values_.emplace_back(0.0);
        } else {
            for (const Atom& arg : args) {
                if (arg.isFloat()) {
                    values_.push_back(arg);
                    continue;
                }
                const auto s = arg.asSymbol().str();
                const char c = s.empty() ? 'f' : s[0];
                if (c == 's')
                    values_.emplace_back(sym::symbol());
                else if (c == 'p')
                    throw ObjectError("pipe: pointers are not supported");
                else if (c == 'f')
                    values_.emplace_back(0.0);
                else
                    throw ObjectError(std::format("pipe: {}: bad type", s));
            }
        }
        n_ = values_.size();
        for (std::size_t i = 0; i <= n_; ++i)
            addInlet(); // values..., then the delay time
        for (std::size_t i = 0; i < n_; ++i)
            addOutlet();
        clock_ = &makeClock([this] { fire(); });
        grow(kInitialCapacity);
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            onList(0, {});
        else
            Object::onBang(inlet);
    }
    void onFloat(int inlet, double f) override {
        const Atom a(f);
        if (inlet == 0)
            onList(0, AtomSpan(&a, 1));
        else if (static_cast<std::size_t>(inlet) == n_)
            delay_ = f;
        else if (values_[static_cast<std::size_t>(inlet)].isFloat())
            values_[static_cast<std::size_t>(inlet)] = a;
        else
            error("inlet {}: expected a symbol", inlet);
    }
    void onSymbol(int inlet, Symbol s) override {
        const Atom a(s);
        if (inlet == 0)
            onList(0, AtomSpan(&a, 1));
        else if (static_cast<std::size_t>(inlet) < n_ && values_[static_cast<std::size_t>(inlet)].isSymbol())
            values_[static_cast<std::size_t>(inlet)] = a;
        else
            error("inlet {}: expected a number", inlet);
    }
    void onList(int inlet, AtomSpan args) override {
        if (inlet != 0) {
            Object::onList(inlet, args);
            return;
        }
        if (args.size() > n_) {
            if (args[n_].isFloat())
                delay_ = args[n_].asFloat();
            else
                error("symbol in time inlet");
            args = args.first(n_);
        }
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (values_[i].isFloat())
                values_[i] = Atom(args[i].asFloat());
            else
                values_[i] = args[i].isSymbol() ? args[i] : Atom(sym::float_());
        }
        schedule();
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet == 0 && selector == flush_)
            flush();
        else if (inlet == 0 && selector == clear_)
            clear();
        else
            Object::onAnything(inlet, selector, args);
    }
    void onDeactivate() override { clear(); }

private:
    static constexpr std::size_t kInitialCapacity = 256;

    struct Event {
        double due;
        std::uint64_t seq;
        std::uint32_t slot;
    };
    /// Heap order: the earliest event (first arrived among equals) on top.
    static bool later(const Event& a, const Event& b) {
        return a.due > b.due || (a.due == b.due && a.seq > b.seq);
    }

    void grow(std::size_t capacity) {
        const std::size_t old = pool_.size() / n_;
        pool_.resize(capacity * n_);
        heap_.reserve(capacity);
        free_.reserve(capacity);
        for (std::size_t s = capacity; s-- > old;)
            free_.push_back(static_cast<std::uint32_t>(s));
    }

    void schedule() {
        if (free_.empty())
            grow(2 * (pool_.size() / n_)); // rare: more events pending than ever before
        const std::uint32_t slot = free_.back();
        free_.pop_back();
        std::copy(values_.begin(), values_.end(), pool_.begin() + static_cast<std::ptrdiff_t>(slot * n_));
        heap_.push_back({now() + (delay_ >= 0 ? delay_ : 0), nextSeq_++, slot});
        std::ranges::push_heap(heap_, later);
        rearm();
    }

    void rearm() {
        if (heap_.empty())
            clock_->unset();
        else if (!clock_->isSet() || clock_->dueTime() != heap_.front().due)
            clock_->setAt(heap_.front().due);
    }

    /// Outputs one event's values right to left, then frees its slot. The
    /// pool is indexed afresh for every atom since outputs may re-enter and
    /// grow it.
    void output(std::uint32_t slot) {
        for (std::size_t i = n_; i-- > 0;) {
            const Atom a = pool_[slot * n_ + i];
            if (a.isFloat())
                outFloat(static_cast<int>(i), a.asFloat());
            else
                outSymbol(static_cast<int>(i), a.asSymbol());
        }
        free_.push_back(slot);
    }

    void fire() {
        // Each due event is output from its own clock firing; re-arming at
        // the same time queues us behind other clocks due then (as Pd's
        // per-event clocks would be).
        if (heap_.empty())
            return;
        if (heap_.front().due > now()) {
            rearm();
            return;
        }
        std::ranges::pop_heap(heap_, later);
        const Event e = heap_.back();
        heap_.pop_back();
        output(e.slot);
        rearm();
    }

    void flush() {
        // Pd outputs pending events newest first.
        while (!heap_.empty()) {
            const auto newest = std::ranges::max_element(heap_, {}, &Event::seq);
            const Event e = *newest;
            *newest = heap_.back();
            heap_.pop_back();
            std::ranges::make_heap(heap_, later);
            output(e.slot);
        }
        rearm();
    }

    void clear() {
        for (const Event& e : heap_)
            free_.push_back(e.slot);
        heap_.clear();
        clock_->unset();
    }

    AtomVector values_; // current values; float atoms or symbol atoms by type
    std::size_t n_ = 0;
    double delay_ = 0;
    Clock* clock_ = nullptr;
    std::vector<Event> heap_;
    AtomVector pool_;
    std::vector<std::uint32_t> free_;
    std::uint64_t nextSeq_ = 0;
    Symbol flush_, clear_;
};

// ---------------------------------------------------------------------------

/// [line]: control-rate ramps, output every `grain` ms.
class Line final : public Object {
public:
    static constexpr double kDefaultGrain = 20;

    explicit Line(const ObjectArgs& a)
        : Object(a), targetVal_(argFloat(a.args, 0)), setVal_(targetVal_), grain_(argFloat(a.args, 1)),
          stop_(Symbol("stop")) {
        clock_ = &makeClock([this] { tick(); });
        addInlet(); // target
        addInlet(); // ramp time
        addInlet(); // grain
        addOutlet();
    }

protected:
    void onActivate() override { targetTime_ = prevTime_ = now(); }

    void onFloat(int inlet, double f) override {
        switch (inlet) {
        case 0: start(f); break;
        case 1:
            rampTime_ = f;
            gotInlet_ = true;
            break;
        default: grain_ = f; break;
        }
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (inlet == 0 && selector == stop_) {
            setVal_ = current(now());
            targetVal_ = setVal_;
            clock_->unset();
        } else if (inlet == 0 && selector == sym::set()) {
            clock_->unset();
            targetVal_ = setVal_ = argFloat(args, 0);
        } else {
            Object::onAnything(inlet, selector, args);
        }
    }

private:
    [[nodiscard]] double current(double t) const {
        if (t >= targetTime_)
            return targetVal_;
        return setVal_ + oneOverTimeDiff_ * (t - prevTime_) * (targetVal_ - setVal_);
    }

    double grain() {
        if (grain_ <= 0)
            grain_ = kDefaultGrain;
        return grain_;
    }

    void start(double f) {
        const double t = now();
        if (gotInlet_ && rampTime_ > 0) {
            setVal_ = current(t);
            prevTime_ = t;
            targetTime_ = t + rampTime_;
            targetVal_ = f;
            oneOverTimeDiff_ = 1. / rampTime_;
            gotInlet_ = false;
            tick();
        } else {
            clock_->unset();
            targetVal_ = setVal_ = f;
            outFloat(0, f);
        }
        gotInlet_ = false;
    }

    void tick() {
        const double t = now();
        const double msToGo = targetTime_ - t;
        if (msToGo < 1e-9) {
            outFloat(0, targetVal_);
            return;
        }
        // Arm first: the output may stop or restart the ramp.
        clock_->delay(std::min(grain(), msToGo));
        outFloat(0, setVal_ + oneOverTimeDiff_ * (t - prevTime_) * (targetVal_ - setVal_));
    }

    Clock* clock_ = nullptr;
    double targetTime_ = 0, prevTime_ = 0;
    double targetVal_, setVal_;
    double rampTime_ = 0;
    double grain_;
    double oneOverTimeDiff_ = 1;
    bool gotInlet_ = false;
    Symbol stop_;
};

// ---------------------------------------------------------------------------

/// [cputime] (process CPU time) and [realtime] (wall clock): left bang
/// resets, right bang outputs the milliseconds elapsed since.
template <class Source>
class Stopwatch final : public Object {
public:
    explicit Stopwatch(const ObjectArgs& a) : Object(a), start_(Source::now()) {
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet == 0)
            start_ = Source::now();
        else
            outFloat(0, Source::now() - start_);
    }

private:
    double start_;
};

struct CpuClock {
    static double now() { return static_cast<double>(std::clock()) * 1000.0 / CLOCKS_PER_SEC; }
};
struct RealClock {
    static double now() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

} // namespace
} // namespace qure::objects

namespace qure {
using namespace objects;

void registerTimeObjects(ObjectRegistry& r) {
    r.add<Metro>("metro", "bang repeatedly at an interval");
    r.add<Delay>("delay", "bang after a delay", {"del"});
    r.add<Timer>("timer", "measure logical time between bangs");
    r.add<Pipe>("pipe", "delay numbers, symbols and lists");
    r.add<Line>("line", "ramp to a target value at control rate");
    r.add<Stopwatch<CpuClock>>("cputime", "measure CPU time");
    r.add<Stopwatch<RealClock>>("realtime", "measure real (wall clock) time");
}

} // namespace qure
